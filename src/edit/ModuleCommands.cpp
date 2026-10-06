#include "ModuleCommands.h"
#include "ModuleSheets.h"

#include "../core/Groups.h"

#include "../core/Brick.h"
#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../core/Sidecar.h"
#include "../parts/BrickPlacement.h"

#include <QDateTime>
#include <QObject>
#include <QUuid>

#include <cmath>

namespace bld::edit {

namespace {

core::LayerBrick* brickLayer(core::Map& map, int idx) {
    if (idx < 0 || idx >= static_cast<int>(map.layers().size())) return nullptr;
    auto* L = map.layers()[idx].get();
    return (L && L->kind() == core::LayerKind::Brick)
        ? static_cast<core::LayerBrick*>(L)
        : nullptr;
}

int findModuleIndex(const core::Map& map, const QString& id) {
    for (int i = 0; i < static_cast<int>(map.sidecar.modules.size()); ++i) {
        if (map.sidecar.modules[i].id == id) return i;
    }
    return -1;
}

}

// ----- CreateModuleCommand -----

CreateModuleCommand::CreateModuleCommand(core::Map& map, QString name,
                                         std::vector<Member> members,
                                         QUndoCommand* parent)
    : QUndoCommand(parent), map_(map),
      moduleId_(core::newBbmId()),
      name_(std::move(name)), members_(std::move(members)) {
    setText(QObject::tr("Group as module %1 (%2 parts)").arg(name_).arg(members_.size()));
}

void CreateModuleCommand::redo() {
    core::Module m;
    m.id = moduleId_;
    m.name = name_;
    for (const auto& mem : members_) m.memberIds.insert(mem.guid);
    map_.sidecar.modules.push_back(std::move(m));
}

void CreateModuleCommand::undo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i >= 0) map_.sidecar.modules.erase(map_.sidecar.modules.begin() + i);
}

// ----- DeleteModuleCommand -----

DeleteModuleCommand::DeleteModuleCommand(core::Map& map, QString moduleId, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), moduleId_(std::move(moduleId)) {
    setText(QObject::tr("Delete module"));
}

void DeleteModuleCommand::redo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i < 0) { removed_.reset(); insertIndex_ = -1; return; }
    removed_ = map_.sidecar.modules[i];
    insertIndex_ = i;
    map_.sidecar.modules.erase(map_.sidecar.modules.begin() + i);
}

void DeleteModuleCommand::undo() {
    if (!removed_) return;
    const int idx = std::min<int>(insertIndex_, static_cast<int>(map_.sidecar.modules.size()));
    map_.sidecar.modules.insert(map_.sidecar.modules.begin() + idx, *removed_);
}

// ----- MoveModuleCommand -----

MoveModuleCommand::MoveModuleCommand(core::Map& map, QString moduleId, QPointF deltaStuds,
                                     QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), moduleId_(std::move(moduleId)), delta_(deltaStuds) {
    setText(QObject::tr("Move module"));
}

void MoveModuleCommand::redo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i < 0) return;
    const auto& mod = map_.sidecar.modules[i];
    for (auto& layerPtr : map_.layers()) {
        if (!layerPtr || layerPtr->kind() != core::LayerKind::Brick) continue;
        for (auto& b : static_cast<core::LayerBrick&>(*layerPtr).bricks) {
            if (mod.memberIds.contains(b.guid)) {
                b.displayArea.translate(delta_);
            }
        }
    }
}

void MoveModuleCommand::undo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i < 0) return;
    const auto& mod = map_.sidecar.modules[i];
    for (auto& layerPtr : map_.layers()) {
        if (!layerPtr || layerPtr->kind() != core::LayerKind::Brick) continue;
        for (auto& b : static_cast<core::LayerBrick&>(*layerPtr).bricks) {
            if (mod.memberIds.contains(b.guid)) {
                b.displayArea.translate(-delta_);
            }
        }
    }
}

