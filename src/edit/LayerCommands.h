#pragma once

#include "../core/Layer.h"

#include <QColor>
#include <QDate>
#include <QString>
#include <QUndoCommand>

#include <memory>

namespace bld::core { class Map; struct ColorSpec; }

namespace bld::edit {

// Insert a new layer of the given kind at the given index (-1 = append).
// Redo creates with a fresh GUID + default name; undo removes + retains a
// deep copy so a second redo restores identical layer content.
class AddLayerCommand : public QUndoCommand {
public:
    AddLayerCommand(core::Map& map, core::LayerKind kind, int insertAt = -1,
                    QString name = {}, QUndoCommand* parent = nullptr);
    ~AddLayerCommand() override;
    void undo() override;
    void redo() override;
    int  insertedIndex() const { return insertedIndex_; }

private:
    core::Map& map_;
    core::LayerKind kind_;
    int insertAt_;
    QString name_;
    int insertedIndex_ = -1;
    // Holds the layer when it's detached from the map (after undo). Empty
    // on first construction; populated by undo, drained by next redo.
    std::unique_ptr<core::Layer> detached_;
};

// Delete a layer by index. Owns the removed layer between redo/undo.
class DeleteLayerCommand : public QUndoCommand {
public:
    DeleteLayerCommand(core::Map& map, int index, QUndoCommand* parent = nullptr);
    ~DeleteLayerCommand() override;
    void undo() override;
    void redo() override;

private:
    core::Map& map_;
    int index_;
    std::unique_ptr<core::Layer> removed_;
};

// Swap a layer with its neighbour. Delta = +1 moves toward the end (up in
// BlueBrick's visual sense), -1 toward the start.
class MoveLayerCommand : public QUndoCommand {
public:
    MoveLayerCommand(core::Map& map, int index, int delta, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    core::Map& map_;
    int index_;
    int delta_;
};

// Rename a layer.
class RenameLayerCommand : public QUndoCommand {
public:
    RenameLayerCommand(core::Map& map, int index, QString newName,
                       QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    core::Map& map_;
    int index_;
    QString oldName_;
    QString newName_;
};

// Change a layer's transparency (0..100 percent). Undo restores the prior
// value. No-op if before == after.
class SetLayerTransparencyCommand : public QUndoCommand {
public:
    SetLayerTransparencyCommand(core::Map& map, int index, int newTransparency,
                                QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    core::Map& map_;
    int index_;
    int before_;
    int after_;
};

// ---------- Map-level metadata commands ----------

// Shift a grid layer's cell-index origin by whole cells (BlueBrick's
// MoveGridOrigin).
class MoveGridOriginCommand : public QUndoCommand {
public:
    MoveGridOriginCommand(core::Map& map, int layerIndex, int dx, int dy, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    void shift(int dx, int dy);
    core::Map& map_;
    int layerIndex_;
    int dx_, dy_;
};

class ChangeBackgroundColorCommand : public QUndoCommand {
public:
    ChangeBackgroundColorCommand(core::Map& map, const core::ColorSpec& newColor,
                                 QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    core::ColorSpec oldColor_;
    core::ColorSpec newColor_;
};

class ChangeGeneralInfoCommand : public QUndoCommand {
public:
    struct Info {
        QString author;
        QString lug;
        QString event;
        QDate   date;
        QString comment;
    };
    ChangeGeneralInfoCommand(core::Map& map, Info next, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    Info before_;
    Info after_;
};

}
