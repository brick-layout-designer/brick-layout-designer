#include "TextCommands.h"

#include "../core/Layer.h"
#include "../core/LayerText.h"
#include "../core/Map.h"

#include <QObject>

namespace bld::edit {

namespace {

core::LayerText* textLayer(core::Map& m, int idx) {
    if (idx < 0 || idx >= static_cast<int>(m.layers().size())) return nullptr;
    auto* L = m.layers()[idx].get();
    return (L && L->kind() == core::LayerKind::Text)
        ? static_cast<core::LayerText*>(L)
        : nullptr;
}

int indexByGuid(const core::LayerText& L, const QString& guid) {
    for (int i = 0; i < static_cast<int>(L.textCells.size()); ++i) {
        if (L.textCells[i].guid == guid) return i;
    }
    return -1;
}

}

// ----- AddTextCellCommand -----

AddTextCellCommand::AddTextCellCommand(core::Map& map, int layerIndex, core::TextCell cell,
                                       QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), layerIndex_(layerIndex), cell_(std::move(cell)) {
    setText(QObject::tr("Add text '%1'").arg(cell_.text));
}

void AddTextCellCommand::redo() {
    if (auto* L = textLayer(map_, layerIndex_)) L->textCells.push_back(cell_);
}

void AddTextCellCommand::undo() {
    auto* L = textLayer(map_, layerIndex_);
    if (!L) return;
    const int i = indexByGuid(*L, cell_.guid);
    if (i >= 0) L->textCells.erase(L->textCells.begin() + i);
}

// ----- DeleteTextCellCommand -----

DeleteTextCellCommand::DeleteTextCellCommand(core::Map& map, int layerIndex, QString cellGuid,
                                             QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), layerIndex_(layerIndex),
      cellGuid_(std::move(cellGuid)) {
    setText(QObject::tr("Delete text"));
}

void DeleteTextCellCommand::redo() {
    auto* L = textLayer(map_, layerIndex_);
    if (!L) { removed_.reset(); return; }
    const int i = indexByGuid(*L, cellGuid_);
    if (i < 0) { removed_.reset(); return; }
    removed_ = L->textCells[i];
    insertIndex_ = i;
    L->textCells.erase(L->textCells.begin() + i);
}

void DeleteTextCellCommand::undo() {
    if (!removed_) return;
    auto* L = textLayer(map_, layerIndex_);
    if (!L) return;
    const int idx = std::min<int>(insertIndex_, static_cast<int>(L->textCells.size()));
    L->textCells.insert(L->textCells.begin() + idx, *removed_);
}

// ----- EditTextCellTextCommand -----

EditTextCellTextCommand::EditTextCellTextCommand(core::Map& map, int layerIndex, QString cellGuid,
                                                  QString newText, QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), layerIndex_(layerIndex),
      cellGuid_(std::move(cellGuid)), newText_(std::move(newText)) {
    if (auto* L = textLayer(map_, layerIndex_)) {
        const int i = indexByGuid(*L, cellGuid_);
        if (i >= 0) oldText_ = L->textCells[i].text;
    }
    setText(QObject::tr("Edit text"));
}

void EditTextCellTextCommand::redo() {
    if (auto* L = textLayer(map_, layerIndex_)) {
        const int i = indexByGuid(*L, cellGuid_);
        if (i >= 0) L->textCells[i].text = newText_;
    }
}

void EditTextCellTextCommand::undo() {
    if (auto* L = textLayer(map_, layerIndex_)) {
        const int i = indexByGuid(*L, cellGuid_);
        if (i >= 0) L->textCells[i].text = oldText_;
    }
}

// ----- MoveTextCellCommand -----

MoveTextCellCommand::MoveTextCellCommand(core::Map& map, int layerIndex, QString cellGuid, QPointF deltaStuds,
                                         QUndoCommand* parent)
    : QUndoCommand(parent), map_(map), layerIndex_(layerIndex), cellGuid_(std::move(cellGuid)), delta_(deltaStuds) {
    setText(QObject::tr("Move text"));
}

void MoveTextCellCommand::shift(QPointF by) {
    if (auto* L = textLayer(map_, layerIndex_)) {
        const int i = indexByGuid(*L, cellGuid_);
        if (i >= 0) L->textCells[i].displayArea.translate(by);
    }
}

void MoveTextCellCommand::redo() { shift(delta_); }

void MoveTextCellCommand::undo() { shift(-delta_); }

}
