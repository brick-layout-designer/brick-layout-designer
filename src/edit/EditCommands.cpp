#include "EditCommands.h"

#include "../core/Brick.h"
#include "../core/Ids.h"
#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"

#include <QHash>
#include <QSet>

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
    if (auto* b = findBrick(map_, ref_)) applyBrickState(*b, after_);
}
void EditBrickCommand::undo() {
    if (auto* b = findBrick(map_, ref_)) applyBrickState(*b, before_);
}

// ----- GroupBricksCommand -----

GroupBricksCommand::GroupBricksCommand(core::Map& map, std::vector<BrickRef> targets,
                                       QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), targets_(std::move(targets)) {
    setText(QObject::tr("Group %1 brick(s)").arg(targets_.size()));
}

void GroupBricksCommand::redo() {
    // Partition targets by layer to create one Group per layer.
    QHash<int, std::vector<QString>> perLayer;
    for (const auto& r : targets_) perLayer[r.layerIndex].push_back(r.guid);

    if (!prepared_) {
        groupsAdded_.clear();
        before_.clear();
        for (auto it = perLayer.constBegin(); it != perLayer.constEnd(); ++it) {
            auto* L = brickLayer(map_, it.key());
            if (!L) continue;
            core::Group g;
            g.guid = core::newBbmId();
            L->groups.push_back(g);
            groupsAdded_.push_back({ it.key(), g.guid });
            for (const QString& guid : it.value()) {
                for (auto& b : L->bricks) {
                    if (b.guid == guid) {
                        before_.push_back({ BrickRef{ it.key(), guid }, b.myGroupId });
                        b.myGroupId = g.guid;
                        break;
                    }
                }
            }
        }
        prepared_ = true;
    } else {
        // Re-apply: recreate the same groups and re-assign myGroupId.
        for (const auto& gm : groupsAdded_) {
            if (auto* L = brickLayer(map_, gm.layerIndex)) {
                core::Group g; g.guid = gm.newGroupGuid;
                L->groups.push_back(g);
            }
        }
        for (const auto& m : before_) {
            if (auto* b = findBrick(map_, m.ref)) {
                for (const auto& gm : groupsAdded_) {
                    if (gm.layerIndex == m.ref.layerIndex) { b->myGroupId = gm.newGroupGuid; break; }
                }
            }
        }
    }
}

void GroupBricksCommand::undo() {
    // Restore each brick's previous group id.
    for (const auto& m : before_) {
        if (auto* b = findBrick(map_, m.ref)) b->myGroupId = m.previousGroupId;
    }
    // Remove the synthesized groups.
    for (const auto& gm : groupsAdded_) {
        if (auto* L = brickLayer(map_, gm.layerIndex)) {
            for (auto it = L->groups.begin(); it != L->groups.end(); ++it) {
                if (it->guid == gm.newGroupGuid) { L->groups.erase(it); break; }
            }
        }
    }
}

// ----- UngroupBricksCommand -----

UngroupBricksCommand::UngroupBricksCommand(core::Map& map, std::vector<BrickRef> targets,
                                           QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), targets_(std::move(targets)) {
    setText(QObject::tr("Ungroup %1 brick(s)").arg(targets_.size()));
}

void UngroupBricksCommand::redo() {
    if (!prepared_) {
        before_.clear();
        removedGroups_.clear();
        // Use "layerIndex|groupGuid" as a hashable key so QSet works.
        QSet<QString> affectedGroups;
        auto keyOf = [](int li, const QString& g) {
            return QString::number(li) + QStringLiteral("|") + g;
        };
        // Clear myGroupId on every target, remembering prior value.
        for (const auto& ref : targets_) {
            if (auto* b = findBrick(map_, ref)) {
                before_.push_back({ ref, b->myGroupId });
                if (!b->myGroupId.isEmpty())
                    affectedGroups.insert(keyOf(ref.layerIndex, b->myGroupId));
                b->myGroupId.clear();
            }
        }
        // Remove now-empty groups.
        for (const QString& key : affectedGroups) {
            const int sep = key.indexOf('|');
            if (sep < 0) continue;
            const int layerIdx = key.left(sep).toInt();
            const QString groupGuid = key.mid(sep + 1);
            auto* L = brickLayer(map_, layerIdx);
            if (!L) continue;
            bool stillMember = false;
            for (const auto& b : L->bricks) if (b.myGroupId == groupGuid) { stillMember = true; break; }
            if (stillMember) continue;
            for (int i = 0; i < static_cast<int>(L->groups.size()); ++i) {
                if (L->groups[i].guid == groupGuid) {
                    removedGroups_.push_back({ layerIdx, i, L->groups[i] });
                    L->groups.erase(L->groups.begin() + i);
                    break;
                }
            }
        }
        prepared_ = true;
    } else {
        // Re-apply: clear myGroupIds again and remove the same groups.
        for (const auto& m : before_) {
            if (auto* b = findBrick(map_, m.ref)) b->myGroupId.clear();
        }
        for (const auto& rm : removedGroups_) {
            if (auto* L = brickLayer(map_, rm.layerIndex)) {
                for (auto it = L->groups.begin(); it != L->groups.end(); ++it) {
                    if (it->guid == rm.group.guid) { L->groups.erase(it); break; }
                }
            }
        }
    }
}

void UngroupBricksCommand::undo() {
    // Reinstate removed groups at their original indices.
    for (auto it = removedGroups_.rbegin(); it != removedGroups_.rend(); ++it) {
        if (auto* L = brickLayer(map_, it->layerIndex)) {
            const int idx = std::min<int>(it->index, static_cast<int>(L->groups.size()));
            L->groups.insert(L->groups.begin() + idx, it->group);
        }
    }
    // Restore each brick's prior groupId.
    for (const auto& m : before_) {
        if (auto* b = findBrick(map_, m.ref)) b->myGroupId = m.previousGroupId;
    }
}

// ----- AddBricksCommand -----

AddBricksCommand::AddBricksCommand(core::Map& map, int layerIndex, std::vector<core::Brick> bricks,
                                   QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), layerIndex_(layerIndex), bricks_(std::move(bricks)) {
    setText(QObject::tr("Add %1 brick(s)").arg(bricks_.size()));
}

void AddBricksCommand::redo() {
    if (auto* L = brickLayer(map_, layerIndex_)) {
        for (const auto& b : bricks_) L->bricks.push_back(b);
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
}

}
