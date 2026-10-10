#include "EditCommands.h"

#include "../core/Brick.h"
#include "../core/Groups.h"
#include "../core/Ids.h"
#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace bld::edit {

namespace {

core::LayerBrick* brickLayer(core::Map& map, int idx) {
    if (idx < 0 || idx >= static_cast<int>(map.layers().size())) return nullptr;
    auto* L = map.layers()[idx].get();
    return (L && L->kind() == core::LayerKind::Brick)
        ? static_cast<core::LayerBrick*>(L)
        : nullptr;
}

core::Brick* findBrick(core::Map& map, const BrickRef& ref) {
    auto* L = brickLayer(map, ref.layerIndex);
    if (!L) return nullptr;
    for (auto& b : L->bricks) if (b.guid == ref.guid) return &b;
    return nullptr;
}

}

// ----- MoveBricksCommand -----

MoveBricksCommand::MoveBricksCommand(core::Map& map, std::vector<Entry> entries, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), entries_(std::move(entries)) {
    setText(QObject::tr("Move %1 brick(s)").arg(entries_.size()));
}

void MoveBricksCommand::redo() {
    for (const auto& e : entries_) {
        if (auto* b = findBrick(map_, e.ref)) {
            b->displayArea.moveTo(e.afterTopLeft);
        }
    }
}

void MoveBricksCommand::undo() {
    for (const auto& e : entries_) {
        if (auto* b = findBrick(map_, e.ref)) {
            b->displayArea.moveTo(e.beforeTopLeft);
        }
    }
}

// ----- RotateBricksCommand -----

RotateBricksCommand::RotateBricksCommand(core::Map& map, std::vector<Entry> entries, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), entries_(std::move(entries)) {
    setText(QObject::tr("Rotate %1 brick(s)").arg(entries_.size()));
}

void RotateBricksCommand::redo() {
    for (const auto& e : entries_) {
        if (auto* b = findBrick(map_, e.ref)) {
            b->orientation = e.afterOrientation;
            if (!e.afterArea.isNull()) b->displayArea = e.afterArea;
        }
    }
}

void RotateBricksCommand::undo() {
    for (const auto& e : entries_) {
        if (auto* b = findBrick(map_, e.ref)) {
            b->orientation = e.beforeOrientation;
            if (!e.afterArea.isNull()) b->displayArea = e.beforeArea;
        }
    }
}

// ----- DeleteBricksCommand -----

DeleteBricksCommand::DeleteBricksCommand(core::Map& map, std::vector<Entry> entries, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), entries_(std::move(entries)) {
    setText(QObject::tr("Delete %1 brick(s)").arg(entries_.size()));
}

void DeleteBricksCommand::redo() {
    // First delete pass: compute, once, which modules are affected so undo
    // has a complete before-picture. After that, redos re-apply the cached
    // delta rather than recomputing (the sidecar shape may have changed
    // during undo/redo cycles, but the original decision stands).
    if (!moduleDeltaReady_) {
        QSet<QString> deletedGuids;
        for (const auto& e : entries_) deletedGuids.insert(e.brick.guid);

        for (int i = static_cast<int>(map_.sidecar.modules.size()) - 1; i >= 0; --i) {
            const auto& mod = map_.sidecar.modules[i];
            const QSet<QString> intersection = mod.memberIds & deletedGuids;
            if (intersection.isEmpty()) continue;

            ModuleEdit edit;
            edit.id = mod.id;
            edit.beforeMemberIds = mod.memberIds;
            edit.afterMemberIds  = mod.memberIds;
            edit.afterMemberIds.subtract(deletedGuids);

            if (edit.afterMemberIds.isEmpty()) {
                ModuleRemoval rem;
                rem.index  = i;
                rem.module = mod;
                moduleRemovals_.push_back(std::move(rem));
            } else {
                moduleEdits_.push_back(std::move(edit));
            }
        }
        // Removals were pushed high-index-first; undo inserts low-index-first
        // so keep the vector sorted ascending.
        std::sort(moduleRemovals_.begin(), moduleRemovals_.end(),
                  [](const ModuleRemoval& a, const ModuleRemoval& b){ return a.index < b.index; });
        moduleDeltaReady_ = true;
    }

    for (const auto& e : entries_) {
        if (auto* L = brickLayer(map_, e.layerIndex)) {
            for (auto it = L->bricks.begin(); it != L->bricks.end(); ++it) {
                if (it->guid == e.brick.guid) { L->bricks.erase(it); break; }
            }
        }
    }

    // Apply module delta: trim edited modules, then drop the emptied ones
    // (high-index first so the indices stay valid as we erase).
    for (const auto& ed : moduleEdits_) {
        for (auto& mod : map_.sidecar.modules) {
            if (mod.id == ed.id) { mod.memberIds = ed.afterMemberIds; break; }
        }
    }
    for (auto it = moduleRemovals_.rbegin(); it != moduleRemovals_.rend(); ++it) {
        if (it->index >= 0 && it->index < static_cast<int>(map_.sidecar.modules.size())) {
            map_.sidecar.modules.erase(map_.sidecar.modules.begin() + it->index);
        }
    }
}

