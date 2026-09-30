#include "ShortcutsDialog.h"

#include "ui/theme/Tokens.h"

#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QRegularExpression>
#include <QScrollArea>
#include <QVBoxLayout>

namespace bld::ui::help {

namespace {

QString plain(QString text) {
    text.remove(QRegularExpression(QStringLiteral("&(?!&)")));
    text.replace(QStringLiteral("&&"), QStringLiteral("&"));
    text.remove(QStringLiteral("..."));
    text.remove(QChar(0x2026));  // …
    return text.trimmed();
}

void collect(const QMenu* menu, QList<Shortcut>& out) {
    for (QAction* a : menu->actions()) {
        if (a->menu()) {
            collect(a->menu(), out);
            continue;
        }
        if (a->isSeparator() || a->shortcut().isEmpty()) continue;
        QStringList keys;
        for (const QKeySequence& k : a->shortcuts()) keys << k.toString(QKeySequence::NativeText);
        out.append({ keys.join(QStringLiteral(" / ")), plain(a->text()) });
    }
}

}  // namespace

QList<ShortcutGroup> collectShortcuts(const QMenuBar* menuBar) {
    QList<ShortcutGroup> groups;
    if (menuBar) {
        for (QAction* top : menuBar->actions()) {
            if (!top->menu()) continue;
            ShortcutGroup g{ plain(top->text()), {} };
            collect(top->menu(), g.items);
            if (!g.items.isEmpty()) groups.append(g);
        }
    }
    // MapView::keyPressEvent's own keys (no menu item has them).
    groups.append({ ShortcutsDialog::tr("On the map"),
                    {
                        { ShortcutsDialog::tr("R"), ShortcutsDialog::tr("Turn the selected pieces") },
                        { ShortcutsDialog::tr("Shift+R"), ShortcutsDialog::tr("Turn them the other way") },
                        { ShortcutsDialog::tr("Arrow keys"), ShortcutsDialog::tr("Nudge the selected pieces") },
                        { ShortcutsDialog::tr("Delete"), ShortcutsDialog::tr("Remove the selected pieces") },
                    } });
    return groups;
}

ShortcutsDialog::ShortcutsDialog(const QList<ShortcutGroup>& groups, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("ShortcutsDialog"));
    setWindowTitle(tr("Keyboard shortcuts"));
    resize(520, 640);
    auto* outer = new QVBoxLayout(this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget(scroll);
    scroll->setWidget(page);
    auto* col = new QVBoxLayout(page);
    col->setSpacing(theme::kSpacing * 2);
    const QPalette& pal = palette();
    const QString chip = QStringLiteral("QLabel { background: %1; border: 1px solid %2; border-radius: 6px;"
                                        " padding: 2px 6px; font-weight: 600; }")
                             .arg(pal.color(QPalette::AlternateBase).name(), pal.color(QPalette::Mid).name());
    for (const ShortcutGroup& g : groups) {
        auto* heading = new QLabel(g.title.toUpper(), page);
        heading->setObjectName(QStringLiteral("ShortcutGroup"));
        QFont f = heading->font();
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() * 0.85);
        heading->setFont(f);
        heading->setStyleSheet(QStringLiteral("color: %1;").arg(pal.color(QPalette::PlaceholderText).name()));
        col->addWidget(heading);
        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(theme::kSpacing * 2);
        grid->setVerticalSpacing(theme::kSpacing / 2);
        int row = 0;
        for (const Shortcut& s : g.items) {
            auto* keys = new QLabel(s.keys, page);
            keys->setStyleSheet(chip);
            grid->addWidget(keys, row, 0, Qt::AlignLeft | Qt::AlignVCenter);
            grid->addWidget(new QLabel(s.does, page), row, 1);
            ++row;
        }
        grid->setColumnStretch(1, 1);
        col->addLayout(grid);
    }
    col->addStretch(1);
    outer->addWidget(scroll, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);
}

}  // namespace bld::ui::help
