#pragma once

// The Venue Designer's state and what each tool does with the pointer and
// the keyboard, as one reducer (no widgets) so the tools are tested
// directly: the twin of the web's venues/designer/designerState.ts. The
// window turns mouse events into world points (studs) and passes them here.

#include "VenueDesign.h"
#include "VenueUnits.h"

#include <QLineF>
#include <QRectF>
#include <QString>

#include <array>
#include <optional>
#include <variant>

namespace bld::edit::venue {

enum class Tool {
    Select,
    Wall,
    Room,
    Door,
    Opening,
    Column,
    Stairs,
    Elevator,
    Counter,
    Railing,
    Power,
    Note,
    Measure,
    Calibrate
};

struct ToolInfo {
    Tool tool;
    const char* label;
    char key;
    const char* hint;
};
// The palette, in order, with one-key shortcuts (Calibrate isn't in it).
const std::array<ToolInfo, 13>& tools();

enum class Layer { Plan, Obstacles, Power, Dimensions, Notes, Estimates };

struct DesignerState {
    History history;
    std::optional<Selection> selection;
    Tool tool = Tool::Select;
    QVector<QPointF> draft; // points placed by the current tool
    struct Cut {
        int edge, seg;
        double t0;
    };
    std::optional<Cut> cut; // door / opening: the wall being cut
    QString typed;          // a length or "W x D" typed while drawing
    std::optional<QPointF> pointer;
    SnapKind snapKind = SnapKind::None;
    LengthUnit unit = LengthUnit::FeetInches;
    bool snap = true;
    std::array<bool, 6> show{ true, true, true, true, true, true };
    struct Drag {
        Selection sel;
        std::optional<int> vertex;
        QPointF from;
        core::Venue base;
        bool moved = false;
    };
    std::optional<Drag> drag;
    QVector<QPointF> calibration; // floor plan: the two points clicked
    QString message;              // for the status line

    const core::Venue& venue() const { return history.present; }
};

DesignerState initialState(core::Venue v);

namespace act {
struct SetTool {
    Tool tool;
};
struct Move {
    QPointF at;
    double tol;
    bool free;
};
struct Down {
    QPointF at;
    double tol;
    bool free;
};
struct Up {};
struct Type {
    QString text;
};
struct Enter {};
struct Escape {};
struct Delete {};
struct Duplicate {};
struct Undo {};
struct Redo {};
struct Select {
    std::optional<Selection> selection;
};
struct Edit {
    core::Venue venue;
};
struct Unit {
    LengthUnit unit;
};
struct Snap {
    bool on;
};
struct Show {
    Layer layer;
    bool on;
};
} // namespace act
using Action = std::variant<act::SetTool, act::Move, act::Down, act::Up, act::Type, act::Enter, act::Escape,
                            act::Delete, act::Duplicate, act::Undo, act::Redo, act::Select, act::Edit,
                            act::Unit, act::Snap, act::Show>;

DesignerState reduce(DesignerState s, const Action& a);

// "40' x 30'" → {w, d}; one length → {len}; nullopt otherwise.
std::optional<QVector<double>> parseSize(const QString& text, LengthUnit unit);

// The line or shape being drawn, for the view to preview.
struct Preview {
    std::optional<QLineF> line;
    std::optional<QRectF> rect;
    std::optional<QLineF> cut;
    double length = 0;
};
Preview preview(const DesignerState& s);

} // namespace bld::edit::venue
