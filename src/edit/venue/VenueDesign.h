#pragma once

// The Venue Designer's edits, snapping and undo as plain functions on a
// core::Venue (studs, x east, y south; the web repo's
// references/VENUE-MODEL.md). The twin of the web's venues/designer/
// model.ts, snap.ts and history.ts: every tool goes through these, and
// both apps' tests check the same cases.

#include "../../core/Venue.h"

#include <QPointF>
#include <QRectF>
#include <QString>

#include <deque>
#include <optional>

namespace bld::edit::venue {

enum class PartKind { Edge, Obstacle, Power, Note, Dimension };
struct Selection {
    PartKind kind = PartKind::Edge;
    int index = 0;
    bool operator==(const Selection&) const = default;
};

core::Venue emptyVenue(const QString& name = QStringLiteral("New venue"));

// ---- adding
core::Venue addEdge(core::Venue v, const QVector<QPointF>& points, core::EdgeKind kind = core::EdgeKind::Wall,
                    const QString& label = {});
// Four walls, one per side, clockwise from the north-west corner.
core::Venue addRoom(core::Venue v, QPointF a, QPointF b);
constexpr double kRailingThicknessStuds = 4.0;
// From two opposite corners (a railing: from its two ends, 4 studs thick).
core::Venue addObstacle(core::Venue v, core::ObstacleKind kind, QPointF a, QPointF b,
                        const QString& label = {});
core::Venue addPower(core::Venue v, QPointF at, bool floor);
core::Venue addNote(core::Venue v, QPointF at, const QString& text);
core::Venue addDimension(core::Venue v, QPointF from, QPointF to, const QString& label = {});
// Cut a door (kind Door) or opening (Open) into segment `seg` of wall
// `edge` from t0 to t1 (0–1): the wall splits into before / cut / after,
// both parts keeping its label and estimated flag. Unchanged when the edge
// isn't a wall or the cut is empty.
core::Venue cutOpening(core::Venue v, int edge, int seg, double t0, double t1, core::EdgeKind kind,
                       const QString& label = {});

// ---- changing
core::Venue movePart(core::Venue v, Selection sel, QPointF d);
core::Venue moveVertex(core::Venue v, Selection sel, int vertex, QPointF to);
// Every outline vertex at `at` moves to `to`, so walls meeting there stay joined.
core::Venue moveCorner(core::Venue v, QPointF at, QPointF to);
// Rectangular obstacle to w × h, keeping its north-west corner.
core::Venue resizeObstacle(core::Venue v, int index, double w, double h);
core::Venue deletePart(core::Venue v, Selection sel);
// A copy 2 ft to the south-east, and its selection.
std::pair<core::Venue, Selection> duplicatePart(core::Venue v, Selection sel);
int partCount(const core::Venue& v, PartKind kind);

// ---- looking
int estimateCount(const core::Venue& v);
struct RoomSize {
    double w = 0, h = 0, area = 0;
};
std::optional<RoomSize> roomSize(const core::Venue& v);
struct Hit {
    Selection sel;
    std::optional<int> vertex; // a corner or a measurement end
    std::optional<int> seg;    // an edge segment, and where along it (0–1)
    double t = 0;
};
// The topmost part under `p` within `tol` studs: notes, power,
// measurements, obstacles (inside), then edges.
std::optional<Hit> hitTest(const core::Venue& v, QPointF p, double tol);

// ---- geometry
double dist(QPointF a, QPointF b);
double polylineLength(const QVector<QPointF>& poly);
struct SegDist {
    double d = 0, t = 0;
};
SegDist segDist(QPointF p, QPointF a, QPointF b);
bool pointInPolygon(QPointF p, const QVector<QPointF>& poly);
QRectF bounds(const QVector<QPointF>& pts);

// ---- snapping
enum class SnapKind { Corner, Wall, Angle, Grid, None };
struct SnapOptions {
    std::optional<QPointF> from; // start of the current line
    double stepStuds = 0;        // length / grid step (0: none)
    double angleStepDeg = 0;     // from `from` (0: none)
    double tolStuds = 0;
    bool toVenue = true;
};
constexpr double kDefaultStepStuds = 38.09814081 / 12.0; // 1″
struct Snapped {
    QPointF pt;
    SnapKind kind = SnapKind::None;
};
Snapped snapPoint(const core::Venue& v, QPointF p, const SnapOptions& o);
// The end of a line exactly `length` long from `from` towards `toward`, in the snapped direction.
QPointF pointAtLength(QPointF from, QPointF toward, double length, double angleStepDeg);

// ---- undo
constexpr int kHistoryLimit = 200;
struct History {
    std::deque<core::Venue> past;
    core::Venue present;
    std::deque<core::Venue> future;
};
History commit(History h, core::Venue next);
History undo(History h);
History redo(History h);

} // namespace bld::edit::venue
