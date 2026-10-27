#pragma once

// Placed custom sets keep track of their set's version, like modules from
// the Module library: the sidecar's `sets` (core::PlacedSet, never the
// .bbm) says which version of the set each placed copy (its outermost
// group) came from, and one of its parts with where that part sits in the
// set. When the set changes (its parts, where they sit, or its name), its
// version (parts::setVersion) differs and the layout says "Update
// available". Updating replaces the copy's parts with the set's current
// ones where the copy sits now, as one undo step; nothing updates by
// itself. Library sets (bundled with the app) aren't tracked. The web does
// the same (apps/web/src/editor/setUpdates.ts).

#include "../core/Brick.h"
#include "../core/Group.h"
#include "../core/Sidecar.h"

#include <QPointF>
#include <QSet>
#include <QString>
#include <QUndoCommand>

#include <functional>
#include <optional>
#include <vector>

namespace bld::core {
class Map;
class LayerBrick;
} // namespace bld::core
namespace bld::parts {
class PartsLibrary;
struct PartMetadata;
} // namespace bld::parts

namespace bld::edit {

// Which sets are tracked: custom ones (yours, your clubs', from a server),
// not the bundled library's.
using TrackedSet = std::function<bool(const parts::PartMetadata&)>;

// A set's version, following the tracked (custom) sets inside it
// (parts::nestedSetVersion). A sub-part is found as expandSet finds it
// (the library's part by that id), never a set it sits in, at most 16
// deep; with no `tracked`, every set inside counts. Empty for a part that
// isn't a set. Each inner set is worked out once; a set whose sets inside
// would need more than 20,000 of them keeps its own version
// (parts::setVersion). The web's server does the same (withNestedSetVersions).
QString trackedSetVersion(parts::PartsLibrary& lib, const parts::PartMetadata& set, const TrackedSet& tracked);

// The link for a copy of set `key` just placed as `groupId`: its version
// (trackedSetVersion), and where its first part sits in the set (its box
// centre and turn).
std::optional<core::PlacedSet> placedSetLink(parts::PartsLibrary& lib, const QString& key,
                                             const QString& groupId, const QString& firstBrickGuid,
                                             const TrackedSet& tracked);

// A placed copy whose set has changed since.
struct SetUpdate {
    core::PlacedSet link;
    int layerIndex = -1;
    QString setKey;  // the group's part number
    QString name;    // the set's English name, else its key
    QString version; // the set's version now
};

// Every placed copy whose set has a newer version than it came from.
std::vector<SetUpdate> findSetUpdates(const core::Map& map, parts::PartsLibrary& lib,
                                      const TrackedSet& tracked);

// The update for the set `guids` are exactly the parts of, if any.
std::optional<SetUpdate> updateForSelection(const core::Map& map, const std::vector<SetUpdate>& updates,
                                            const QSet<QString>& guids);

// Where the copy sits now: its set-local origin and turn. From its anchor
// part when that's still there; else from a part matching one of the set's
// parts by number; else its parts' middle, unturned.
struct SetPose {
    QPointF centre;
    double angle = 0.0;
};
SetPose placedSetPose(const core::LayerBrick& layer, const SetUpdate& u, parts::PartsLibrary& lib);

// Records links for copies just placed or pasted (one undo step with the
// placing, inside its macro).
class AddPlacedSetsCommand : public QUndoCommand {
public:
    AddPlacedSetsCommand(core::Map& map, std::vector<core::PlacedSet> links, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    core::Map& map_;
    std::vector<core::PlacedSet> links_;
    std::vector<core::PlacedSet> before_;
};

// Updates placed copies: their parts become their set's current ones where
// each sits now (place and turn), keeping the copy's group (and its place
// in a group of yours), so labels on it and its link stay; a module it was
// in gets the new parts. One undo step. Connections are made again by the
// caller's connectivity pass.
class UpdatePlacedSetsCommand : public QUndoCommand {
public:
    UpdatePlacedSetsCommand(core::Map& map, parts::PartsLibrary& lib, const std::vector<SetUpdate>& updates,
                            QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

    // The new parts, after redo.
    const QSet<QString>& newGuids() const { return newGuids_; }

private:
    struct LayerState {
        int index = -1;
        std::vector<core::Brick> bricks;
        std::vector<core::Group> groups;
    };
    void apply(const std::vector<LayerState>& layers, const std::vector<core::PlacedSet>& sets,
               const std::vector<core::Module>& modules);

    core::Map& map_;
    std::vector<LayerState> before_, after_;
    std::vector<core::PlacedSet> setsBefore_, setsAfter_;
    std::vector<core::Module> modulesBefore_, modulesAfter_;
    QSet<QString> newGuids_;
};

} // namespace bld::edit