// ----- RotateModuleCommand -----

RotateModuleCommand::RotateModuleCommand(core::Map& map, parts::PartsLibrary& lib, QString moduleId,
                                         double degrees, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), lib_(lib), moduleId_(std::move(moduleId)), degrees_(degrees) {
    setText(QObject::tr("Rotate module %1°").arg(degrees_, 0, 'f', 1));
}

void RotateModuleCommand::redo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i < 0) return;
    const auto& mod = map_.sidecar.modules[i];

    // Snapshot every member brick's pre-state the first time we redo, so
    // undo restores exact positions even for fractional angles.
    if (!captured_) {
        before_.clear();
        for (int li = 0; li < static_cast<int>(map_.layers().size()); ++li) {
            auto* L = brickLayer(map_, li);
            if (!L) continue;
            for (const auto& b : L->bricks) {
                if (mod.memberIds.contains(b.guid)) {
                    before_.push_back({ li, b.guid, b.displayArea, b.orientation });
                }
            }
        }
        captured_ = true;
    }
    if (before_.empty()) return;

    // Module centre: centroid of member sprite centres (stud coords).
    QPointF centroid(0, 0);
    int count = 0;
    for (int li = 0; li < static_cast<int>(map_.layers().size()); ++li) {
        auto* L = brickLayer(map_, li);
        if (!L) continue;
        for (const auto& b : L->bricks) {
            if (mod.memberIds.contains(b.guid)) {
                centroid += parts::placement::imageCentre(b, lib_);
                ++count;
            }
        }
    }
    if (count == 0) return;
    centroid /= count;

    const double rad = degrees_ * M_PI / 180.0;
    const double c = std::cos(rad), s = std::sin(rad);

    for (int li = 0; li < static_cast<int>(map_.layers().size()); ++li) {
        auto* L = brickLayer(map_, li);
        if (!L) continue;
        for (auto& b : L->bricks) {
            if (!mod.memberIds.contains(b.guid)) continue;
            // Turn the brick's sprite centre around the module centroid;
            // its displayArea follows the rotated hull.
            const QPointF rel = parts::placement::imageCentre(b, lib_) - centroid;
            const QPointF rotated(rel.x() * c - rel.y() * s, rel.x() * s + rel.y() * c);
            b.orientation = std::fmod(b.orientation + static_cast<float>(degrees_), 360.0f);
            parts::placement::placeByImageCentre(b, centroid + rotated, lib_);
        }
    }
}

void RotateModuleCommand::undo() {
    for (const auto& s : before_) {
        auto* L = brickLayer(map_, s.layerIndex);
        if (!L) continue;
        for (auto& b : L->bricks) {
            if (b.guid == s.guid) {
                b.displayArea = s.area;
                b.orientation = s.orientation;
                break;
            }
        }
    }
}

// ----- RenameModuleCommand -----

RenameModuleCommand::RenameModuleCommand(core::Map& map, QString moduleId, QString newName,
                                         QUndoCommand* parent)
    : QUndoCommand(parent), map_(map),
      moduleId_(std::move(moduleId)), newName_(std::move(newName)) {
    const int i = findModuleIndex(map_, moduleId_);
    if (i >= 0) oldName_ = map_.sidecar.modules[i].name;
    setText(QObject::tr("Rename module"));
}

void RenameModuleCommand::redo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i >= 0) map_.sidecar.modules[i].name = newName_;
}

void RenameModuleCommand::undo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i >= 0) map_.sidecar.modules[i].name = oldName_;
}

// ----- UpdateModuleCommand -----

UpdateModuleCommand::UpdateModuleCommand(core::Map& map, core::Module updated, const QString& text, int mergeKey,
                                         QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), after_(std::move(updated)), mergeKey_(mergeKey) {
    const int i = findModuleIndex(map_, after_.id);
    if (i >= 0) before_ = map_.sidecar.modules[i];
    setText(text);
}