void DeleteBricksCommand::undo() {
    // Reinsert removed modules at their recorded indices (low-index first
    // so later insertions don't shift earlier ones).
    for (const auto& rem : moduleRemovals_) {
        const int idx = std::min<int>(rem.index, static_cast<int>(map_.sidecar.modules.size()));
        map_.sidecar.modules.insert(map_.sidecar.modules.begin() + idx, rem.module);
    }
    // Restore trimmed memberIds.
    for (const auto& ed : moduleEdits_) {
        for (auto& mod : map_.sidecar.modules) {
            if (mod.id == ed.id) { mod.memberIds = ed.beforeMemberIds; break; }
        }
    }

    // Restore in original order (forward iteration; insertAt indices were
    // captured before deletion and remain valid if we insert in order).
    for (const auto& e : entries_) {
        if (auto* L = brickLayer(map_, e.layerIndex)) {
            const int idx = std::min<int>(e.indexInLayer, static_cast<int>(L->bricks.size()));
            L->bricks.insert(L->bricks.begin() + idx, e.brick);
        }
    }
}

// ----- AddBrickCommand -----

AddBrickCommand::AddBrickCommand(core::Map& map, int layerIndex, core::Brick brick, int insertAt,
                                 QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), layerIndex_(layerIndex),
      brick_(std::move(brick)), insertAt_(insertAt) {
    setText(QObject::tr("Add brick %1").arg(brick_.partNumber));
}

void AddBrickCommand::redo() {
    if (auto* L = brickLayer(map_, layerIndex_)) {
        if (insertAt_ < 0 || insertAt_ > static_cast<int>(L->bricks.size())) {
            L->bricks.push_back(brick_);
        } else {
            L->bricks.insert(L->bricks.begin() + insertAt_, brick_);
        }
    }
}

void AddBrickCommand::undo() {
    if (auto* L = brickLayer(map_, layerIndex_)) {
        for (auto it = L->bricks.begin(); it != L->bricks.end(); ++it) {
            if (it->guid == brick_.guid) { L->bricks.erase(it); break; }
        }
    }
}

// ----- ReorderBricksCommand -----

ReorderBricksCommand::ReorderBricksCommand(core::Map& map, std::vector<Target> targets,
                                           Direction dir, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), targets_(std::move(targets)), dir_(dir) {
    setText(dir_ == ToFront
        ? QObject::tr("Bring %1 brick(s) to front").arg(targets_.size())
        : QObject::tr("Send %1 brick(s) to back").arg(targets_.size()));
}

void ReorderBricksCommand::redo() {
    // Snapshot the current per-layer order on first redo so undo can restore.
    if (!haveSnapshot_) {
        QHash<int, std::vector<QString>> snapshot;
        for (const auto& t : targets_) {
            if (snapshot.contains(t.layerIndex)) continue;
            if (auto* L = brickLayer(map_, t.layerIndex)) {
                std::vector<QString>& ord = snapshot[t.layerIndex];
                ord.reserve(L->bricks.size());
                for (const auto& b : L->bricks) ord.push_back(b.guid);
            }
        }
        originalOrder_ = std::move(snapshot);
        haveSnapshot_ = true;
    }

    // Group targets by layer so each layer reorders once.
    QHash<int, std::vector<QString>> byLayer;
    for (const auto& t : targets_) byLayer[t.layerIndex].push_back(t.guid);

    for (auto it = byLayer.begin(); it != byLayer.end(); ++it) {
        auto* L = brickLayer(map_, it.key());
        if (!L) continue;
        const std::vector<QString>& movingGuids = it.value();
        QSet<QString> movingSet(movingGuids.begin(), movingGuids.end());

        // Partition: keep relative order for both moving and non-moving sets.
        std::vector<core::Brick> keep;     // non-moving, in original order
        std::vector<core::Brick> moving;   // moving, in original order
        keep.reserve(L->bricks.size());
        moving.reserve(movingGuids.size());
        for (auto& b : L->bricks) {
            if (movingSet.contains(b.guid)) moving.push_back(std::move(b));
            else                            keep.push_back(std::move(b));
        }

        L->bricks.clear();
        L->bricks.reserve(keep.size() + moving.size());
        if (dir_ == ToFront) {
            for (auto& b : keep)   L->bricks.push_back(std::move(b));
            for (auto& b : moving) L->bricks.push_back(std::move(b));
        } else {
            for (auto& b : moving) L->bricks.push_back(std::move(b));
            for (auto& b : keep)   L->bricks.push_back(std::move(b));
        }
    }
}

