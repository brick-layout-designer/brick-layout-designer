#pragma once

#include "../core/Brick.h"
#include "../core/Group.h"
#include "../core/Module.h"

#include <QHash>
#include <QPointF>
#include <QSet>
#include <QRectF>
#include <QUndoCommand>

#include <functional>
#include <vector>

namespace bld::core { class Layer; class LayerBrick; class Map; }

namespace bld::edit {

// Identity for a brick in the edit pipeline: (layer index, brick guid).
// Layer index + guid is stable across vector reshuffles (unlike raw pointers)
// so commands undo/redo correctly even after concurrent mutations.
struct BrickRef {
    int     layerIndex = -1;
    QString guid;
};

// Move one or more bricks by per-brick deltas (in studs). `before/after`
// hold the brick's displayArea position before/after; undo restores before.
class MoveBricksCommand : public QUndoCommand {
public:
    struct Entry {
        BrickRef ref;
        QPointF  beforeTopLeft;   // in studs
        QPointF  afterTopLeft;    // in studs
    };

    MoveBricksCommand(core::Map& map, std::vector<Entry> entries, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

    const std::vector<Entry>& entries() const { return entries_; }

private:
    core::Map& map_;
    std::vector<Entry> entries_;
};

// Rotate bricks. Orientation in degrees; the displayArea changes with it
// (its size follows the rotated hull, and bricks turn around their sprite
// centre or a shared pivot), so both are stored for an exact undo. A null
// afterArea leaves the displayArea alone.
class RotateBricksCommand : public QUndoCommand {
public:
    struct Entry {
        BrickRef ref;
        float beforeOrientation = 0.0f;
        float afterOrientation  = 0.0f;
        QRectF beforeArea;
        QRectF afterArea;
    };

    RotateBricksCommand(core::Map& map, std::vector<Entry> entries, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    core::Map& map_;
    std::vector<Entry> entries_;
};

// Delete one or more bricks from their layers. Redo removes; undo restores
// at their original index. A brick can be a member of a sidecar Module; when
// every member of a module lives in this delete batch the module is removed
// too (otherwise the Modules panel would list a ghost module with zero
// members). Per-module membership edits are captured so undo restores them.
class DeleteBricksCommand : public QUndoCommand {
public:
    struct Entry {
        int layerIndex = -1;
        int indexInLayer = -1;       // insertion index for undo
        core::Brick brick;           // full deep copy so undo can restore
    };

    DeleteBricksCommand(core::Map& map, std::vector<Entry> entries, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    core::Map& map_;
    std::vector<Entry> entries_;

    struct ModuleEdit {
        QString id;
        QSet<QString> beforeMemberIds;
        QSet<QString> afterMemberIds;
    };
    struct ModuleRemoval {
        int index = -1;
        core::Module module;
    };
    std::vector<ModuleEdit>    moduleEdits_;
    std::vector<ModuleRemoval> moduleRemovals_;  // sorted by index ascending
    bool moduleDeltaReady_ = false;
};

// Add a single brick to the given layer at the given index (or append if -1).
class AddBrickCommand : public QUndoCommand {
public:
    AddBrickCommand(core::Map& map, int layerIndex, core::Brick brick, int insertAt = -1,
                    QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    core::Map& map_;
    int         layerIndex_;
    core::Brick brick_;
    int         insertAt_;
};

// Move a set of bricks to the front / back of the drawing order within their
// layer (upstream's "Bring to Front" / "Send to Back"). Selected bricks move
// together, preserving their relative order.
class ReorderBricksCommand : public QUndoCommand {
public:
    enum Direction { ToFront, ToBack };

    struct Target { int layerIndex = -1; QString guid; };

    ReorderBricksCommand(core::Map& map, std::vector<Target> targets, Direction dir,
                         QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    core::Map& map_;
    std::vector<Target> targets_;
    Direction dir_;
    // Per-layer snapshot of brick order taken on first redo; restored by undo.
    QHash<int, std::vector<QString>> originalOrder_;
    bool haveSnapshot_ = false;
};

// Move parts to another part sheet, on top of what's there. A group goes
// along when every part under it does (and its parent groups likewise); a
// part whose group stays behind leaves it. Guids are kept, so modules,
// labels and rulers still find the parts; links are rebuilt afterwards
// (MapView rebuilds connectivity on every undo-stack change).
class MoveBricksToLayerCommand : public QUndoCommand {
public:
    struct Target { int layerIndex = -1; QString guid; };

