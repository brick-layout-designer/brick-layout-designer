#include "SetUpdates.h"

#include "Sets.h"

#include "../core/Groups.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/BrickPlacement.h"
#include "../parts/PartsLibrary.h"
#include "../parts/SetVersion.h"

#include <QObject>

#include <algorithm>
#include <utility>

namespace bld::edit {

namespace {

QString setName(const parts::PartMetadata& meta, const QString& key) {
    for (const auto& d : meta.descriptions)
        if (d.language == QLatin1String("en") && !d.text.trimmed().isEmpty()) return d.text.trimmed();
    return key;
}

// The bricks under `groupId`, at any depth.
std::vector<const core::Brick*> membersOf(const core::LayerBrick& layer, const QString& groupId) {
    std::vector<const core::Brick*> out;
    for (const auto& b : layer.bricks)
        if (core::groupChain(layer, b.myGroupId).contains(groupId)) out.push_back(&b);
    return out;
}

const core::LayerBrick* brickLayer(const core::Map& map, int i) {
    if (i < 0 || i >= static_cast<int>(map.layers().size())) return nullptr;
    const auto* L = map.layers()[i].get();
    return L && L->kind() == core::LayerKind::Brick ? static_cast<const core::LayerBrick*>(L) : nullptr;
}

} // namespace

std::optional<core::PlacedSet> placedSetLink(parts::PartsLibrary& lib, const QString& key,
                                             const QString& groupId, const QString& firstBrickGuid) {
    const auto meta = lib.metadata(key);
    if (!meta || groupId.isEmpty()) return std::nullopt;
    const QString version = parts::setVersion(*meta);
    if (version.isEmpty()) return std::nullopt;
    core::PlacedSet link;
    link.groupId = groupId;
    link.version = version;
    const ExpandedSet local = expandSet(lib, key, QPointF(0, 0));
    if (!firstBrickGuid.isEmpty() && !local.bricks.empty()) {
        link.anchorBrick = firstBrickGuid;
        link.anchorPos = local.bricks.front().displayArea.center();
        link.anchorAngle = local.bricks.front().orientation;
    }
    return link;
}

std::vector<SetUpdate> findSetUpdates(const core::Map& map, parts::PartsLibrary& lib,
                                      const TrackedSet& tracked) {
    std::vector<SetUpdate> out;
    if (map.sidecar.sets.empty()) return out;
    for (const core::PlacedSet& link : map.sidecar.sets) {
        for (int i = 0; i < static_cast<int>(map.layers().size()); ++i) {
            const core::LayerBrick* L = brickLayer(map, i);
            const core::Group* g = L ? core::findGroup(*L, link.groupId) : nullptr;
            if (!g) continue;
            const auto meta = lib.metadata(g->partNumber);
            if (meta && (!tracked || tracked(*meta))) {
                const QString now = parts::setVersion(*meta);
                if (!now.isEmpty() && now != link.version)
                    out.push_back({ link, i, g->partNumber, setName(*meta, g->partNumber), now });
            }
            break;
        }
    }
    return out;
}

std::optional<SetUpdate> updateForSelection(const core::Map& map, const std::vector<SetUpdate>& updates,
                                            const QSet<QString>& guids) {
    if (guids.isEmpty()) return std::nullopt;
    for (const SetUpdate& u : updates) {
        const core::LayerBrick* L = brickLayer(map, u.layerIndex);
        if (!L) continue;
        const auto members = membersOf(*L, u.link.groupId);
        if (!members.empty() && static_cast<int>(members.size()) == guids.size()
            && std::all_of(members.cbegin(), members.cend(),
                           [&](const core::Brick* b) { return guids.contains(b->guid); }))
            return u;
    }
    return std::nullopt;
}

SetPose placedSetPose(const core::LayerBrick& layer, const SetUpdate& u, parts::PartsLibrary& lib) {
    const auto members = membersOf(layer, u.link.groupId);
    const auto fromPart = [](const core::Brick& b, QPointF local, double localAngle) {
        const double angle = b.orientation - localAngle;
        return SetPose{ b.displayArea.center() - parts::placement::rotated(local, angle), angle };
    };
    if (!u.link.anchorBrick.isEmpty())
        for (const core::Brick* b : members)
            if (b->guid == u.link.anchorBrick) return fromPart(*b, u.link.anchorPos, u.link.anchorAngle);
    // No anchor: the first of its parts that the set (as it is now) has too.
    const ExpandedSet local = expandSet(lib, u.setKey, QPointF(0, 0));
    for (const core::Brick* b : members)
        for (const auto& leaf : local.bricks)
            if (leaf.partNumber.compare(b->partNumber, Qt::CaseInsensitive) == 0)
                return fromPart(*b, leaf.displayArea.center(), leaf.orientation);
    if (members.empty()) return {};
    QPointF mid;
    for (const core::Brick* b : members) mid += b->displayArea.center();
    return { mid / static_cast<double>(members.size()), 0.0 };
}

AddPlacedSetsCommand::AddPlacedSetsCommand(core::Map& map, std::vector<core::PlacedSet> links,
                                           QUndoCommand* parent)
    : QUndoCommand(QObject::tr("Keep track of a set's version"), parent), map_(map),
      links_(std::move(links)) {
}

void AddPlacedSetsCommand::redo() {
    before_ = map_.sidecar.sets;
    for (const core::PlacedSet& l : links_) {
        auto& sets = map_.sidecar.sets;
        sets.erase(std::remove_if(sets.begin(), sets.end(),
                                  [&](const core::PlacedSet& s) { return s.groupId == l.groupId; }),
                   sets.end());
        sets.push_back(l);
    }
}

void AddPlacedSetsCommand::undo() {
    map_.sidecar.sets = before_;
}

UpdatePlacedSetsCommand::UpdatePlacedSetsCommand(core::Map& map, parts::PartsLibrary& lib,
                                                 const std::vector<SetUpdate>& updates, QUndoCommand* parent)
    : QUndoCommand(QObject::tr("Update set"), parent), map_(map) {
    setsBefore_ = map.sidecar.sets;
    modulesBefore_ = map.sidecar.modules;
    setsAfter_ = setsBefore_;
    modulesAfter_ = modulesBefore_;
    // Each layer's parts and groups before and after, worked out once.
    const auto stateOf = [&](std::vector<LayerState>& list, int index) -> LayerState& {
        for (auto& s : list)
            if (s.index == index) return s;
        const auto& L = static_cast<const core::LayerBrick&>(*map.layers()[index]);
        list.push_back({ index, L.bricks, L.groups });
        return list.back();
    };
    for (const SetUpdate& u : updates) {
        const core::LayerBrick* L = brickLayer(map, u.layerIndex);
        if (!L || !core::findGroup(*L, u.link.groupId)) continue;
        stateOf(before_, u.layerIndex);
        LayerState& st = stateOf(after_, u.layerIndex);
        // The copy as it stands in the state so far.
        core::LayerBrick now;
        now.bricks = st.bricks;
        now.groups = st.groups;
        const core::Group* top = core::findGroup(now, u.link.groupId);
        if (!top) continue;
        const QString parent = top->myGroupId;
        const SetPose pose = placedSetPose(now, u, lib);
        ExpandedSet fresh = expandSet(lib, u.setKey, pose.centre, pose.angle);
        if (fresh.bricks.empty()) continue;
        // The outermost group keeps the copy's id and parent.
        const QString topNew = fresh.groups.front().guid;
        for (auto& g : fresh.groups) {
            if (g.guid == topNew) {
                g.guid = u.link.groupId;
                g.myGroupId = parent;
            } else if (g.myGroupId == topNew) {
                g.myGroupId = u.link.groupId;
            }
        }
        for (auto& b : fresh.bricks)
            if (b.myGroupId == topNew) b.myGroupId = u.link.groupId;
        const QSet<QString> oldBricks = core::bricksUnder(now, u.link.groupId);
        QSet<QString> oldGroups;
        for (const auto& g : now.groups)
            if (g.guid != u.link.groupId && core::groupChain(now, g.guid).contains(u.link.groupId))
                oldGroups.insert(g.guid);
        // The new parts take the old ones' place in the drawing order.
        auto at = std::find_if(st.bricks.begin(), st.bricks.end(),
                               [&](const core::Brick& b) { return oldBricks.contains(b.guid); });
        const auto offset = at - st.bricks.begin();
        st.bricks.erase(std::remove_if(st.bricks.begin(), st.bricks.end(),
                                       [&](const core::Brick& b) { return oldBricks.contains(b.guid); }),
                        st.bricks.end());
        const auto insertAt = std::min<std::ptrdiff_t>(offset, static_cast<std::ptrdiff_t>(st.bricks.size()));
        QStringList ids;
        for (const auto& b : fresh.bricks) {
            ids << b.guid;
            newGuids_.insert(b.guid);
        }
        st.bricks.insert(st.bricks.begin() + insertAt, fresh.bricks.begin(), fresh.bricks.end());
        st.groups.erase(std::remove_if(st.groups.begin(), st.groups.end(),
                                       [&](const core::Group& g) {
                                           return g.guid == u.link.groupId || oldGroups.contains(g.guid);
                                       }),
                        st.groups.end());
        st.groups.insert(st.groups.end(), fresh.groups.begin(), fresh.groups.end());
        // The link: the new version, and the new first part as its anchor.
        if (auto link = placedSetLink(lib, u.setKey, u.link.groupId, ids.value(0))) {
            link->extras = u.link.extras;
            setsAfter_.erase(
                std::remove_if(setsAfter_.begin(), setsAfter_.end(),
                               [&](const core::PlacedSet& s) { return s.groupId == u.link.groupId; }),
                setsAfter_.end());
            setsAfter_.push_back(*link);
        }
        // A module it was in: the new parts take the old ones' place.
        for (auto& m : modulesAfter_) {
            if (!m.memberIds.intersects(oldBricks)) continue;
            m.memberIds.subtract(oldBricks);
            for (const QString& id : std::as_const(ids)) m.memberIds.insert(id);
        }
    }
    if (after_.empty()) setObsolete(true);
}

void UpdatePlacedSetsCommand::apply(const std::vector<LayerState>& layers,
                                    const std::vector<core::PlacedSet>& sets,
                                    const std::vector<core::Module>& modules) {
    for (const LayerState& s : layers) {
        if (s.index < 0 || s.index >= static_cast<int>(map_.layers().size())) continue;
        auto* L = map_.layers()[s.index].get();
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        auto& B = static_cast<core::LayerBrick&>(*L);
        B.bricks = s.bricks;
        B.groups = s.groups;
    }
    map_.sidecar.sets = sets;
    map_.sidecar.modules = modules;
}

void UpdatePlacedSetsCommand::redo() {
    apply(after_, setsAfter_, modulesAfter_);
}

void UpdatePlacedSetsCommand::undo() {
    apply(before_, setsBefore_, modulesBefore_);
}

} // namespace bld::edit
