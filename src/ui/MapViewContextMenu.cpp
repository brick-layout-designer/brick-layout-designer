// Right-click context menu on the map canvas. Action set is context-aware:
//
//   * Non-empty selection → Properties / Edit Text / rotate / bring-front /
//     send-back / group / ungroup / select-path / cut-copy-duplicate / delete.
//   * Empty area         → paste (if clipboard non-empty) / add text here.
//   * Selected ruler + right-click on a brick → "Attach endpoint N to this
//     brick" entries (linear rulers get two endpoints, circular gets centre).
//
// Always tails with Undo/Redo. Split out of MapView.cpp so the main TU
// stays focused on drag / paint / selection plumbing rather than menu wiring.

#include "MapView.h"
#include "../core/ModuleEdit.h"
#include "../core/ModuleLook.h"
#include "../edit/ModuleCommands.h"

#include "../core/Layer.h"
#include "../core/LayerRuler.h"
#include "../core/Map.h"
#include "../edit/RulerCommands.h"
#include "EditDialogs.h"
#include "MapViewInternal.h"
#include "TouchActionBar.h"

#include <QToolButton>

#include <QAction>
#include <QContextMenuEvent>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QSet>
#include <QUndoStack>

namespace bld::ui {

using detail::kBrickDataLayerIndex;
using detail::kBrickDataGuid;
using detail::isBrickItem;
using detail::isTextItem;
using detail::isRulerItem;

void MapView::contextMenuEvent(QContextMenuEvent* e) {
    // Snapshot the pre-selection state so the "attach ruler to brick" flow
    // can detect a selected-ruler + clicked-brick combination BEFORE the
    // auto-reselect below overwrites things.
    QString  heldRulerGuid;
    int      heldRulerLayer = -1;
    bool     heldRulerIsCircular = false;
    {
        QSet<QString> rulerGuids;
        for (QGraphicsItem* it : scene()->selectedItems()) {
            if (!isRulerItem(it)) continue;
            const QString g = it->data(kBrickDataGuid).toString();
            if (!g.isEmpty()) rulerGuids.insert(g);
        }
        if (rulerGuids.size() == 1) {
            heldRulerGuid = *rulerGuids.begin();
            for (QGraphicsItem* it : scene()->selectedItems()) {
                if (!isRulerItem(it)) continue;
                if (it->data(kBrickDataGuid).toString() != heldRulerGuid) continue;
                heldRulerLayer = it->data(kBrickDataLayerIndex).toInt();
                break;
            }
            if (heldRulerLayer >= 0 && map_
                && heldRulerLayer < static_cast<int>(map_->layers().size())) {
                auto* L = map_->layers()[heldRulerLayer].get();
                if (L && L->kind() == core::LayerKind::Ruler) {
                    const auto& RL = static_cast<const core::LayerRuler&>(*L);
                    for (const auto& any : RL.rulers) {
                        const QString& g = (any.kind == core::RulerKind::Linear)
                            ? any.linear.guid : any.circular.guid;
                        if (g == heldRulerGuid) {
                            heldRulerIsCircular = (any.kind == core::RulerKind::Circular);
                            break;
                        }
                    }
                }
            }
        }
    }

    // Clicked-on brick (pre-reselect). Lets us offer "attach ruler here"
    // against the brick the user actually right-clicked on.
    QString clickedBrickGuid;
    {
        const QPointF scenePos = mapToScene(e->pos());
        for (QGraphicsItem* it : scene()->items(scenePos)) {
            if (!isBrickItem(it)) continue;
            clickedBrickGuid = it->data(kBrickDataGuid).toString();
            break;
        }
    }

    // If the right-click happens on an item that is not part of the existing
    // selection, clear the selection and select that one item so the menu's
    // actions act on what the user clicked.
    if (auto* under = itemAt(e->pos())) {
        // A part's drawing may have pieces of its own (the red cross of a
        // part missing from the library): act on the part.
        while (under->parentItem() && !(under->flags() & QGraphicsItem::ItemIsSelectable))
            under = under->parentItem();
        if (!under->isSelected()) {
            scene()->clearSelection();
            under->setSelected(true);
        }
    }

    QMenu menu(this);
    const bool wholeModule = addModuleMenu(menu, clickedBrickGuid);
    const auto sel = scene()->selectedItems();
    const bool hasSel = !sel.isEmpty();
    int brickCount = 0, textCount = 0;
    for (QGraphicsItem* it : sel) {
        if (isBrickItem(it)) ++brickCount;
        else if (isTextItem(it)) ++textCount;
    }
    const bool onlyText  = textCount > 0 && brickCount == 0;
    const bool onlyBrick = brickCount > 0 && textCount == 0;
    const bool singleText = onlyText && sel.size() == 1;

    if (hasSel) {
        // Properties... opens the type-appropriate dialog for a single item.
        if (sel.size() == 1) {
            QGraphicsItem* only = sel.front();
            const int li = only->data(kBrickDataLayerIndex).toInt();
            const QString guid = only->data(kBrickDataGuid).toString();
            if (isBrickItem(only)) {
                auto* prop = menu.addAction(tr("Properties..."));
                connect(prop, &QAction::triggered, [this, li, guid]{
                    if (editBrickDialog(this, *map_, li, guid, parts_, *undoStack_))
                        rebuildScene();
                });
            } else if (isRulerItem(only)) {
                auto* prop = menu.addAction(tr("Properties..."));
                connect(prop, &QAction::triggered, [this, li, guid]{
                    if (editRulerDialog(this, *map_, li, guid, *undoStack_))
                        rebuildScene();
                });
            } else if (isTextItem(only)) {
                auto* prop = menu.addAction(tr("Properties..."));
                connect(prop, &QAction::triggered, [this, li, guid]{
                    if (editTextDialog(this, *map_, li, guid, *undoStack_))
                        rebuildScene();
                });
            }
        }
        if (singleText) {
            auto* edit = menu.addAction(tr("Edit Text..."));
            connect(edit, &QAction::triggered, [this]{ editSelectedTextContent(); });
        }

        auto* ccw = menu.addAction(tr("Rotate CCW"));
        connect(ccw, &QAction::triggered,
                [this]{ rotateSelected(static_cast<float>(-rotationStepDegrees_)); });
        auto* cw = menu.addAction(tr("Rotate CW"));
        connect(cw, &QAction::triggered,
                [this]{ rotateSelected(static_cast<float>(rotationStepDegrees_)); });
        menu.addSeparator();

        if (onlyBrick) {
            auto* bringFront = menu.addAction(tr("Bring to Front"));
            connect(bringFront, &QAction::triggered, [this]{ bringSelectionToFront(); });
            auto* sendBack = menu.addAction(tr("Send to Back"));
            connect(sendBack, &QAction::triggered, [this]{ sendSelectionToBack(); });
            // To another part sheet, on top of what's there.
            {
                QSet<int> from;
                for (QGraphicsItem* it : sel)
                    if (isBrickItem(it)) from.insert(it->data(kBrickDataLayerIndex).toInt());
                auto* sheets = menu.addMenu(tr("Move to Sheet"));
                sheets->setObjectName(QStringLiteral("moveToSheet"));
                const auto& layers = map_->layers();
                for (int i = static_cast<int>(layers.size()) - 1; i >= 0; --i) {
                    if (layers[i]->kind() != core::LayerKind::Brick) continue;
                    if (from.size() == 1 && from.contains(i)) continue;
                    auto* a = sheets->addAction(layers[i]->name);
                    connect(a, &QAction::triggered, [this, i]{ moveSelectionToSheet(i); });
                }
                if (!sheets->isEmpty()) sheets->addSeparator();
                auto* fresh = sheets->addAction(tr("New Sheet"));
                connect(fresh, &QAction::triggered, [this]{ moveSelectionToSheet(-1); });
            }
            menu.addSeparator();

            if (sel.size() >= 2) {
                auto* grp = menu.addAction(tr("Group"));
                connect(grp, &QAction::triggered, [this]{ groupSelection(); });
            }
            if (const auto state = ungroupState(); state != UngroupState::Nothing) {
                auto* ungrp = menu.addAction(tr("Ungroup"));
                connect(ungrp, &QAction::triggered, [this]{ ungroupSelection(); });
                if (state == UngroupState::AlwaysWhole) {
                    ungrp->setEnabled(false);
                    ungrp->setText(tr("Ungroup (this set is always used whole)"));
                }
            }
            auto* selPath = menu.addAction(tr("Select Connected"));
            selPath->setToolTip(tr("Extend selection to every brick connected to current selection"));
            connect(selPath, &QAction::triggered, [this]{ selectPath(); });
            menu.addSeparator();
            // Already one whole module: its own entries are at the top.
            if (!wholeModule || !editingModuleId_.isEmpty()) {
                auto* make = menu.addAction(tr("Make a Module..."));
                make->setObjectName(QStringLiteral("ctxMakeModule"));
                connect(make, &QAction::triggered, this, &MapView::makeModuleRequested);
                menu.addSeparator();
            }

            auto* cut = menu.addAction(tr("Cut"));
            connect(cut, &QAction::triggered, [this]{ cutSelection(); });
            auto* copy = menu.addAction(tr("Copy"));
            connect(copy, &QAction::triggered, [this]{ copySelection(); });
            auto* dup = menu.addAction(tr("Duplicate"));
            connect(dup, &QAction::triggered, [this]{ duplicateSelection(); });
            menu.addSeparator();
        }

        auto* del = menu.addAction(tr("Delete"));
        del->setShortcut(Qt::Key_Delete);
        connect(del, &QAction::triggered, [this]{ deleteSelected(); });
        menu.addSeparator();
    } else {
        // Empty-area menu: paste and quick-add actions tied to the
        // cursor position.
        if (!clipboard_.empty()) {
            auto* paste = menu.addAction(tr("Paste"));
            connect(paste, &QAction::triggered, [this]{ pasteClipboard(); });
            menu.addSeparator();
        }
        const QPointF scenePos = mapToScene(e->pos());
        auto* addText = menu.addAction(tr("Add Text Here..."));
        connect(addText, &QAction::triggered, [this, scenePos]{
            if (!map_) return;
            bool ok = false;
            const QString text = QInputDialog::getText(
                this, tr("Add text"), tr("Label text:"),
                QLineEdit::Normal, {}, &ok);
            if (!ok || text.isEmpty()) return;
            addTextAtScenePos(text, scenePos);
        });
        menu.addSeparator();
    }

    // Attach-ruler flow: when there's exactly one ruler held in the
    // pre-reselect selection and the user right-clicked on a brick,
    // offer "Attach endpoint N to this brick" entries.
    if (!heldRulerGuid.isEmpty() && !clickedBrickGuid.isEmpty()
        && heldRulerLayer >= 0) {
        const int layer = heldRulerLayer;
        const QString rulerGuid = heldRulerGuid;
        const QString brickGuid = clickedBrickGuid;
        if (heldRulerIsCircular) {
            auto* a = menu.addAction(tr("Attach Ruler Centre to This Brick"));
            connect(a, &QAction::triggered, [this, layer, rulerGuid, brickGuid]{
                undoStack_->push(new edit::AttachRulerCommand(
                    *map_, layer, rulerGuid, 0, brickGuid));
            });
        } else {
            auto* a1 = menu.addAction(tr("Attach Ruler Endpoint &1 to This Brick"));
            connect(a1, &QAction::triggered, [this, layer, rulerGuid, brickGuid]{
                undoStack_->push(new edit::AttachRulerCommand(
                    *map_, layer, rulerGuid, 0, brickGuid));
            });
            auto* a2 = menu.addAction(tr("Attach Ruler Endpoint &2 to This Brick"));
            connect(a2, &QAction::triggered, [this, layer, rulerGuid, brickGuid]{
                undoStack_->push(new edit::AttachRulerCommand(
                    *map_, layer, rulerGuid, 1, brickGuid));
            });
        }
        menu.addSeparator();
    }

    auto* undo = menu.addAction(tr("Undo"));
    undo->setEnabled(undoStack_->canUndo());
    connect(undo, &QAction::triggered, undoStack_.get(), &QUndoStack::undo);
    auto* redo = menu.addAction(tr("Redo"));
    redo->setEnabled(undoStack_->canRedo());
    connect(redo, &QAction::triggered, undoStack_.get(), &QUndoStack::redo);

    menu.exec(e->globalPos());
    e->accept();
}

bool MapView::addModuleMenu(QMenu& menu, const QString& clickedBrickGuid) {
    if (!map_) return false;
    if (!editingModuleId_.isEmpty()) {
        menu.addAction(tr("Done editing module"), this, [this] { setEditingModule({}); });
        menu.addSeparator();
        return false;
    }
    // The module of the part right-clicked, or a whole module that is the selection.
    const auto byPart = core::moduleByPart(map_->sidecar.modules);
    const core::Module* mod = clickedBrickGuid.isEmpty() ? nullptr : byPart.value(clickedBrickGuid);
    if (!mod) {
        QSet<QString> sel;
        for (QGraphicsItem* it : scene()->selectedItems())
            if (isBrickItem(it)) sel.insert(it->data(kBrickDataGuid).toString());
        if (!sel.isEmpty()) {
            const core::Module* first = byPart.value(*sel.begin());
            if (first && first->memberIds == sel) mod = first;
        }
    }
    if (!mod) return false;
    const QString id = mod->id;
    menu.addSection(mod->name.isEmpty() ? tr("Module") : mod->name);
    menu.addAction(tr("Edit module"), this, [this, id] { setEditingModule(id); });
    menu.addAction(mod->pinned ? tr("Unpin") : tr("Pin in place"), this, [this, id] {
        if (const core::Module* m = map_ ? core::findModule(map_->sidecar.modules, id) : nullptr)
            undoStack_->push(new edit::UpdateModuleCommand(*map_, core::withPinned(*m, !m->pinned),
                                                           m->pinned ? tr("Unpin module") : tr("Pin module in place")));
    });
    menu.addAction(mod->showName ? tr("Hide name") : tr("Show name"), this, [this, id] {
        if (const core::Module* m = map_ ? core::findModule(map_->sidecar.modules, id) : nullptr)
            undoStack_->push(new edit::UpdateModuleCommand(*map_, core::withShowName(*m, !m->showName),
                                                           m->showName ? tr("Hide module name") : tr("Show module name")));
    });
    menu.addAction(tr("Colors..."), this, [this, id] { emit moduleLookRequested(id); });
    if (moduleLibraryInfo_) {
        menu.addSeparator();
        for (const auto& e : moduleLibraryInfo_(*mod).entries) {
            QAction* a = menu.addAction(e.label);
            a->setObjectName(QStringLiteral("moduleLibrary_") + e.action);
            a->setEnabled(e.enabled);
            a->setToolTip(e.tip);
            const QString action = e.action;
            connect(a, &QAction::triggered, this, [this, id, action] { emit moduleLibraryActionRequested(id, action); });
        }
    }
    menu.addSeparator();
    return true;
}

void MapView::touchModule() {
    QMenu menu(this);
    // A whole module picked: its menu; otherwise make one of the picked parts.
    if (!addModuleMenu(menu, QString())) {
        emit makeModuleRequested();
        return;
    }
    QPoint at = mapToGlobal(viewport()->rect().center());
    if (touchBar_)
        if (QWidget* b = touchBar_->button(QStringLiteral("touchModule"))) at = b->mapToGlobal(QPoint(0, 0));
    menu.exec(at);
}

}  // namespace bld::ui
