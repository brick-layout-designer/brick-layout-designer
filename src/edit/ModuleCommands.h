#pragma once

#include "../core/Brick.h"
#include "../core/Module.h"

#include <QPointF>
#include <QString>
#include <QUndoCommand>

#include <vector>

namespace bld::core { class Map; }

namespace bld::parts { class PartsLibrary; }

namespace bld::edit {

// Create a sidecar module grouping the given (layer, guid) pairs. No brick data
// is mutated — this is pure sidecar bookkeeping. Redo appends to sidecar.modules;
// undo removes it.
class CreateModuleCommand : public QUndoCommand {
public:
    struct Member { int layerIndex = -1; QString guid; };
    CreateModuleCommand(core::Map& map, QString name, std::vector<Member> members,
                        QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
    const QString& moduleId() const { return moduleId_; }

private:
    core::Map&  map_;
    QString     moduleId_;
    QString     name_;
    std::vector<Member> members_;
};

// Remove a module from sidecar.modules. Keeps the member bricks untouched.
class DeleteModuleCommand : public QUndoCommand {
public:
    DeleteModuleCommand(core::Map& map, QString moduleId, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    QString    moduleId_;
    std::optional<core::Module> removed_;
    int insertIndex_ = -1;
};

// Translate every brick belonging to a module by a stud delta. Cross-layer,
// since module membership spans layers.
class MoveModuleCommand : public QUndoCommand {
public:
    MoveModuleCommand(core::Map& map, QString moduleId, QPointF deltaStuds,
                      QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    QString    moduleId_;
    QPointF    delta_;
};

// Rotate every brick belonging to a module around the module's centroid by
// the given angle (degrees; positive = clockwise to match Qt convention).
// Cross-layer. Captures pre-state on first redo so undo is exact.
class RotateModuleCommand : public QUndoCommand {
public:
    RotateModuleCommand(core::Map& map, parts::PartsLibrary& lib, QString moduleId, double degrees,
                        QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    struct Snap { int layerIndex = -1; QString guid; QRectF area; float orientation = 0.0f; };
    core::Map& map_;
    parts::PartsLibrary& lib_;
    QString    moduleId_;
    double     degrees_;
    std::vector<Snap> before_;
    bool        captured_ = false;
};

// Rename a module in-project. Purely updates sidecar.modules[i].name;
// no brick data is touched.
class RenameModuleCommand : public QUndoCommand {
public:
    RenameModuleCommand(core::Map& map, QString moduleId, QString newName,
                        QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    QString    moduleId_;
    QString    oldName_;
    QString    newName_;
};

// Replace a module's sidecar entry with a changed copy (its look, pin, …)
// in one undo step. The members are not touched. Undo puts the old entry
// back. Steps on the same module with the same `mergeKey` (a colour being
// dragged around a picker) merge into one.
class UpdateModuleCommand : public QUndoCommand {
public:
    UpdateModuleCommand(core::Map& map, core::Module updated, const QString& text, int mergeKey = -1,
                        QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
    int id() const override { return mergeKey_; }
    bool mergeWith(const QUndoCommand* other) override;
private:
    core::Map&   map_;
    core::Module before_;
    core::Module after_;
    int          mergeKey_;
};

// Clone an existing module in-project: duplicates every member brick
// (fresh guids, same part numbers / orientation / altitude / layer) at an
// offset from the source, and registers a new Module entry over the
// clones. Cloning a module N times gives you N independent instances you
// can move/rotate separately.
class CloneModuleCommand : public QUndoCommand {
public:
    CloneModuleCommand(core::Map& map, QString sourceModuleId,
                       QPointF offsetStuds, QString newName,
                       QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
    const QString& newModuleId() const { return newModuleId_; }

private:
    core::Map& map_;
    QString    sourceModuleId_;
    QPointF    offsetStuds_;
    QString    newName_;
    QString    newModuleId_;

    // Populated on first redo so undo can reverse exactly.
    struct AppliedBrick { int layerIndex = -1; QString guid; };
    std::vector<AppliedBrick> appliedBricks_;
    bool captured_ = false;
};

// Dissolve a module: remove the sidecar entry so its members become
// ordinary independent bricks/texts/rulers on their own layers. Undo
// recreates the module entry with the original memberIds.
class FlattenModuleCommand : public QUndoCommand {
public:
    FlattenModuleCommand(core::Map& map, QString moduleId, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    QString    moduleId_;
    std::optional<core::Module> removed_;
    int insertIndex_ = -1;
};

// Re-scan a module from its source .bbm: delete current member bricks and
// re-import from the file with fresh guids. Useful when the upstream
// module file was edited and the user wants to pick up the change.
// Snapshots pre-state so undo fully restores the previous members.
class RescanModuleCommand : public QUndoCommand {
public:
    RescanModuleCommand(core::Map& map, int targetLayerIndex, QString moduleId,
                        std::vector<core::Brick> freshBricks,
                        QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    int         layerIndex_;
    QString     moduleId_;
    std::vector<core::Brick> freshBricks_;
    std::vector<core::Brick> oldBricks_;        // snapshot for undo
    QSet<QString> oldMemberIds_;
    bool captured_ = false;
};

// Load a .bbm, merge its brick layers into the current map (remapping guids
// to fresh ones to avoid collisions), and register the new bricks as a
// module. Preserves the source's per-layer structure: every distinct
// source-layer-name becomes (or matches) a brick layer in the host map,
// and each brick lands on its original layer. Undo removes every imported
// brick, every layer that was newly created for the import, and the
// module entry itself.
class ImportBbmAsModuleCommand : public QUndoCommand {
public:
    // One batch per source layer. layerName is matched against existing
    // brick-layer names in the host map; a new brick layer is created
    // when no match is found.
    struct LayerBatch {
        QString layerName;
        std::vector<core::Brick> bricks;
    };

    ImportBbmAsModuleCommand(core::Map& map,
                             QString sourcePath, QString moduleName,
                             std::vector<LayerBatch> batches,
                             QUndoCommand* parent = nullptr);

    // Back-compat: single-layer constructor for call sites that haven't
    // migrated to the batched version yet.
    ImportBbmAsModuleCommand(core::Map& map, int targetLayerIndex,
                             QString sourcePath, QString moduleName,
                             std::vector<core::Brick> bricks,
                             QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
    const QString& moduleId() const { return moduleId_; }

    // After redo() runs (the undo stack pushes call redo immediately),
    // returns every (layerIndex, guid) pair the command inserted. Lets
    // callers select the just-placed bricks so post-drop R/Shift+R or
    // arrow-nudge applies to the freshly-placed module.
    struct PlacedBrick { int layerIndex; QString guid; };
    QList<PlacedBrick> placedBricks() const {
        QList<PlacedBrick> out;
        for (const auto& a : applied_) {
            for (const auto& g : a.addedGuids) out.append({ a.layerIndex, g });
        }
        return out;
    }

private:
    core::Map& map_;
    QString sourcePath_;
    QString moduleId_;
    QString name_;
    std::vector<LayerBatch> batches_;

    // Populated on first redo so undo can exactly reverse the operation.
    struct AppliedLayer {
        QString  layerName;
        int      layerIndex = -1;
        bool     wasCreated = false;      // created by this command → remove on undo
        QList<QString> addedGuids;         // bricks we appended to this layer
    };
    std::vector<AppliedLayer> applied_;
    bool captured_ = false;
    std::vector<core::Brick> bricks_;
};

}