    MoveBricksToLayerCommand(core::Map& map, std::vector<Target> targets, int toLayerIndex,
                             QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
    int moved() const { return moved_; }

private:
    struct Contents { std::vector<core::Brick> bricks; std::vector<core::Group> groups; };
    void apply(const QHash<int, Contents>& state);

    core::Map& map_;
    std::vector<Target> targets_;
    int to_;
    QHash<int, Contents> before_, after_;
    bool ready_ = false;
    int moved_ = 0;
};

// Edit a single brick's mutable properties in one undoable step. Upstream's
// EditBrickForm exposes part number (for ReplaceBrick), position, orientation,
// altitude, and active connection point — we capture the same surface here so
// one dialog drives everything. Fields whose before/after match are no-ops.
class EditBrickCommand : public QUndoCommand {
public:
    struct State {
        QString partNumber;
        QPointF topLeft;          // studs
        float   orientation = 0.0f;
        float   altitude    = 0.0f;
        int     activeConnectionPointIndex = 0;
    };

    EditBrickCommand(core::Map& map, BrickRef ref, State before, State after,
                     QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    core::Map& map_;
    BrickRef   ref_;
    // A new altitude re-sorts the sheet by altitude, as BlueBrick does
    // (LayerBrick.sortBricksByElevation); undo puts the old order back.
    std::vector<QString> orderBefore_;
    State      before_;
    State      after_;
};

// A brick layer's groups and its bricks' <MyGroup>, as a whole.
struct Grouping {
    int layerIndex = -1;
    std::vector<core::Group> groups;
    QHash<QString, QString> parentOf;  // brick guid -> myGroupId
};
Grouping captureGrouping(const core::Map& map, int layerIndex);

// Puts layers' groupings from `before` to `after` (and back on undo).
class SetGroupingCommand : public QUndoCommand {
public:
    SetGroupingCommand(core::Map& map, std::vector<Grouping> before, std::vector<Grouping> after,
                       QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
    bool changes() const { return !after_.empty(); }

protected:
    explicit SetGroupingCommand(core::Map& map, QUndoCommand* parent = nullptr);
    core::Map& map_;
    std::vector<Grouping> before_, after_;
};

// BlueBrick's GroupItems: the targets' outermost items (a set stays whole
// inside) go into one new group per layer, when there are at least two
// items in all.
class GroupBricksCommand : public SetGroupingCommand {
public:
    GroupBricksCommand(core::Map& map, const std::vector<BrickRef>& targets, QUndoCommand* parent = nullptr);
};

// BlueBrick's UngroupItems: the targets' outermost groups that may be split
// are removed, their items going up a level. A set whose <CanUngroup> is
// false (`canUngroup` says no) stays; refused() counts those.
class UngroupBricksCommand : public SetGroupingCommand {
public:
    UngroupBricksCommand(core::Map& map, const std::vector<BrickRef>& targets,
                         const std::function<bool(const core::Group&)>& canUngroup = {},
                         QUndoCommand* parent = nullptr);
    int refused() const { return refused_; }

private:
    int refused_ = 0;
};

// Append a batch of bricks to the given layer as a single undoable step. Used
// by Paste / Duplicate so the user doesn't see N individual undo entries for
// a single clipboard operation.
class AddBricksCommand : public QUndoCommand {
public:
    AddBricksCommand(core::Map& map, int layerIndex, std::vector<core::Brick> bricks,
                     QUndoCommand* parent = nullptr);
    // With the groups the bricks belong to (a placed or pasted set).
    AddBricksCommand(core::Map& map, int layerIndex, std::vector<core::Brick> bricks,
                     std::vector<core::Group> groups, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

    const std::vector<core::Brick>& bricks() const { return bricks_; }

private:
    core::Map& map_;
    int layerIndex_;
    std::vector<core::Brick> bricks_;
    std::vector<core::Group> groups_;
};

}
