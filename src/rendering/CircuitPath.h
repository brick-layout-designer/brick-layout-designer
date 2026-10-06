#pragma once

// The path an electric circuit takes through a part, so the circuit
// overlay follows the track instead of cutting straight across it.
//
// BlueBrick (LayerBrick.cs draw) joins a circuit's two connection points
// with straight lines. Here the centreline is a biarc: two circular arcs,
// tangent to each connection's direction and to each other. A standard
// curve comes out as one arc (2867: R40), a straight as a line, and a
// switch branch or a crossover diagonal as an S of two arcs. The web
// (apps/web/src/editor/render/circuitPath.ts) computes the same points.

#include <QPointF>

#include <vector>

namespace bld::rendering {

struct CircuitPathPoint {
    QPointF p;       // on the centreline, studs
    QPointF normal;  // unit, BlueBrick's (-dir.y, dir.x) (y down)
    double  s = 0.0; // distance along the path from the start, studs
};

// The centreline from p1 to p2. `angle1`/`angle2` are the connections'
// world angles in degrees (part orientation + connection angle), pointing
// out of the part: the path leaves p1 against angle1 and reaches p2 along
// angle2. Arcs are sampled every 2 degrees at most.
std::vector<CircuitPathPoint> circuitPath(QPointF p1, double angle1, QPointF p2, double angle2);

// The rail on one side of the path: each point moved `offset` studs along
// its normal (BlueBrick: +2.5 for the first line, -2.5 for the second).
std::vector<QPointF> offsetPath(const std::vector<CircuitPathPoint>& path, double offset);

// The part of the path between distances s0 and s1, `offset` studs to the side.
std::vector<QPointF> offsetPathBetween(const std::vector<CircuitPathPoint>& path, double offset, double s0, double s1);

// The point and normal at distance s along the path.
CircuitPathPoint pointAt(const std::vector<CircuitPathPoint>& path, double s);

// BlueBrick's constants (studs).
constexpr double kCircuitRailOffset = 2.5;    // ELECTRIC_WIDTH: half the 9V rail gauge
constexpr double kCircuitPenWidth   = 0.5;
constexpr double kCircuitCutterGap  = 2.25 * kCircuitRailOffset;  // 862AC01/02: line 2 stops this far from each end
constexpr double kCircuitCutterPen  = 1.5;    // the orange bars, and the shortcut sign
constexpr double kCircuitShortcutSize = 3.0;  // SHORTCUT_WIDTH: the sign's half size

}  // namespace bld::rendering