void UpdateModuleCommand::redo() {
    const int i = findModuleIndex(map_, after_.id);
    if (i < 0) return;
    // The members may have changed since (a part added): keep them.
    core::Module next = after_;
    next.memberIds = map_.sidecar.modules[i].memberIds;
    map_.sidecar.modules[i] = next;
}

void UpdateModuleCommand::undo() {
    const int i = findModuleIndex(map_, before_.id);
    if (i < 0) return;
    core::Module prev = before_;
    prev.memberIds = map_.sidecar.modules[i].memberIds;
    map_.sidecar.modules[i] = prev;
}

bool UpdateModuleCommand::mergeWith(const QUndoCommand* other) {
    const auto* o = dynamic_cast<const UpdateModuleCommand*>(other);
    if (!o || o->after_.id != after_.id || mergeKey_ < 0) return false;
    after_ = o->after_;
    return true;
}

// ----- SetModuleMembersCommand -----

SetModuleMembersCommand::SetModuleMembersCommand(core::Map& map, QString moduleId, QSet<QString> members,
                                                 const QString& text, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), moduleId_(std::move(moduleId)), after_(std::move(members)) {
    const int i = findModuleIndex(map_, moduleId_);
    if (i >= 0) before_ = map_.sidecar.modules[i].memberIds;
    setText(text);
}

void SetModuleMembersCommand::redo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i >= 0) map_.sidecar.modules[i].memberIds = after_;
}

void SetModuleMembersCommand::undo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i >= 0) map_.sidecar.modules[i].memberIds = before_;
}

// ----- CloneModuleCommand -----

CloneModuleCommand::CloneModuleCommand(core::Map& map, QString sourceModuleId,
                                       QPointF offsetStuds, QString newName,
                                       QUndoCommand* parent)
    : QUndoCommand(parent), map_(map),
      sourceModuleId_(std::move(sourceModuleId)),
      offsetStuds_(offsetStuds),
      newName_(std::move(newName)),
      newModuleId_(core::newBbmId()) {
    setText(QObject::tr("Duplicate module"));
}

void CloneModuleCommand::redo() {
    const int srcIdx = findModuleIndex(map_, sourceModuleId_);
    if (srcIdx < 0) return;
    const core::Module srcMod = map_.sidecar.modules[srcIdx];  // copy

    // First redo: duplicate the source module's members on THEIR ORIGINAL
    // layers with fresh guids and the offset, with the sets (groups) they
    // are in. Later redos put back exactly those copies.
    if (!captured_) {
        clones_.clear();
        for (int li = 0; li < static_cast<int>(map_.layers().size()); ++li) {
            auto* L = brickLayer(map_, li);
            if (!L) continue;
            LayerClone clone;
            clone.layerIndex = li;
            for (const auto& b : L->bricks) {
                if (!srcMod.memberIds.contains(b.guid)) continue;
                core::Brick copy = b;
                copy.guid = core::newBbmId();
                copy.displayArea.translate(offsetStuds_);
                // Clones shouldn't inherit the source's connection links —
                // new bricks are un-linked and snap fresh.
                for (auto& c : copy.connections) {
                    c.guid = core::newBbmId();
                    c.linkedToId.clear();
                }
                clone.bricks.push_back(std::move(copy));
            }
            if (clone.bricks.empty()) continue;
            clone.groups = core::cloneGroups(*L, clone.bricks, [] { return core::newBbmId(); });
            clones_.push_back(std::move(clone));
        }
        captured_ = true;
    }
    appliedBricks_.clear();
    for (const auto& clone : clones_) {
        auto* L = brickLayer(map_, clone.layerIndex);
        if (!L) continue;
        for (const auto& b : clone.bricks) {
            appliedBricks_.push_back({ clone.layerIndex, b.guid });
            L->bricks.push_back(b);
        }
        for (const auto& g : clone.groups) L->groups.push_back(g);
    }

    core::Module m;
    m.id = newModuleId_;
    m.name = newName_.isEmpty() ? (srcMod.name + QObject::tr(" (copy)")) : newName_;
    for (const auto& a : appliedBricks_) m.memberIds.insert(a.guid);
    map_.sidecar.modules.push_back(std::move(m));
}

