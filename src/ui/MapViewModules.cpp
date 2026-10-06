// Edit module and Pin in place on the map (the web's moduleEdit.ts,
// BrickLayer.tsx and ModuleEditDim.tsx): modules are picked whole; one
// module can be opened part by part with the rest dimmed and out of reach;
// a pinned module doesn't move as a whole.

#include "MapView.h"
#include "MapViewInternal.h"
#include "ModuleEditBar.h"
#include "../edit/ModuleSheets.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../core/ModuleEdit.h"
#include "../edit/ModuleCommands.h"
#include "../rendering/ModuleLabels.h"
#include "../rendering/SceneBuilder.h"

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QUndoStack>

namespace bld::ui {

using detail::isBrickItem;
using detail::isLabelItem;
using detail::isRulerItem;
using detail::isTextItem;
using detail::isVenueItem;
using detail::kBrickDataGuid;

namespace {
QSet<QString> selectedBricks(QGraphicsScene* scene) {
    QSet<QString> out;
    for (QGraphicsItem* it : scene->selectedItems())
        if (isBrickItem(it)) out.insert(it->data(kBrickDataGuid).toString());
    return out;
}
}  // namespace

QGraphicsItem* MapView::itemUnder(QPoint viewPos) const {
    // Module frames and names lie over their parts but take no clicks.
    for (QGraphicsItem* it : items(viewPos))
        if (!it->data(rendering::kModuleAnnotationRole).isValid()) return it;
    return nullptr;
}

void MapView::showStatus(const QString& text, int ms) {
    if (auto* mw = window())
        if (auto* sb = mw->findChild<QStatusBar*>()) sb->showMessage(text, ms);
}

void MapView::setEditingModule(const QString& moduleId) {
    if (moduleId == editingModuleId_) return;
    if (!moduleId.isEmpty() && (!map_ || !core::findModule(map_->sidecar.modules, moduleId))) return;
    editingModuleId_ = moduleId;
    if (scene()) scene()->clearSelection();
    lastBrickSelection_.clear();
    if (map_) rebuildScene();  // applies the flags and the dimming
    refreshModuleEditBar();
    viewport()->update();
    emit editingModuleChanged(moduleId);
}

void MapView::setPeersEditing(const QHash<QString, QStringList>& who) {
    if (who == peersEditing_) return;
    peersEditing_ = who;
    refreshModuleEditBar();
}

void MapView::refreshModuleEditBar() {
    const core::Module* mod =
        map_ && !editingModuleId_.isEmpty() ? core::findModule(map_->sidecar.modules, editingModuleId_) : nullptr;
    if (!mod) {
        if (editBar_) editBar_->hide();
        return;
    }
    if (!editBar_) {
        editBar_ = new ModuleEditBar(this);
        connect(editBar_, &ModuleEditBar::done, this, [this] { setEditingModule({}); });
        connect(editBar_, &ModuleEditBar::showHiddenSheets, this, &MapView::showEditedModuleSheets);
    }
    editBar_->setModuleName(mod->name.isEmpty() ? tr("(module)") : mod->name);
    editBar_->setOthers(core::hereTooText(peersEditing_.value(editingModuleId_)));
    {
        // On several sheets: where new parts go. Some hidden: a Show button.
        const auto uses = edit::moduleSheetsUsed(*map_, mod->memberIds);
        int hidden = 0;
        for (const auto& u : uses) hidden += u.visible ? 0 : 1;
        QStringList lines;
        if (uses.size() > 1) {
            const int picked = edit::pickedPartsSheet(*map_);
            QString line = tr("This module uses %1 sheets.").arg(uses.size());
            if (picked >= 0) {
                const QString name = map_->layers()[picked]->name;
                line += QLatin1Char(' ') + tr("New parts go on <b>%1</b> (the picked sheet).")
                                               .arg((name.isEmpty() ? tr("untitled") : name).toHtmlEscaped());
            }
            lines << line;
        }
        if (hidden > 0)
            lines << (hidden == static_cast<int>(uses.size())
                          ? (uses.size() == 1 ? tr("Its sheet is hidden.") : tr("Its sheets are hidden."))
                          : (hidden == 1 ? tr("1 of its sheets is hidden.") : tr("%1 of its sheets are hidden.").arg(hidden)));
        editBar_->setSheetsHint(lines.join(QLatin1Char(' ')),
                                hidden == 0 ? QString() : hidden == 1 ? tr("Show it") : tr("Show them"));
    }
    editBar_->place(viewport()->geometry());
    editBar_->show();
    editBar_->raise();
}

void MapView::showEditedModuleSheets() {
    const core::Module* mod =
        map_ && !editingModuleId_.isEmpty() ? core::findModule(map_->sidecar.modules, editingModuleId_) : nullptr;
    if (!mod) return;
    for (const auto& u : edit::moduleSheetsUsed(*map_, mod->memberIds)) {
        if (u.visible) continue;
        map_->layers()[u.index]->visible = true;
        if (builder_) builder_->setLayerVisible(u.index, true);
    }
    if (builder_) builder_->refreshModuleLabels(*map_);
    refreshModuleEditBar();
    emit layersChanged();
}

std::optional<QRectF> MapView::editedModuleFrameStuds() const {
    if (!map_ || editingModuleId_.isEmpty()) return std::nullopt;
    const core::Module* mod = core::findModule(map_->sidecar.modules, editingModuleId_);
    if (!mod) return std::nullopt;
    QRectF box;
    for (const auto& L : map_->layers()) {
        if (!L || L->kind() != core::LayerKind::Brick || !L->visible) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
            if (mod->memberIds.contains(b.guid)) box = box.united(b.displayArea);
    }
    if (box.isEmpty()) return std::nullopt;
    const double p = rendering::kModuleEditPadStuds;
    return box.adjusted(-p, -p, p, p);
}

void MapView::applyModuleState() {
    if (!map_ || !scene()) return;
    // The module being edited is gone (deleted here or by someone else).
    if (!editingModuleId_.isEmpty() && !core::findModule(map_->sidecar.modules, editingModuleId_)) {
        editingModuleId_.clear();
        refreshModuleEditBar();
        QTimer::singleShot(0, this, [this] { emit editingModuleChanged({}); });
    }
    const auto& mods = map_->sidecar.modules;
    const bool editing = !editingModuleId_.isEmpty();
    const bool anyPinned = std::any_of(mods.begin(), mods.end(), [](const core::Module& m) { return m.pinned; });
    if (!editing && !anyPinned) return;
    for (QGraphicsItem* it : scene()->items()) {
        if (isBrickItem(it)) {
            const QString g = it->data(kBrickDataGuid).toString();
            if (core::outsideEdit(g, mods, editingModuleId_)) {
                it->setFlag(QGraphicsItem::ItemIsSelectable, false);
                it->setFlag(QGraphicsItem::ItemIsMovable, false);
                it->setOpacity(it->opacity() * 0.5);
            } else if (!core::canDragPart(g, mods, editingModuleId_)) {
                it->setFlag(QGraphicsItem::ItemIsMovable, false);
            }
        } else if (editing && (isTextItem(it) || isRulerItem(it) || isLabelItem(it) || isVenueItem(it))) {
            it->setFlag(QGraphicsItem::ItemIsSelectable, false);
            it->setFlag(QGraphicsItem::ItemIsMovable, false);
        }
    }
}

void MapView::shapeModuleSelection() {
    if (!map_ || !scene()) return;
    const auto& mods = map_->sidecar.modules;
    const QSet<QString> sel = selectedBricks(scene());
    if (mods.empty()) {
        lastBrickSelection_ = sel;
        return;
    }
    QSet<QString> want = sel;
    if (!editingModuleId_.isEmpty()) {
        want = core::shapeSelection(sel, mods, editingModuleId_);
    } else {
        for (const auto& m : mods) {
            if (m.memberIds.isEmpty() || !m.memberIds.intersects(sel) || sel.contains(m.memberIds)) continue;
            // Part of a module picked: a part was just taken off a whole
            // module (the module goes), or one was added (it all comes).
            if (lastBrickSelection_.contains(m.memberIds)) want.subtract(m.memberIds);
            else want.unite(m.memberIds);
        }
    }
    if (want != sel) {
        for (QGraphicsItem* it : scene()->items()) {
            if (!isBrickItem(it)) continue;
            const bool on = want.contains(it->data(kBrickDataGuid).toString());
            if (it->isSelected() != on && (!on || (it->flags() & QGraphicsItem::ItemIsSelectable))) it->setSelected(on);
        }
    }
    lastBrickSelection_ = want;
}

bool MapView::selectionMayMove() {
    if (!map_ || !scene()) return true;
    const core::Module* pinned = core::pinnedAmong(selectedBricks(scene()), map_->sidecar.modules, editingModuleId_);
    if (!pinned) return true;
    showStatus(tr("“%1” is pinned in place. Unpin it from its ⋯ menu to move it, or use Edit module to change its parts.")
                   .arg(pinned->name.isEmpty() ? tr("This module") : pinned->name),
               6000);
    return false;
}

void MapView::absorbIntoEditedModule(const QSet<QString>& guids) {
    if (!map_ || editingModuleId_.isEmpty() || guids.isEmpty()) return;
    // A set or library module placed meanwhile arrives as a module of its
    // own: it melts into the edited one (no module inside a module).
    QStringList dissolve;
    for (const auto& m : map_->sidecar.modules)
        if (m.id != editingModuleId_ && !m.memberIds.isEmpty() && guids.contains(m.memberIds)) dissolve << m.id;
    for (const QString& id : dissolve) undoStack_->push(new edit::DeleteModuleCommand(*map_, id));
    const core::Module* mod = core::findModule(map_->sidecar.modules, editingModuleId_);
    if (!mod) return;
    undoStack_->push(new edit::SetModuleMembersCommand(*map_, editingModuleId_, mod->memberIds + guids, tr("Add to module")));
}

void MapView::checkPartsLeftModule() {
    const auto outline = editOutlineAtPress_;
    const auto areas = editAreasAtPress_;
    editOutlineAtPress_.reset();
    editAreasAtPress_.clear();
    if (!outline || !map_ || editingModuleId_.isEmpty()) return;
    const core::Module* mod = core::findModule(map_->sidecar.modules, editingModuleId_);
    if (!mod) return;
    // Parts that moved, now clear of the outline the module had.
    QSet<QString> left;
    for (const auto& L : map_->layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks) {
            const auto was = areas.constFind(b.guid);
            if (was == areas.constEnd() || *was == b.displayArea || !mod->memberIds.contains(b.guid)) continue;
            if (core::outsideOutline(b.displayArea, *outline)) left.insert(b.guid);
        }
    }
    if (left.isEmpty()) return;
    const QString id = editingModuleId_;
    const QString name = mod->name.isEmpty() ? tr("the module") : mod->name;
    // After the release has finished (the question runs its own loop).
    QTimer::singleShot(0, this, [this, id, name, left] {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(tr("Take out of the module"));
        box.setText(left.size() == 1 ? tr("Take this part out of “%1”?").arg(name)
                                     : tr("Take these %1 parts out of “%2”?").arg(left.size()).arg(name));
        box.setInformativeText(left.size() == 1
                                   ? tr("It stays on the map, on its own. Cancel keeps it in the module, which grows to take it in.")
                                   : tr("They stay on the map, on their own. Cancel keeps them in the module, which grows to take them in."));
        auto* take = box.addButton(tr("Take out"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != take || !map_) return;
        const core::Module* m = core::findModule(map_->sidecar.modules, id);
        if (!m) return;
        undoStack_->push(new edit::SetModuleMembersCommand(*map_, id, m->memberIds - left, tr("Take out of module")));
    });
}

void MapView::paintModuleEdit(QPainter* painter) {
    const auto frame = editedModuleFrameStuds();
    if (!frame) return;
    const double k = rendering::SceneBuilder::kPixelsPerStud;
    const QRectF r(frame->x() * k, frame->y() * k, frame->width() * k, frame->height() * k);
    const QRectF shown = mapToScene(viewport()->rect()).boundingRect().adjusted(-16, -16, 16, 16).united(r);
    QPainterPath outside;
    outside.setFillRule(Qt::OddEvenFill);
    outside.addRect(shown);
    outside.addRect(r);
    painter->save();
    painter->fillPath(outside, rendering::kModuleEditDim);
    QPen pen(rendering::kModuleEditOutline);
    pen.setWidthF(rendering::kModuleEditOutlineWidth);
    pen.setCosmetic(true);
    // Qt's dash is in pen widths; the web's is in screen px.
    pen.setDashPattern({ rendering::kModuleEditDash[0] / rendering::kModuleEditOutlineWidth,
                         rendering::kModuleEditDash[1] / rendering::kModuleEditOutlineWidth });
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(r);
    painter->restore();
}

}  // namespace bld::ui
