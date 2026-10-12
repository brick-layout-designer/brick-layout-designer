#pragma once

#include <QColor>
#include <QUndoCommand>

#include <optional>
#include <vector>

namespace bld::core { class Map; }

namespace bld::edit {

// Paint or erase one or more cells on a LayerArea in a single undoable step.
// Each entry either sets the cell color (newColor has value) or erases it
// (newColor is std::nullopt). The command snapshots each cell's previous
// color (or absence) on first redo so undo restores the prior state exactly.
class PaintAreaCellsCommand : public QUndoCommand {
public:
    struct Change {
        int x = 0;
        int y = 0;
        std::optional<QColor> newColor;  // nullopt = erase
    };

    PaintAreaCellsCommand(core::Map& map, int layerIndex, std::vector<Change> changes,
                          QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    core::Map& map_;
    int layerIndex_;
    std::vector<Change> changes_;

    // Per-entry snapshot taken on first redo.
    struct Prev { int x, y; std::optional<QColor> oldColor; };
    std::vector<Prev> prev_;
    bool haveSnapshot_ = false;
};

// BlueBrick MoveArea: shift every cell of an area sheet by whole cells.
class MoveAreaCommand : public QUndoCommand {
public:
    MoveAreaCommand(core::Map& map, int layerIndex, int dxCells, int dyCells, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    void shift(int dx, int dy);
    core::Map& map_;
    int layerIndex_;
    int dx_;
    int dy_;
};

}