void CloneModuleCommand::undo() {
    // Remove cloned bricks and their groups.
    for (const auto& clone : clones_) {
        auto* L = brickLayer(map_, clone.layerIndex);
        if (!L) continue;
        QSet<QString> bricks, groups;
        for (const auto& b : clone.bricks) bricks.insert(b.guid);
        for (const auto& g : clone.groups) groups.insert(g.guid);
        L->bricks.erase(std::remove_if(L->bricks.begin(), L->bricks.end(),
                                       [&](const core::Brick& b) { return bricks.contains(b.guid); }),
                        L->bricks.end());
        L->groups.erase(std::remove_if(L->groups.begin(), L->groups.end(),
                                       [&](const core::Group& g) { return groups.contains(g.guid); }),
                        L->groups.end());
    }
    // Remove cloned module entry.
    const int i = findModuleIndex(map_, newModuleId_);
    if (i >= 0) map_.sidecar.modules.erase(map_.sidecar.modules.begin() + i);
}

// ----- FlattenModuleCommand -----

FlattenModuleCommand::FlattenModuleCommand(core::Map& map, QString moduleId, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), moduleId_(std::move(moduleId)) {
    setText(QObject::tr("Ungroup module"));
}

void FlattenModuleCommand::redo() {
    const int i = findModuleIndex(map_, moduleId_);
    if (i < 0) { removed_.reset(); insertIndex_ = -1; return; }
    removed_ = map_.sidecar.modules[i];
    insertIndex_ = i;
    map_.sidecar.modules.erase(map_.sidecar.modules.begin() + i);
}

void FlattenModuleCommand::undo() {
    if (!removed_) return;
    const int idx = std::min<int>(insertIndex_, static_cast<int>(map_.sidecar.modules.size()));
    map_.sidecar.modules.insert(map_.sidecar.modules.begin() + idx, *removed_);
}

// ----- RescanModuleCommand -----

RescanModuleCommand::RescanModuleCommand(core::Map& map, int targetLayerIndex, QString moduleId,
                                         std::vector<core::Brick> freshBricks,
                                         QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), layerIndex_(targetLayerIndex),
      moduleId_(std::move(moduleId)), freshBricks_(std::move(freshBricks)) {
    setText(QObject::tr("Update module (%1 parts)").arg(freshBricks_.size()));
}

void RescanModuleCommand::redo() {
    const int mi = findModuleIndex(map_, moduleId_);
    if (mi < 0) return;
    auto* L = brickLayer(map_, layerIndex_);
    if (!L) return;

    // Snapshot the pre-state once: remove old members from the layer and
    // stash them so undo reinstates.
    if (!captured_) {
        oldMemberIds_ = map_.sidecar.modules[mi].memberIds;
        oldBricks_.clear();
        auto it = L->bricks.begin();
        while (it != L->bricks.end()) {
            if (oldMemberIds_.contains(it->guid)) {
                oldBricks_.push_back(*it);
                it = L->bricks.erase(it);
            } else {
                ++it;
            }
        }
        captured_ = true;
    } else {
        // Re-apply on redo after undo: remove the same guids again.
        auto it = L->bricks.begin();
        while (it != L->bricks.end()) {
            if (oldMemberIds_.contains(it->guid)) it = L->bricks.erase(it);
            else                                   ++it;
        }
    }

    // Insert fresh bricks and update memberIds. Fresh guids have already been
    // minted by the caller (MainWindow reads the .bbm and clears guids).
    QSet<QString> newIds;
    for (auto& b : freshBricks_) {
        if (b.guid.isEmpty()) b.guid = core::newBbmId();
        newIds.insert(b.guid);
        L->bricks.push_back(b);
    }
    map_.sidecar.modules[mi].memberIds = newIds;
    map_.sidecar.modules[mi].importedAt = QDateTime::currentDateTimeUtc();
}

