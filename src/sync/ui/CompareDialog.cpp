#include "CompareDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace bld::sync {

using merge::Choice;
using merge::ItemChange;
using merge::Side;
using merge::Status;

namespace {

constexpr int kKeyRole = Qt::UserRole + 1;

}  // namespace

CompareDialog::CompareDialog(const QList<ItemChange>& changes, QWidget* parent)
    : QDialog(parent), changes_(changes) {
    setWindowTitle(tr("Your offline changes"));
    resize(760, 480);

    int mine = 0, clashes = 0, theirs = 0;
    for (const auto& c : changes_) {
        if (c.status == Status::Server) ++theirs;
        else if (c.status == Status::Conflict) ++clashes;
        else if (c.status == Status::Mine) ++mine;
    }
    auto* intro = new QLabel(this);
    intro->setObjectName(QStringLiteral("intro"));
    intro->setWordWrap(true);
    // Separate strings for one: no English plural catalogue is shipped, so
    // "%n change(s)" would read that way.
    const int made = mine + clashes;
    QString text;
    if (clashes == 0)
        text = mine == 1 ? tr("While you were offline you made 1 change. It doesn't clash with what changed on the "
                              "server meanwhile. Apply it to the live layout, or discard it.")
                         : tr("While you were offline you made %n changes. None of them clash with what changed "
                              "on the server meanwhile. Apply them to the live layout, or discard them.",
                              nullptr, mine);
    else if (made == 1)
        text = tr("While you were offline you made 1 change, and it clashes with what changed on the server "
                  "meanwhile. Choose what to keep.");
    else if (clashes == 1)
        text = tr("While you were offline you made %n changes; 1 of them clashes with what changed on the "
                  "server meanwhile. For the clash, choose what to keep.", nullptr, made);
    else
        text = tr("While you were offline you made %1 changes; %2 of them clash with what changed on the "
                  "server meanwhile. For each clash, choose what to keep.")
                   .arg(made)
                   .arg(clashes);
    intro->setText(text);

    list_ = new QTreeWidget(this);
    list_->setColumnCount(2);
    list_->setHeaderLabels({ tr("Change"), tr("Keep") });
    list_->setRootIsDecorated(false);
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list_->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    int keepWidth = list_->header()->sectionSizeHint(1);
    // Clashes first, then my other changes, then the server's for reference.
    for (const Status group : { Status::Conflict, Status::Mine, Status::Same, Status::Server }) {
        for (const auto& c : changes_) {
            if (c.status != group) continue;
            auto* row = new QTreeWidgetItem(list_, { merge::describe(c) });
            row->setData(0, kKeyRole, c.key);
            if (c.status == Status::Server || c.status == Status::Same) {
                row->setText(1, c.status == Status::Same ? tr("Both the same") : tr("Server's change"));
                for (int col : { 0, 1 }) row->setForeground(col, palette().color(QPalette::Disabled, QPalette::Text));
                continue;
            }
            auto* combo = new QComboBox(list_);
            if (c.status == Status::Mine) {
                combo->addItem(tr("Apply mine"), static_cast<int>(Choice::Mine));
                combo->addItem(tr("Don't apply"), static_cast<int>(Choice::Server));
            } else {
                combo->addItem(tr("Keep mine"), static_cast<int>(Choice::Mine));
                combo->addItem(tr("Keep server"), static_cast<int>(Choice::Server));
                if (c.allowsBoth()) combo->addItem(tr("Keep both"), static_cast<int>(Choice::Both));
                // Nothing is lost by default: the server's stays until you pick mine.
                combo->setCurrentIndex(1);
                row->setIcon(0, style()->standardIcon(QStyle::SP_MessageBoxWarning));
            }
            keepWidth = std::max(keepWidth, combo->sizeHint().width());
            combos_.insert(c.key, combo);
            list_->setItemWidget(row, 1, combo);
        }
    }
    // Just wide enough for the choices; the changes take the rest.
    list_->header()->resizeSection(1, keepWidth + 8);
    connect(list_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* row) {
        if (!row) return;
        const QString key = row->data(0, kKeyRole).toString();
        for (const auto& c : std::as_const(changes_))
            if (c.key == key) emit highlight(c);
    });

    auto* allMine = new QPushButton(tr("All Mine"), this);
    auto* allServer = new QPushButton(tr("All Server"), this);
    connect(allMine, &QPushButton::clicked, this, [this] { chooseAll(Choice::Mine); });
    connect(allServer, &QPushButton::clicked, this, [this] { chooseAll(Choice::Server); });
    auto* bulk = new QHBoxLayout;
    bulk->addWidget(allMine);
    bulk->addWidget(allServer);
    bulk->addStretch();

    auto* buttons = new QDialogButtonBox(this);
    auto* apply = buttons->addButton(tr("Apply"), QDialogButtonBox::AcceptRole);
    auto* discard = buttons->addButton(tr("Discard My Changes"), QDialogButtonBox::DestructiveRole);
    auto* replace = buttons->addButton(tr("Replace Server with Mine..."), QDialogButtonBox::ActionRole);
    auto* saveNew = buttons->addButton(tr("Save Mine as a New Layout..."), QDialogButtonBox::ActionRole);
    auto* later = buttons->addButton(tr("Later"), QDialogButtonBox::RejectRole);
    apply->setDefault(true);
    later->setToolTip(tr("Keep your offline changes waiting; File > Review Offline Changes brings this back"));
    connect(apply, &QPushButton::clicked, this, [this] { finish(Action::Apply); });
    connect(discard, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, windowTitle(), tr("Throw away the changes you made offline?"),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            == QMessageBox::Yes)
            finish(Action::Discard);
    });
    connect(replace, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, windowTitle(),
                                  tr("Put your version in place of the server's? Everything changed on the "
                                     "server while you were offline is undone for everyone (they can "
                                     "still undo this)."),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            == QMessageBox::Yes)
            finish(Action::ReplaceServer);
    });
    connect(saveNew, &QPushButton::clicked, this, [this] { finish(Action::SaveAsNew); });
    connect(later, &QPushButton::clicked, this, [this] { finish(Action::Later); });

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(list_, 1);
    layout->addLayout(bulk);
    layout->addWidget(buttons);
}

QHash<QString, Choice> CompareDialog::choices() const {
    QHash<QString, Choice> out;
    for (auto it = combos_.constBegin(); it != combos_.constEnd(); ++it)
        out.insert(it.key(), static_cast<Choice>(it.value()->currentData().toInt()));
    return out;
}

void CompareDialog::chooseAll(Choice choice) {
    for (auto it = combos_.constBegin(); it != combos_.constEnd(); ++it) {
        const int i = it.value()->findData(static_cast<int>(choice));
        if (i >= 0) it.value()->setCurrentIndex(i);
    }
}

void CompareDialog::choose(const QString& key, Choice choice) {
    if (auto* combo = combos_.value(key)) {
        const int i = combo->findData(static_cast<int>(choice));
        if (i >= 0) combo->setCurrentIndex(i);
    }
}

void CompareDialog::finish(Action action) {
    action_ = action;
    if (action == Action::Later) reject();
    else accept();
}

}  // namespace bld::sync