void ReorderBricksCommand::undo() {
    for (auto it = originalOrder_.constBegin(); it != originalOrder_.constEnd(); ++it) {
        auto* L = brickLayer(map_, it.key());
        if (!L) continue;
        QHash<QString, core::Brick> byGuid;
        for (auto& b : L->bricks) byGuid.insert(b.guid, std::move(b));
        L->bricks.clear();
        L->bricks.reserve(it.value().size());
        for (const QString& guid : it.value()) {
            auto found = byGuid.find(guid);
            if (found != byGuid.end()) L->bricks.push_back(std::move(*found));
        }
    }
}

// ----- MoveBricksToLayerCommand -----

MoveBricksToLayerCommand::MoveBricksToLayerCommand(core::Map& map, std::vector<Target> targets,
                                                   int toLayerIndex, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), targets_(std::move(targets)), to_(toLayerIndex) {
    setText(QObject::tr("Move %n part(s) to another sheet", nullptr, static_cast<int>(targets_.size())));
}

void MoveBricksToLayerCommand::apply(const QHash<int, Contents>& state) {
    for (auto it = state.constBegin(); it != state.constEnd(); ++it) {
        if (auto* L = brickLayer(map_, it.key())) {
            L->bricks = it.value().bricks;
            L->groups = it.value().groups;
        }
    }
}

void MoveBricksToLayerCommand::redo() {
    if (!ready_) {
        auto* target = brickLayer(map_, to_);
        if (!target) return;
        QHash<int, QSet<QString>> byLayer;
        for (const auto& t : targets_)
            if (t.layerIndex != to_ && brickLayer(map_, t.layerIndex)) byLayer[t.layerIndex].insert(t.guid);
        before_.insert(to_, { target->bricks, target->groups });
        Contents into = before_.value(to_);
        for (auto it = byLayer.constBegin(); it != byLayer.constEnd(); ++it) {
            auto* L = brickLayer(map_, it.key());
            const QSet<QString>& moving = it.value();
            before_.insert(it.key(), { L->bricks, L->groups });
            QHash<QString, QString> parentOf;
            for (const auto& g : L->groups) parentOf.insert(g.guid, g.myGroupId);
            const auto chain = [&](QString g) {
                QStringList out;
                while (!g.isEmpty() && !out.contains(g)) {
                    out << g;
                    g = parentOf.value(g);
                }
                return out;
            };
            // A group goes when every part under it does.
            QSet<QString> stays, goes;
            for (const auto& b : L->bricks)
                if (!moving.contains(b.guid))
                    for (const QString& g : chain(b.myGroupId)) stays.insert(g);
            for (const auto& b : L->bricks)
                if (moving.contains(b.guid))
                    for (const QString& g : chain(b.myGroupId))
                        if (!stays.contains(g)) goes.insert(g);
            Contents left;
            for (const auto& g : L->groups) {
                if (!goes.contains(g.guid)) {
                    left.groups.push_back(g);
                    continue;
                }
                core::Group copy = g;
                if (!goes.contains(copy.myGroupId)) copy.myGroupId.clear();
                into.groups.push_back(std::move(copy));
            }
            for (const auto& b : L->bricks) {
                if (!moving.contains(b.guid)) {
                    left.bricks.push_back(b);
                    continue;
                }
                core::Brick copy = b;
                if (!goes.contains(copy.myGroupId)) copy.myGroupId.clear();
                for (auto& c : copy.connections) c.linkedToId.clear();
                into.bricks.push_back(std::move(copy));
                ++moved_;
            }
            after_.insert(it.key(), std::move(left));
        }
        after_.insert(to_, std::move(into));
        ready_ = true;
    }
    apply(after_);
}