void RescanModuleCommand::undo() {
    const int mi = findModuleIndex(map_, moduleId_);
    if (mi < 0) return;
    auto* L = brickLayer(map_, layerIndex_);
    if (!L) return;
    // Remove fresh bricks.
    QSet<QString> freshIds;
    for (const auto& b : freshBricks_) freshIds.insert(b.guid);
    auto it = L->bricks.begin();
    while (it != L->bricks.end()) {
        if (freshIds.contains(it->guid)) it = L->bricks.erase(it);
        else                              ++it;
    }
    // Restore old bricks + memberIds.
    for (const auto& b : oldBricks_) L->bricks.push_back(b);
    map_.sidecar.modules[mi].memberIds = oldMemberIds_;
}

// ----- ImportBbmAsModuleCommand -----

ImportBbmAsModuleCommand::ImportBbmAsModuleCommand(core::Map& map,
                                                   QString sourcePath, QString moduleName,
                                                   std::vector<LayerBatch> batches,
                                                   QUndoCommand* parent)
    : QUndoCommand(parent), map_(map),
      sourcePath_(std::move(sourcePath)),
      moduleId_(core::newBbmId()),
      name_(std::move(moduleName)), batches_(std::move(batches)) {
    int total = 0; for (const auto& b : batches_) total += static_cast<int>(b.bricks.size());
    setText(QObject::tr("Import module (%1 parts)").arg(total));
}

// Back-compat single-layer ctor: wraps the args into a one-batch array so
// the multi-layer redo/undo path is the single code path.
ImportBbmAsModuleCommand::ImportBbmAsModuleCommand(core::Map& map, int targetLayerIndex,
                                                   QString sourcePath, QString moduleName,
                                                   std::vector<core::Brick> bricks,
                                                   QUndoCommand* parent)
    : QUndoCommand(parent), map_(map),
      sourcePath_(std::move(sourcePath)),
      moduleId_(core::newBbmId()),
      name_(std::move(moduleName)) {
    // Pick the layer's current name (if any) so undo/redo stays stable.
    QString layerName = QStringLiteral("Bricks");
    if (targetLayerIndex >= 0 && targetLayerIndex < static_cast<int>(map.layers().size())) {
        layerName = map.layers()[targetLayerIndex]->name;
    }
    LayerBatch batch;
    batch.layerName = layerName;
    batch.bricks = std::move(bricks);
    batches_.push_back(std::move(batch));
    setText(QObject::tr("Import module (%1 parts)").arg(batches_.front().bricks.size()));
}

namespace {
int findBrickLayerByGuid(core::Map& map, const QString& guid) {
    if (guid.isEmpty()) return -1;
    for (int i = 0; i < static_cast<int>(map.layers().size()); ++i) {
        auto* L = map.layers()[i].get();
        if (L && L->kind() == core::LayerKind::Brick && L->guid == guid) return i;
    }
    return -1;
}
}

