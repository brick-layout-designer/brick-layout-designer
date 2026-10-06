#pragma once

#include "../core/Brick.h"
#include "../core/Group.h"
#include "../core/Module.h"

#include <QPointF>
#include <QSet>
#include <QString>
#include <QUndoCommand>

#include <optional>
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

// Set which parts a module has (parts placed while it's edited join it;
// parts taken out leave it). Undo puts the old list back.
class SetModuleMembersCommand : public QUndoCommand {
public:
    SetModuleMembersCommand(core::Map& map, QString moduleId, QSet<QString> members, const QString& text,
                            QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map&    map_;
    QString       moduleId_;
    QSet<QString> before_;
    QSet<QString> after_;
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
    struct LayerClone { int layerIndex = -1; std::vector<core::Brick> bricks; std::vector<core::Group> groups; };
    std::vector<LayerClone> clones_;
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
    // brick-layer names in the host map (ignoring case and the spaces
    // around it, edit/ModuleSheets.h). With no match the batch goes on
    // targetLayerGuid's layer (the answer to "Where should these go?",
    // ui/SheetChoiceDialog) when set, else on a new brick layer named
    // layerName.
    struct LayerBatch {
        QString layerName;
        std::vector<core::Brick> bricks;
        std::vector<core::Group> groups;  // the sets (groups) the bricks are in
        QString targetLayerGuid;          // where an unmatched batch goes; empty: a new layer
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
    // From the library: the new module is linked to it (call before pushing).
    void setLibrary(const QString& id, int version) { libraryId_ = id; libraryVersion_ = version; }

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
        QString  layerGuid;               // the host layer it went on (found again on redo)
        int      layerIndex = -1;
        bool     wasCreated = false;      // created by this command → remove on undo
        QList<QString> addedGuids;         // bricks we appended to this layer
    };
    std::vector<AppliedLayer> applied_;
    bool captured_ = false;
    std::vector<core::Brick> bricks_;
    QString libraryId_;
    int libraryVersion_ = 0;
};

// Update from library: the module's parts are replaced by a library
// version's (`batches`, already placed where the module is, each with its
// targetLayerGuid: the sheet its parts go on). The module keeps its id,
// name, look and pin, and is linked to `libraryId` at `libraryVersion`.
// One undo step; undo puts the old parts back where they were.
class ReplaceModulePartsCommand : public QUndoCommand {
public:
    ReplaceModulePartsCommand(core::Map& map, QString moduleId,
                              std::vector<ImportBbmAsModuleCommand::LayerBatch> batches,
                              QString libraryId, int libraryVersion, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
    // The new parts' guids (after the first redo).
    QSet<QString> newMembers() const;

private:
    core::Map& map_;
    QString moduleId_;
    std::vector<ImportBbmAsModuleCommand::LayerBatch> batches_;
    QString libraryId_;
    int libraryVersion_ = 0;
    // The old parts, where they were (layer guid, index), for undo.
    struct Removed { QString layerGuid; int index = 0; core::Brick brick; };
    std::vector<Removed> removed_;
    std::optional<core::Module> before_;
};

}
