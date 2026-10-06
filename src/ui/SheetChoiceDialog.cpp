#include "SheetChoiceDialog.h"

#include "../core/Layer.h"
#include "../core/Map.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace bld::ui {

namespace {
QString quoteList(const std::vector<edit::UnmatchedSheet>& sheets) {
    QStringList q;
    for (const auto& s : sheets) q << QStringLiteral("“%1”").arg(s.name);
    if (q.size() <= 1) return q.join(QString());
    const QString last = q.takeLast();
    return SheetChoiceDialog::tr("%1 or %2").arg(q.join(QStringLiteral(", ")), last);
}
}  // namespace

SheetChoiceDialog::SheetChoiceDialog(const core::Map& map, const QString& moduleName, int moduleSheets,
                                     std::vector<edit::UnmatchedSheet> unmatched, QWidget* parent)
    : QDialog(parent), unmatched_(std::move(unmatched)) {
    setObjectName(QStringLiteral("sheetChoiceDialog"));
    setWindowTitle(tr("Where should these go?"));
    setModal(true);
    auto* col = new QVBoxLayout(this);
    col->setSpacing(10);
    auto* title = new QLabel(tr("Where should these go?"), this);
    QFont f = title->font();
    f.setPointSizeF(f.pointSizeF() * 1.25);
    f.setBold(true);
    title->setFont(f);
    col->addWidget(title);

    body_ = new QLabel(moduleSheets == 1
                           ? tr("“%1” uses a sheet. Your layout doesn’t have %2.").arg(moduleName, quoteList(unmatched_))
                           : tr("“%1” uses %2 sheets. Your layout doesn’t have %3.")
                                 .arg(moduleName)
                                 .arg(moduleSheets)
                                 .arg(quoteList(unmatched_)),
                       this);
    body_->setObjectName(QStringLiteral("sheetChoiceBody"));
    body_->setWordWrap(true);
    col->addWidget(body_);

    // The layout's parts sheets, top first, as the Sheets panel lists them.
    const int picked = edit::pickedPartsSheet(map);
    auto* form = new QFormLayout();
    for (const auto& u : unmatched_) {
        auto* box = new QComboBox(this);
        box->setObjectName(QStringLiteral("sheetChoice"));
        box->setAccessibleName(tr("Where the parts on %1 go").arg(u.name));
        box->setMinimumHeight(32);
        for (int i = static_cast<int>(map.layers().size()) - 1; i >= 0; --i) {
            const auto* L = map.layers()[i].get();
            if (!L || L->kind() != core::LayerKind::Brick) continue;
            const QString name = L->name.isEmpty() ? tr("untitled") : L->name;
            box->addItem(i == picked ? tr("%1 (picked sheet)").arg(name) : name, L->guid);
            if (i == picked) box->setCurrentIndex(box->count() - 1);
        }
        box->addItem(tr("A new sheet named “%1”").arg(u.name), QString());
        if (picked < 0) box->setCurrentIndex(box->count() - 1);
        const QString parts = u.parts == 1 ? tr("1 part") : tr("%1 parts").arg(u.parts);
        auto* label = new QLabel(QStringLiteral("<b>%1</b> (%2)").arg(u.name.toHtmlEscaped(), parts), this);
        label->setTextFormat(Qt::RichText);
        label->setBuddy(box);
        form->addRow(label, box);
        choices_.push_back(box);
    }
    col->addLayout(form);

    auto* row = new QHBoxLayout();
    row->addStretch(1);
    cancel_ = new QPushButton(tr("Cancel"), this);
    cancel_->setObjectName(QStringLiteral("sheetChoiceCancel"));
    place_ = new QPushButton(tr("Place"), this);
    place_->setObjectName(QStringLiteral("sheetChoicePlace"));
    place_->setProperty("accent", true);
    place_->setDefault(true);
    for (QPushButton* b : { cancel_, place_ }) b->setMinimumHeight(36);  // easy to hit with a finger
    row->addWidget(cancel_);
    row->addWidget(place_);
    col->addLayout(row);
    connect(cancel_, &QPushButton::clicked, this, &QDialog::reject);
    connect(place_, &QPushButton::clicked, this, &QDialog::accept);
    setMinimumWidth(400);
    place_->setFocus(Qt::OtherFocusReason);
}

QComboBox* SheetChoiceDialog::choiceFor(const QString& sheetName) const {
    for (std::size_t i = 0; i < unmatched_.size(); ++i)
        if (edit::sheetKey(unmatched_[i].name) == edit::sheetKey(sheetName)) return choices_[i];
    return nullptr;
}

QString SheetChoiceDialog::targetFor(const QString& sheetName) const {
    const QComboBox* box = choiceFor(sheetName);
    return box ? box->currentData().toString() : QString();
}

QString SheetChoiceDialog::bodyText() const { return body_->text(); }

bool SheetChoiceDialog::choose(QWidget* parent, const core::Map& map, const QString& moduleName,
                               std::vector<edit::ImportBbmAsModuleCommand::LayerBatch>& batches) {
    auto unmatched = edit::unmatchedSheets(map, batches);
    if (unmatched.empty()) return true;
    SheetChoiceDialog d(map, moduleName.isEmpty() ? tr("Module") : moduleName, edit::moduleSheetCount(batches),
                        unmatched, parent);
    if (d.exec() != QDialog::Accepted) return false;
    for (const auto& u : unmatched) edit::sendSheetTo(batches, u.name, d.targetFor(u.name));
    return true;
}

}  // namespace bld::ui