void MoveBricksToLayerCommand::undo() { apply(before_); }

// ----- EditBrickCommand -----

EditBrickCommand::EditBrickCommand(core::Map& map, BrickRef ref, State before, State after,
                                   QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), ref_(std::move(ref)),
      before_(std::move(before)), after_(std::move(after)) {
    setText(QObject::tr("Edit brick %1").arg(after_.partNumber));
}

namespace {
void applyBrickState(core::Brick& b, const EditBrickCommand::State& s) {
    b.partNumber = s.partNumber;
    b.displayArea.moveTo(s.topLeft);
    b.orientation = s.orientation;
    b.altitude = s.altitude;
    b.activeConnectionPointIndex = s.activeConnectionPointIndex;
}
}

void EditBrickCommand::redo() {
    auto* b = findBrick(map_, ref_);
    if (!b) return;
    applyBrickState(*b, after_);
    // Drawing follows the sheet's order (BlueBrick); a new altitude sorts the
    // sheet so higher parts draw over lower ones (sortBricksByElevation).
    if (after_.altitude == before_.altitude) return;
    auto* L = brickLayer(map_, ref_.layerIndex);
    if (!L) return;
    orderBefore_.clear();
    for (const auto& x : L->bricks) orderBefore_.push_back(x.guid);
    std::stable_sort(L->bricks.begin(), L->bricks.end(),
                     [](const core::Brick& a, const core::Brick& c) { return a.altitude < c.altitude; });
}
void EditBrickCommand::undo() {
    if (auto* b = findBrick(map_, ref_)) applyBrickState(*b, before_);
    auto* L = brickLayer(map_, ref_.layerIndex);
    if (!L || orderBefore_.empty()) return;
    QHash<QString, core::Brick> byGuid;
    for (auto& x : L->bricks) byGuid.insert(x.guid, std::move(x));
    L->bricks.clear();
    for (const QString& g : orderBefore_) {
        auto found = byGuid.find(g);
        if (found != byGuid.end()) L->bricks.push_back(std::move(*found));
    }
    orderBefore_.clear();
}

// ----- Grouping -----

Grouping captureGrouping(const core::Map& map, int layerIndex) {
    Grouping g;
    g.layerIndex = layerIndex;
    if (layerIndex < 0 || layerIndex >= static_cast<int>(map.layers().size())) return g;
    const auto* L = map.layers()[layerIndex].get();
    if (!L || L->kind() != core::LayerKind::Brick) return g;
    const auto& BL = static_cast<const core::LayerBrick&>(*L);
    g.groups = BL.groups;
    for (const auto& b : BL.bricks) g.parentOf.insert(b.guid, b.myGroupId);
    return g;
}

namespace {
void applyGrouping(core::Map& map, const std::vector<Grouping>& state) {
    for (const auto& g : state) {
        auto* L = brickLayer(map, g.layerIndex);
        if (!L) continue;
        L->groups = g.groups;
        for (auto& b : L->bricks) {
            const auto it = g.parentOf.constFind(b.guid);
            if (it != g.parentOf.constEnd()) b.myGroupId = it.value();
        }
    }
}

// Targets by layer, each layer's outermost items: top group guids and loose brick guids.
struct TopItems { QStringList groups; QStringList bricks; };
QHash<int, TopItems> topItems(const core::Map& map, const std::vector<BrickRef>& targets) {
    QHash<int, TopItems> out;
    for (const auto& r : targets) {
        if (r.layerIndex < 0 || r.layerIndex >= static_cast<int>(map.layers().size())) continue;
        const auto* L = map.layers()[r.layerIndex].get();
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        const auto& BL = static_cast<const core::LayerBrick&>(*L);
        for (const auto& b : BL.bricks) {
            if (b.guid != r.guid) continue;
            TopItems& t = out[r.layerIndex];
            const QString top = core::topGroup(BL, b.myGroupId);
            if (top.isEmpty()) { if (!t.bricks.contains(b.guid)) t.bricks << b.guid; }
            else if (!t.groups.contains(top)) t.groups << top;
            break;
        }
    }
    return out;
}
}  // namespace

SetGroupingCommand::SetGroupingCommand(core::Map& map, QUndoCommand* parent) : QUndoCommand(parent), map_(map) {}

