#pragma once

#include "../core/TextCell.h"

#include <QPointF>
#include <QUndoCommand>
#include <QString>

#include <optional>

namespace bld::core { class Map; }

namespace bld::edit {

// Append a text cell to a text layer. Redo pushes it; undo removes by guid.
class AddTextCellCommand : public QUndoCommand {
public:
    AddTextCellCommand(core::Map& map, int layerIndex, core::TextCell cell,
                       QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
    const QString& cellGuid() const { return cell_.guid; }

private:
    core::Map& map_;
    int layerIndex_;
    core::TextCell cell_;
};

// Remove a text cell by guid, remembering its position for undo.
class DeleteTextCellCommand : public QUndoCommand {
public:
    DeleteTextCellCommand(core::Map& map, int layerIndex, QString cellGuid,
                          QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    int layerIndex_;
    QString cellGuid_;
    std::optional<core::TextCell> removed_;
    int insertIndex_ = -1;
};

// Move a text cell by `deltaStuds` (BlueBrick MoveText).
class MoveTextCellCommand : public QUndoCommand {
public:
    MoveTextCellCommand(core::Map& map, int layerIndex, QString cellGuid, QPointF deltaStuds,
                        QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    void shift(QPointF by);
    core::Map& map_;
    int layerIndex_;
    QString cellGuid_;
    QPointF delta_;
};

// Replace a text cell's text content. Font / color / alignment stay put.
class EditTextCellTextCommand : public QUndoCommand {
public:
    EditTextCellTextCommand(core::Map& map, int layerIndex, QString cellGuid, QString newText,
                            QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    int layerIndex_;
    QString cellGuid_;
    QString newText_;
    QString oldText_;
};

}
