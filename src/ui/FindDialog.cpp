#include "FindDialog.h"
#include "MapView.h"

#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/LayerText.h"
#include "../core/Map.h"
#include "../edit/EditCommands.h"
#include "../edit/TextCommands.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QUndoStack>
#include <QVBoxLayout>

namespace bld::ui {

FindDialog::FindDialog(MapView& view, QWidget* parent)
    : QDialog(parent), view_(view) {
    setWindowTitle(tr("Find && Replace"));
    resize(480, 240);
    setModal(false);

    auto* vbox = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    auto* scope = new QComboBox(this);
    scope->addItem(tr("Text content"));
    scope->addItem(tr("Part number"));
    form->addRow(tr("Search in:"), scope);

    auto* findE = new QLineEdit(this);
    form->addRow(tr("Find:"), findE);
    auto* replaceE = new QLineEdit(this);
    form->addRow(tr("Replace with:"), replaceE);

    auto* caseChk = new QCheckBox(tr("Match case"), this);
    form->addRow(caseChk);

    vbox->addLayout(form);

    auto* btnRow = new QHBoxLayout();
    auto* findBtn = new QPushButton(tr("Find Next"), this);
    auto* replaceBtn = new QPushButton(tr("Replace"), this);
    auto* replaceAllBtn = new QPushButton(tr("Replace All"), this);
    auto* closeBtn = new QPushButton(tr("Close"), this);
    btnRow->addWidget(findBtn); btnRow->addWidget(replaceBtn);
    btnRow->addWidget(replaceAllBtn); btnRow->addStretch(); btnRow->addWidget(closeBtn);
    vbox->addLayout(btnRow);

    auto* status = new QLabel(this);
    status->setWordWrap(true);
    vbox->addWidget(status);

    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);

    auto matchesText = [](const QString& needle, const QString& hay, Qt::CaseSensitivity cs) {
        if (needle.isEmpty()) return false;
        return hay.contains(needle, cs);
    };

    // The search pass. Extracted so both the explicit "Find Next" button
    // and the live-update debounced retrigger can share it.
    auto runSearch = [&view, scope, findE, caseChk, status, matchesText]{
        auto* map = view.currentMap();
        if (!map) return;
        const Qt::CaseSensitivity cs = caseChk->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
        const QString needle = findE->text();
        if (needle.isEmpty()) { view.deselectAll(); status->clear(); return; }

        view.deselectAll();
        int matches = 0;
        for (const auto& it : view.scene()->items()) {
            const int li = it->data(0).toInt();
            const QString guid = it->data(1).toString();
            const QString kind = it->data(2).toString();
            if (scope->currentIndex() == 0 && kind == QStringLiteral("text")) {
                auto* L = map->layers()[li].get();
                if (!L) continue;
                for (const auto& c : static_cast<core::LayerText&>(*L).textCells) {
                    if (c.guid == guid && matchesText(needle, c.text, cs)) {
                        it->setSelected(true); ++matches;
                        break;
                    }
                }
            } else if (scope->currentIndex() == 1 && kind == QStringLiteral("brick")) {
                auto* L = map->layers()[li].get();
                if (!L) continue;
                for (const auto& b : static_cast<core::LayerBrick&>(*L).bricks) {
                    if (b.guid == guid && matchesText(needle, b.partNumber, cs)) {
                        it->setSelected(true); ++matches;
                        break;
                    }
                }
            }
        }
        status->setText(QObject::tr("%1 match(es).").arg(matches));
    };
    connect(findBtn, &QPushButton::clicked, this, runSearch);

    // Live-update: debounce at 200 ms so typing doesn't redo the scan on
    // every keystroke but still feels responsive. Triggered from every
    // input that affects the result set (text, scope, case).
    auto* debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(200);
    connect(debounce, &QTimer::timeout, this, runSearch);
    auto schedule = [debounce]{ debounce->start(); };
    connect(findE, &QLineEdit::textChanged, this, [schedule](const QString&){ schedule(); });
    connect(scope, &QComboBox::currentIndexChanged, this, [schedule](int){ schedule(); });
    connect(caseChk, &QCheckBox::toggled, this, [schedule](bool){ schedule(); });

    // Re-run the search whenever the map mutates (e.g. user edits a text
    // cell while the dialog is open), so the match list stays accurate.
    connect(view.undoStack(), &QUndoStack::indexChanged, this,
            [schedule](int){ schedule(); });

    auto doReplace = [&view, scope, findE, replaceE, caseChk, status](bool all) {
        auto* map = view.currentMap();
        if (!map) return;
        const Qt::CaseSensitivity cs = caseChk->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
        const QString needle = findE->text();
        if (needle.isEmpty()) return;
        const QString with = replaceE->text();

        int replaced = 0;
        view.undoStack()->beginMacro(QObject::tr("Find & Replace"));
        if (scope->currentIndex() == 0) {
            for (int li = 0; li < static_cast<int>(map->layers().size()); ++li) {
                auto* L = map->layers()[li].get();
                if (!L || L->kind() != core::LayerKind::Text) continue;
                for (const auto& c : static_cast<core::LayerText&>(*L).textCells) {
                    if (!c.text.contains(needle, cs)) continue;
                    QString next = c.text; next.replace(needle, with, cs);
                    view.undoStack()->push(new edit::EditTextCellTextCommand(*map, li, c.guid, next));
                    ++replaced;
                    if (!all) break;
                }
                if (!all && replaced > 0) break;
            }
        } else {
            for (int li = 0; li < static_cast<int>(map->layers().size()); ++li) {
                auto* L = map->layers()[li].get();
                if (!L || L->kind() != core::LayerKind::Brick) continue;
                for (const auto& b : static_cast<core::LayerBrick&>(*L).bricks) {
                    if (!b.partNumber.contains(needle, cs)) continue;
                    QString next = b.partNumber; next.replace(needle, with, cs);
                    edit::EditBrickCommand::State before{
                        b.partNumber, b.displayArea.topLeft(),
                        b.orientation, b.altitude, b.activeConnectionPointIndex };
                    edit::EditBrickCommand::State after = before;
                    after.partNumber = next;
                    view.undoStack()->push(new edit::EditBrickCommand(
                        *map, edit::BrickRef{ li, b.guid }, before, after));
                    ++replaced;
                    if (!all) break;
                }
                if (!all && replaced > 0) break;
            }
        }
        view.undoStack()->endMacro();
        status->setText(QObject::tr("Replaced %1 occurrence(s).").arg(replaced));
    };
    connect(replaceBtn, &QPushButton::clicked, this, [doReplace]{ doReplace(false); });
    connect(replaceAllBtn, &QPushButton::clicked, this, [doReplace]{ doReplace(true); });
}

}
