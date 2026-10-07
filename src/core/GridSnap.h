#pragma once

// Grid snap, as BlueBrick does it (MapData/Layer.cs snapToGrid and
// updateSnapMargin, MapData/LayerBrick.cs getMovedSnapPoint). The web
// has the same functions in apps/web/src/editor/gridSnap.ts, and both
// apps run the cases in fixtures/grid-snap-vectors.json.
//
// A part snaps by its snap corner: the top-left of its display area plus
// the offset its <SnapMargin> gives at its orientation. A 9V straight is
// 17 studs wide with half a stud of margin each side, so its 16-stud
// rails sit on the grid, not its box.
//
// Points (ruler ends, label corners, text and venue points) snap to the
// nearest grid point.

#include <QPointF>
#include <QtMath>

#include <cmath>

namespace bld::gridsnap {

// A part's <SnapMargin>, in studs.
struct Margin {
    double left = 0.0, right = 0.0, top = 0.0, bottom = 0.0;
    bool isNull() const { return left == 0.0 && right == 0.0 && top == 0.0 && bottom == 0.0; }
    bool operator==(const Margin&) const = default;
};

// From the top-left of a part's display area to its snap corner, at
// `orientationDeg` (BlueBrick's updateSnapMargin, its branches included).
inline QPointF snapOffset(const Margin& m, double orientationDeg) {
    if (m.isNull()) return {};
    const double a = orientationDeg * M_PI / 180.0;
    double c = std::cos(a);
    double s = std::sin(a);
    double x = 0.0, y = 0.0;
    if (c > 0) {
        x = m.left * c;
        y = m.top * c;
    } else {
        c = -c;
        x = m.right * c;
        y = m.bottom * c;
    }
    if (s > 0) {
        x += m.bottom * s;
        y += m.left * s;
    } else {
        s = -s;
        x += m.top * s;
        y += m.right * s;
    }
    return { x, y };
}

// So a value that is a whole number of steps apart from rounding noise
// counts as one.
inline constexpr double kEpsilon = 1e-6;

// The grid line at or before `v`.
inline double floorToStep(double v, double step) {
    return std::floor(v / step + kEpsilon) * step;
}

// The nearest grid line; halfway goes to the even step (C#'s Math.Round,
// which BlueBrick's centred snap uses).
inline double roundToStep(double v, double step) {
    const double q = v / step;
    double r = std::floor(q + 0.5);
    if (std::abs(q + 0.5 - r) < kEpsilon && std::fmod(r, 2.0) != 0.0) r -= 1.0;
    return r * step;
}

// The nearest grid point to `p`; `p` itself when the step is 0 (off).
inline QPointF snapPoint(QPointF p, double step) {
    if (step <= 0.0) return p;
    return { roundToStep(p.x(), step), roundToStep(p.y(), step) };
}

// Where a dragged part's snap corner lands: the pointer is at `mouse`,
// and would have the corner at `rawCorner` (both studs). The corner keeps
// the grid steps it had from the pointer when grabbed, so it lands on the
// grid and moves a whole step as the pointer crosses a grid line
// (BlueBrick's getMovedSnapPoint with no connection in reach).
inline QPointF dragCorner(QPointF mouse, QPointF rawCorner, double step) {
    if (step <= 0.0) return rawCorner;
    return { floorToStep(mouse.x(), step) - floorToStep(mouse.x() - rawCorner.x(), step),
             floorToStep(mouse.y(), step) - floorToStep(mouse.y() - rawCorner.y(), step) };
}

// The shift that takes a dragged part from where the pointer has it onto
// the grid.
inline QPointF dragShift(QPointF mouse, QPointF rawCorner, double step) {
    return dragCorner(mouse, rawCorner, step) - rawCorner;
}

}  // namespace bld::gridsnap
