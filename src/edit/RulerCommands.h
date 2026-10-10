#pragma once

#include "../core/LayerRuler.h"

#include <QUndoCommand>

#include <optional>
#include <vector>

namespace bld::core { class Map; }

namespace bld::edit {

// Append a ruler item (linear or circular) to a LayerRuler at a given index.
class AddRulerItemCommand : public QUndoCommand {
public:
    AddRulerItemCommand(core::Map& map, int layerIndex, core::LayerRuler::AnyRuler ruler,
                        QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    int layerIndex_;
    core::LayerRuler::AnyRuler ruler_;
    // The guid carried by the stored ruler (linear.guid or circular.guid)
    // — used as the identity for undo removal.
    QString guidForUndo_;
};

// Delete a ruler item by guid. Remembers its original position for undo.
class DeleteRulerItemCommand : public QUndoCommand {
public:
    DeleteRulerItemCommand(core::Map& map, int layerIndex, QString guid,
                           QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    int layerIndex_;
    QString guid_;
    std::optional<core::LayerRuler::AnyRuler> removed_;
    int index_ = -1;
};

// Translate a ruler by a (dx, dy) stud delta. Moves point1 + point2 for
// linear rulers, center for circular. Undo subtracts the same delta.
class MoveRulerItemCommand : public QUndoCommand {
public:
    MoveRulerItemCommand(core::Map& map, int layerIndex, QString rulerGuid,
                         QPointF deltaStuds, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    int layerIndex_;
    QString rulerGuid_;
    QPointF delta_;
};

// Move just one endpoint of a ruler. Linear: endpointIndex 0 → point1,
// 1 → point2. Circular: endpointIndex 0 → centre (radius preserved),
// 1 → adjusts radius so the circle's edge passes through `toStuds`.
// Used by the in-canvas endpoint-drag handles for inline geometry edits
// without opening the Properties dialog.
class MoveRulerEndpointCommand : public QUndoCommand {
public:
    MoveRulerEndpointCommand(core::Map& map, int layerIndex, QString rulerGuid,
                             int endpointIndex, QPointF toStuds,
                             QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    int layerIndex_;
    QString rulerGuid_;
    int endpointIndex_;
    QPointF before_;     // cached on first redo
    QPointF after_;
    bool captured_ = false;
};

// Attach or detach a ruler endpoint to a brick. `endpointIndex` is 0 or 1 on
// linear rulers (point1/point2), ignored on circular rulers (the circle
// centre is always the attached endpoint). Passing an empty brickGuid
// detaches. Upstream terminology: Attach / Detach ruler.
class AttachRulerCommand : public QUndoCommand {
public:
    AttachRulerCommand(core::Map& map, int layerIndex, QString rulerGuid,
                       int endpointIndex, QString brickGuid,
                       QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    int layerIndex_;
    QString rulerGuid_;
    int endpointIndex_;
    QString before_;
    QString after_;
};

// Edit an existing ruler's base properties (color, line thickness, display
// toggles, guideline color/thickness/dash, unit, measure font/color). The
// ruler's geometry (endpoints / centre / radius / attachments) is not
// touched.
class EditRulerItemCommand : public QUndoCommand {
public:
    struct BaseProps {
        core::ColorSpec color;
        float lineThickness = 1.0f;
        bool  displayDistance = true;
        bool  displayUnit = true;
        core::ColorSpec guidelineColor;
        float guidelineThickness = 1.0f;
        std::vector<float> guidelineDashPattern;
        int   unit = 0;
        core::FontSpec measureFont;
        core::ColorSpec measureFontColor;
    };

    EditRulerItemCommand(core::Map& map, int layerIndex, QString guid, BaseProps next,
                         QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    core::Map& map_;
    int layerIndex_;
    QString guid_;
    BaseProps before_;
    BaseProps after_;
};

}
