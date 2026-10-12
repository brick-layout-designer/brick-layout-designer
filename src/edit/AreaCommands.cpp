#include "AreaCommands.h"

#include "../core/Layer.h"
#include "../core/LayerArea.h"
#include "../core/Map.h"

#include <QObject>

namespace bld::edit {

namespace {

core::LayerArea* areaLayer(core::Map& m, int idx) {
    if (idx < 0 || idx >= static_cast<int>(m.layers().size())) return nullptr;
    auto* L = m.layers()[idx].get();
    return (L && L->kind() == core::LayerKind::Area)
        ? static_cast<core::LayerArea*>(L)
        : nullptr;
}

// Find the index of the cell at (x,y) in the layer's cells vector, or -1.
int findCell(const core::LayerArea& L, int x, int y) {
    for (int i = 0; i < static_cast<int>(L.cells.size()); ++i) {
        if (L.cells[i].x == x && L.cells[i].y == y) return i;
    }
    return -1;
}

void setOrErase(core::LayerArea& L, int x, int y, const std::optional<QColor>& c) {
    const int i = findCell(L, x, y);
    if (c) {
        if (i >= 0) L.cells[i].color = *c;
        else        L.cells.push_back({ x, y, *c });
    } else if (i >= 0) {
        L.cells.erase(L.cells.begin() + i);
    }
}

}

PaintAreaCellsCommand::PaintAreaCellsCommand(core::Map& map, int layerIndex,
                                              std::vector<Change> changes, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), layerIndex_(layerIndex), changes_(std::move(changes)) {
    const int n = static_cast<int>(changes_.size());
    setText(QObject::tr("Paint %1 area cell(s)").arg(n));
}

void PaintAreaCellsCommand::redo() {
    auto* L = areaLayer(map_, layerIndex_);
    if (!L) return;
    if (!haveSnapshot_) {
        prev_.reserve(changes_.size());
        for (const auto& c : changes_) {
            Prev p{ c.x, c.y, std::nullopt };
            const int i = findCell(*L, c.x, c.y);
            if (i >= 0) p.oldColor = L->cells[i].color;
            prev_.push_back(p);
        }
        haveSnapshot_ = true;
    }
    for (const auto& c : changes_) setOrErase(*L, c.x, c.y, c.newColor);
}

void PaintAreaCellsCommand::undo() {
    auto* L = areaLayer(map_, layerIndex_);
    if (!L) return;
    for (const auto& p : prev_) setOrErase(*L, p.x, p.y, p.oldColor);
}

// ----- MoveAreaCommand -----

MoveAreaCommand::MoveAreaCommand(core::Map& map, int layerIndex, int dxCells, int dyCells, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), layerIndex_(layerIndex), dx_(dxCells), dy_(dyCells) {
    setText(QObject::tr("Move area"));
}

void MoveAreaCommand::shift(int dx, int dy) {
    if (auto* L = areaLayer(map_, layerIndex_))
        for (auto& c : L->cells) {
            c.x += dx;
            c.y += dy;
        }
}

void MoveAreaCommand::redo() { shift(dx_, dy_); }

void MoveAreaCommand::undo() { shift(-dx_, -dy_); }

}