void ImportBbmAsModuleCommand::redo() {
    // First redo: resolve each batch's target layer (the brick layer with
    // the same name; else the one the person chose; else a new brick
    // layer with the batch's name), then insert bricks. Later redos
    // (after an undo) replay the same resolution captured in applied_
    // (one entry per batch, in order) so the layer set is the same every time.
    if (!captured_) {
        applied_.clear();
        for (auto& batch : batches_) {
            AppliedLayer a;
            a.layerName = batch.layerName;
            int idx = findPartsSheet(map_, a.layerName);
            if (idx < 0) idx = findBrickLayerByGuid(map_, batch.targetLayerGuid);
            if (idx < 0) {
                auto L = std::make_unique<core::LayerBrick>();
                L->guid = core::newBbmId();
                L->name = a.layerName.isEmpty() ? QStringLiteral("Module") : a.layerName;
                idx = static_cast<int>(map_.layers().size());
                map_.layers().push_back(std::move(L));
                a.wasCreated = true;
            }
            a.layerIndex = idx;
            a.layerGuid = map_.layers()[idx]->guid;
            auto* BL = brickLayer(map_, idx);
            for (auto& b : batch.bricks) {
                if (b.guid.isEmpty()) b.guid = core::newBbmId();
                a.addedGuids.append(b.guid);
                if (BL) BL->bricks.push_back(b);
            }
            if (BL)
                for (const auto& g : batch.groups) BL->groups.push_back(g);
            applied_.push_back(std::move(a));
        }
        captured_ = true;
    } else {
        // Re-apply: recreate any layers we had created, re-insert bricks.
        for (std::size_t i = 0; i < applied_.size() && i < batches_.size(); ++i) {
            auto& a = applied_[i];
            if (a.wasCreated) {
                auto L = std::make_unique<core::LayerBrick>();
                L->guid = a.layerGuid.isEmpty() ? core::newBbmId() : a.layerGuid;
                L->name = a.layerName.isEmpty() ? QStringLiteral("Module") : a.layerName;
                a.layerIndex = static_cast<int>(map_.layers().size());
                map_.layers().push_back(std::move(L));
            } else {
                a.layerIndex = findBrickLayerByGuid(map_, a.layerGuid);
                if (a.layerIndex < 0) a.layerIndex = findPartsSheet(map_, a.layerName);
            }
            auto* BL = brickLayer(map_, a.layerIndex);
            if (!BL) continue;
            for (const auto& b : batches_[i].bricks) BL->bricks.push_back(b);
            for (const auto& g : batches_[i].groups) BL->groups.push_back(g);
        }
    }

    core::Module mod;
    mod.id = moduleId_;
    mod.name = name_;
    mod.sourceFile = sourcePath_;
    mod.importedAt = QDateTime::currentDateTimeUtc();
    for (const auto& a : applied_)
        for (const QString& g : a.addedGuids) mod.memberIds.insert(g);
    map_.sidecar.modules.push_back(std::move(mod));
}

void ImportBbmAsModuleCommand::undo() {
    // Remove every brick we added; then pop any layers we created (in
    // reverse order so their indices stay valid); then drop the module.
    for (auto& a : applied_) {
        if (auto* BL = brickLayer(map_, a.layerIndex)) {
            QSet<QString> toRemove;
            for (const QString& g : a.addedGuids) toRemove.insert(g);
            BL->bricks.erase(
                std::remove_if(BL->bricks.begin(), BL->bricks.end(),
                               [&](const core::Brick& b) { return toRemove.contains(b.guid); }),
                BL->bricks.end());
            QSet<QString> groups;
            const std::size_t i = static_cast<std::size_t>(&a - applied_.data());
            if (i < batches_.size())
                for (const auto& g : batches_[i].groups) groups.insert(g.guid);
            BL->groups.erase(std::remove_if(BL->groups.begin(), BL->groups.end(),
                                            [&](const core::Group& g) { return groups.contains(g.guid); }),
                             BL->groups.end());
        }
    }
    // Walk applied_ in reverse; for layers we created, drop them so the
    // host map returns to its pre-import layer count.
    for (auto it = applied_.rbegin(); it != applied_.rend(); ++it) {
        if (!it->wasCreated) continue;
        if (it->layerIndex < 0 ||
            it->layerIndex >= static_cast<int>(map_.layers().size())) continue;
        map_.layers().erase(map_.layers().begin() + it->layerIndex);
    }
    const int i = findModuleIndex(map_, moduleId_);
    if (i >= 0) map_.sidecar.modules.erase(map_.sidecar.modules.begin() + i);
}

}
