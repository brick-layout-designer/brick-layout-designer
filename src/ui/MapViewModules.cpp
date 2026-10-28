// Edit module and Pin in place on the map (the web's moduleEdit.ts,
// BrickLayer.tsx and ModuleEditDim.tsx): modules are picked whole; one
// module can be opened part by part with the rest dimmed and out of reach;
// a pinned module doesn't move as a whole.

#include "../edit/ModuleSheets.h"
#include "MapView.h"
#include "MapViewInternal.h"
#include "ModuleEditBar.h"
#include "SetUpdatesBar.h"

#include "../core/Groups.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../core/ModuleEdit.h"
#include "../edit/Connectivity.h"
#include "../edit/ModuleCommands.h"
#include "../edit/SetUpdates.h"
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
    const QSet<QString> sel = pickedParts();
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
        // Only the parts that come or go.
        const PickIndex& index = pickIndex();
        const auto apply = [&](const QSet<QString>& guids, bool on) {
            for (const QString& guid : guids)
                for (QGraphicsItem* it : index.bricks.value(guid))
                    if (it->isSelected() != on && (!on || (it->flags() & QGraphicsItem::ItemIsSelectable)))
                        it->setSelected(on);
        };
        apply(QSet<QString>(want).subtract(sel), true);
        apply(QSet<QString>(sel).subtract(want), false);
    }
    lastBrickSelection_ = want;
}

bool MapView::selectionMayMove() {
    if (!map_ || !scene()) return true;
    const core::Module* pinned = core::pinnedAmong(pickedParts(), map_->sidecar.modules, editingModuleId_);
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

// ---- Placed custom sets and their versions (edit/SetUpdates.h) ------------

void MapView::setTrackedSets(edit::TrackedSet tracked) {
    trackedSets_ = std::move(tracked);
    refreshSetUpdates();
}

void MapView::refreshSetUpdates() {
    setUpdates_ = map_ && trackedSets_ ? edit::findSetUpdates(*map_, parts_, trackedSets_)
                                       : std::vector<edit::SetUpdate>{};
    if (setUpdates_.empty()) {
        if (updatesBar_) updatesBar_->hide();
        return;
    }
    if (!updatesBar_) {
        updatesBar_ = new SetUpdatesBar(this);
        connect(updatesBar_, &SetUpdatesBar::showRequested, this, [this] { showPlacedSets(setUpdates_); });
        connect(updatesBar_, &SetUpdatesBar::updateRequested, this,
                [this] { updatePlacedSets(setUpdates_); });
    }
    QStringList names;
    QString key;
    for (const auto& u : setUpdates_) {
        names << u.name;
        key += u.link.groupId + QLatin1Char(':') + u.version + QLatin1Char(',');
    }
    updatesBar_->setUpdates(names, key);
    if (updatesBar_->isVisible()) updatesBar_->place(viewport()->geometry());
}

void MapView::updatePlacedSets(const std::vector<edit::SetUpdate>& updates) {
    if (!map_ || updates.empty()) return;
    // A copy: the bar's own list (often `updates`) changes as this goes.
    const std::vector<edit::SetUpdate> list = updates; // NOLINT(performance-unnecessary-copy-initialization)
    auto* cmd = new edit::UpdatePlacedSetsCommand(*map_, parts_, list);
    const QSet<QString> fresh = cmd->newGuids();
    QStringList names;
    for (const auto& u : list)
        if (!names.contains(u.name)) names << u.name;
    undoStack_->push(cmd);
    if (fresh.isEmpty()) return;
    // Join the new parts to the track around them where they line up.
    edit::rebuildConnectivity(*map_, parts_);
    rebuildScene();
    QList<QGraphicsItem*> pick;
    for (QGraphicsItem* it : scene()->items())
        if (detail::isBrickItem(it) && fresh.contains(it->data(detail::kBrickDataGuid).toString()))
            pick.append(it);
    selectItems(pick);
    if (auto* mw = window())
        if (auto* sb = mw->findChild<QStatusBar*>())
            sb->showMessage(
                names.size() == 1
                    ? tr("Updated “%1” to its newest version. Undo (Ctrl+Z) puts it back.").arg(names.front())
                    : tr("Updated %n set(s) to their newest versions. Undo (Ctrl+Z) puts them back.", nullptr,
                         static_cast<int>(list.size())),
                7000);
}

void MapView::showPlacedSets(const std::vector<edit::SetUpdate>& updates) {
    if (!map_) return;
    QSet<QString> groups;
    for (const auto& u : updates) groups.insert(u.link.groupId);
    QSet<QString> guids;
    for (const auto& L : map_->layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        const auto& B = static_cast<const core::LayerBrick&>(*L);
        for (const auto& b : B.bricks)
            for (const QString& g : core::groupChain(B, b.myGroupId))
                if (groups.contains(g)) guids.insert(b.guid);
    }
    QList<QGraphicsItem*> pick;
    QRectF box;
    for (QGraphicsItem* it : scene()->items())
        if (detail::isBrickItem(it) && guids.contains(it->data(detail::kBrickDataGuid).toString())) {
            pick.append(it);
            box = box.united(it->sceneBoundingRect());
        }
    if (pick.isEmpty()) return;
    selectItems(pick);
    centerOn(box.center());
}

}  // namespace bld::ui
