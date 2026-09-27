#pragma once

#include "../core/Brick.h"
#include "../core/Group.h"
#include "../core/Module.h"

#include <QHash>
#include <QPointF>
#include <QSet>
#include <QRectF>
#include <QUndoCommand>

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
    State      before_;
    State      after_;
};

// Create a same-layer vanilla Group per affected layer covering every target
// brick's layer. Sets each target's myGroupId to the new group's guid so the
// serialized <Groups> list re-creates on save. Undo restores each brick's
// previous group assignment and removes the synthetic groups.
class GroupBricksCommand : public QUndoCommand {
public:
    GroupBricksCommand(core::Map& map, std::vector<BrickRef> targets,
                       QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    struct GroupMemo {
        int     layerIndex = -1;
        QString newGroupGuid;
    };
    struct MemberMemo {
        BrickRef ref;
        QString  previousGroupId;
    };
    core::Map& map_;
    std::vector<BrickRef>  targets_;
    std::vector<GroupMemo>  groupsAdded_;
    std::vector<MemberMemo> before_;
    bool prepared_ = false;
};

// Clear the myGroupId of every target brick. If a group is left with no
// remaining members in its layer, the group is also removed. Undo restores
// each brick's prior groupId and reinstates any removed Group.
class UngroupBricksCommand : public QUndoCommand {
public:
    UngroupBricksCommand(core::Map& map, std::vector<BrickRef> targets,
                         QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    struct MemberMemo {
        BrickRef ref;
        QString  previousGroupId;
    };
    struct GroupMemo {
        int layerIndex = -1;
        int index      = -1;
        core::Group group;
    };
    core::Map& map_;
    std::vector<BrickRef> targets_;
    std::vector<MemberMemo> before_;
    std::vector<GroupMemo> removedGroups_;
    bool prepared_ = false;
};

// Append a batch of bricks to the given layer as a single undoable step. Used
// by Paste / Duplicate so the user doesn't see N individual undo entries for
// a single clipboard operation.
class AddBricksCommand : public QUndoCommand {
public:
    AddBricksCommand(core::Map& map, int layerIndex, std::vector<core::Brick> bricks,
                     QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

    const std::vector<core::Brick>& bricks() const { return bricks_; }

private:
    core::Map& map_;
    int layerIndex_;
    std::vector<core::Brick> bricks_;
};

}