SetGroupingCommand::SetGroupingCommand(core::Map& map, std::vector<Grouping> before, std::vector<Grouping> after,
                                       QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), before_(std::move(before)), after_(std::move(after)) {}

void SetGroupingCommand::redo() { applyGrouping(map_, after_); }
void SetGroupingCommand::undo() { applyGrouping(map_, before_); }

GroupBricksCommand::GroupBricksCommand(core::Map& map, const std::vector<BrickRef>& targets, QUndoCommand* parent)
    : SetGroupingCommand(map, parent) {
    const auto tops = topItems(map, targets);
    qsizetype items = 0;
    for (const auto& t : tops) items += t.groups.size() + t.bricks.size();
    if (items < 2) return;  // nothing to group
    for (auto it = tops.constBegin(); it != tops.constEnd(); ++it) {
        Grouping before = captureGrouping(map, it.key());
        Grouping after = before;
        core::Group g;
        g.guid = core::newBbmId();
        for (auto& group : after.groups)
            if (it->groups.contains(group.guid)) group.myGroupId = g.guid;
        for (const QString& b : it->bricks) after.parentOf[b] = g.guid;
        after.groups.push_back(g);
        before_.push_back(std::move(before));
        after_.push_back(std::move(after));
    }
    setText(QObject::tr("Group %1 brick(s)").arg(targets.size()));
}

UngroupBricksCommand::UngroupBricksCommand(core::Map& map, const std::vector<BrickRef>& targets,
                                           const std::function<bool(const core::Group&)>& canUngroup,
                                           QUndoCommand* parent)
    : SetGroupingCommand(map, parent) {
    const auto tops = topItems(map, targets);
    for (auto it = tops.constBegin(); it != tops.constEnd(); ++it) {
        Grouping before = captureGrouping(map, it.key());
        Grouping after = before;
        bool changed = false;
        for (const QString& id : it->groups) {
            const auto found = std::find_if(after.groups.begin(), after.groups.end(),
                                            [&](const core::Group& g) { return g.guid == id; });
            if (found == after.groups.end()) continue;
            if (canUngroup && !canUngroup(*found)) { ++refused_; continue; }
            const QString up = found->myGroupId;
            after.groups.erase(found);
            for (auto& g : after.groups)
                if (g.myGroupId == id) g.myGroupId = up;
            for (auto p = after.parentOf.begin(); p != after.parentOf.end(); ++p)
                if (p.value() == id) p.value() = up;
            changed = true;
        }
        if (!changed) continue;
        before_.push_back(std::move(before));
        after_.push_back(std::move(after));
    }
    setText(QObject::tr("Ungroup %1 brick(s)").arg(targets.size()));
}

// ----- AddBricksCommand -----

AddBricksCommand::AddBricksCommand(core::Map& map, int layerIndex, std::vector<core::Brick> bricks,
                                   QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), layerIndex_(layerIndex), bricks_(std::move(bricks)) {
    setText(QObject::tr("Add %1 brick(s)").arg(bricks_.size()));
}

AddBricksCommand::AddBricksCommand(core::Map& map, int layerIndex, std::vector<core::Brick> bricks,
                                   std::vector<core::Group> groups, QUndoCommand* parent)
    : AddBricksCommand(map, layerIndex, std::move(bricks), parent) {
    groups_ = std::move(groups);
}

void AddBricksCommand::redo() {
    if (auto* L = brickLayer(map_, layerIndex_)) {
        for (const auto& b : bricks_) L->bricks.push_back(b);
        for (const auto& g : groups_) L->groups.push_back(g);
    }
}

void AddBricksCommand::undo() {
    auto* L = brickLayer(map_, layerIndex_);
    if (!L) return;
    // Guids are unique within our bricks_ list; remove each from the layer.
    QSet<QString> toRemove;
    for (const auto& b : bricks_) toRemove.insert(b.guid);
    L->bricks.erase(
        std::remove_if(L->bricks.begin(), L->bricks.end(),
                       [&](const core::Brick& b) { return toRemove.contains(b.guid); }),
        L->bricks.end());
    QSet<QString> groups;
    for (const auto& g : groups_) groups.insert(g.guid);
    L->groups.erase(std::remove_if(L->groups.begin(), L->groups.end(),
                                   [&](const core::Group& g) { return groups.contains(g.guid); }),
                    L->groups.end());
}

}
