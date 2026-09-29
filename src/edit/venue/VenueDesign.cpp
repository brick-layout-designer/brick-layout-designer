#include "VenueDesign.h"

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace bld::edit::venue {

namespace {

constexpr double kEps = 0.001;

QVector<QPointF> rectPoly(QPointF a, QPointF b) {
    const double x0 = std::min(a.x(), b.x()), x1 = std::max(a.x(), b.x()), y0 = std::min(a.y(), b.y()),
                 y1 = std::max(a.y(), b.y());
    return { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } };
}

QVector<QPointF> thickLine(QPointF a, QPointF b, double w) {
    const double len = dist(a, b);
    if (len < kEps) return {};
    const QPointF n(-(b.y() - a.y()) / len * (w / 2), (b.x() - a.x()) / len * (w / 2));
    return { a + n, b + n, b - n, a - n };
}

QString defaultLabel(core::ObstacleKind kind) {
    switch (kind) {
    case core::ObstacleKind::Column: return QStringLiteral("column");
    case core::ObstacleKind::Stairs: return QStringLiteral("stairs");
    case core::ObstacleKind::Elevator: return QStringLiteral("elevator");
    case core::ObstacleKind::Counter: return QStringLiteral("counter");
    case core::ObstacleKind::Railing: return QStringLiteral("railing");
    case core::ObstacleKind::Other: break;
    }
    return QStringLiteral("obstacle");
}

QPointF lerp(QPointF a, QPointF b, double t) {
    return a + (b - a) * t;
}

} // namespace

core::Venue emptyVenue(const QString& name) {
    core::Venue v;
    v.name = name;
    return v;
}

core::Venue addEdge(core::Venue v, const QVector<QPointF>& points, core::EdgeKind kind,
                    const QString& label) {
    if (points.size() < 2) return v;
    core::VenueEdge e;
    e.polyline = points;
    e.kind = kind;
    e.doorWidthStuds = kind == core::EdgeKind::Door ? polylineLength(points) : 0.0;
    e.label = label;
    v.edges.append(e);
    return v;
}

core::Venue addRoom(core::Venue v, QPointF a, QPointF b) {
    const auto c = rectPoly(a, b);
    if (c[1].x() - c[0].x() < kEps || c[2].y() - c[1].y() < kEps) return v;
    const QString sides[] = { QStringLiteral("north wall"), QStringLiteral("east wall"),
                              QStringLiteral("south wall"), QStringLiteral("west wall") };
    for (int i = 0; i < 4; ++i)
        v = addEdge(std::move(v), { c[i], c[(i + 1) % 4] }, core::EdgeKind::Wall, sides[i]);
    return v;
}

core::Venue addObstacle(core::Venue v, core::ObstacleKind kind, QPointF a, QPointF b, const QString& label) {
    QVector<QPointF> poly;
    if (kind == core::ObstacleKind::Railing) {
        poly = thickLine(a, b, kRailingThicknessStuds);
        if (poly.isEmpty()) return v;
    } else {
        poly = rectPoly(a, b);
        if (poly[1].x() - poly[0].x() < kEps || poly[2].y() - poly[1].y() < kEps) return v;
    }
    core::VenueObstacle ob;
    ob.polygon = poly;
    ob.label = label.isEmpty() ? defaultLabel(kind) : label;
    ob.kind = kind;
    if (kind == core::ObstacleKind::Stairs) ob.upDegrees = 270.0;
    v.obstacles.append(ob);
    return v;
}

core::Venue addPower(core::Venue v, QPointF at, bool floor) {
    core::VenuePower p;
    p.pos = at;
    p.floor = floor;
    v.power.append(p);
    return v;
}

core::Venue addNote(core::Venue v, QPointF at, const QString& text) {
    if (text.trimmed().isEmpty()) return v;
    core::VenueNote n;
    n.pos = at;
    n.text = text.trimmed();
    v.notes.append(n);
    return v;
}

core::Venue addDimension(core::Venue v, QPointF from, QPointF to, const QString& label) {
    if (dist(from, to) < kEps) return v;
    core::VenueDimension d;
    d.from = from;
    d.to = to;
    d.label = label;
    v.dimensions.append(d);
    return v;
}

core::Venue cutOpening(core::Venue v, int edge, int seg, double t0, double t1, core::EdgeKind kind,
                       const QString& label) {
    if (edge < 0 || edge >= v.edges.size()) return v;
    const core::VenueEdge e = v.edges[edge];
    if (e.kind != core::EdgeKind::Wall || seg < 0 || seg >= e.polyline.size() - 1) return v;
    const double lo = std::max(0.0, std::min(t0, t1)), hi = std::min(1.0, std::max(t0, t1));
    const QPointF a = e.polyline[seg], b = e.polyline[seg + 1];
    const QPointF p0 = lerp(a, b, lo), p1 = lerp(a, b, hi);
    if (dist(p0, p1) < kEps) return v;
    QVector<core::VenueEdge> parts;
    QVector<QPointF> before = e.polyline.mid(0, seg + 1);
    before.append(p0);
    if (polylineLength(before) >= kEps) {
        core::VenueEdge w = e;
        w.polyline = before;
        parts.append(w);
    }
    core::VenueEdge cut;
    cut.kind = kind;
    cut.doorWidthStuds = kind == core::EdgeKind::Door ? dist(p0, p1) : 0.0;
    cut.label = label;
    cut.polyline = { p0, p1 };
    cut.estimated = e.estimated;
    parts.append(cut);
    QVector<QPointF> after{ p1 };
    after += e.polyline.mid(seg + 1);
    if (polylineLength(after) >= kEps) {
        core::VenueEdge w = e;
        w.polyline = after;
        parts.append(w);
    }
    v.edges.remove(edge);
    for (int i = 0; i < parts.size(); ++i) v.edges.insert(edge + i, parts[i]);
    return v;
}

core::Venue movePart(core::Venue v, Selection s, QPointF d) {
    switch (s.kind) {
    case PartKind::Edge:
        if (s.index < v.edges.size())
            for (auto& p : v.edges[s.index].polyline) p += d;
        break;
    case PartKind::Obstacle:
        if (s.index < v.obstacles.size())
            for (auto& p : v.obstacles[s.index].polygon) p += d;
        break;
    case PartKind::Power:
        if (s.index < v.power.size()) v.power[s.index].pos += d;
        break;
    case PartKind::Note:
        if (s.index < v.notes.size()) v.notes[s.index].pos += d;
        break;
    case PartKind::Dimension:
        if (s.index < v.dimensions.size()) {
            v.dimensions[s.index].from += d;
            v.dimensions[s.index].to += d;
        }
        break;
    }
    return v;
}

core::Venue moveVertex(core::Venue v, Selection s, int vertex, QPointF to) {
    if (s.kind == PartKind::Edge && s.index < v.edges.size()) {
        auto& e = v.edges[s.index];
        if (vertex < 0 || vertex >= e.polyline.size()) return v;
        e.polyline[vertex] = to;
        if (e.kind == core::EdgeKind::Door) e.doorWidthStuds = polylineLength(e.polyline);
    } else if (s.kind == PartKind::Obstacle && s.index < v.obstacles.size()) {
        auto& o = v.obstacles[s.index];
        if (vertex < 0 || vertex >= o.polygon.size()) return v;
        o.polygon[vertex] = to;
    } else if (s.kind == PartKind::Dimension && s.index < v.dimensions.size()) {
        (vertex == 0 ? v.dimensions[s.index].from : v.dimensions[s.index].to) = to;
    }
    return v;
}

core::Venue moveCorner(core::Venue v, QPointF at, QPointF to) {
    for (auto& e : v.edges) {
        bool hit = false;
        for (auto& p : e.polyline)
            if (dist(p, at) < kEps) {
                p = to;
                hit = true;
            }
        if (hit && e.kind == core::EdgeKind::Door) e.doorWidthStuds = polylineLength(e.polyline);
    }
    return v;
}

core::Venue resizeObstacle(core::Venue v, int index, double w, double h) {
    if (index < 0 || index >= v.obstacles.size() || w <= 0 || h <= 0) return v;
    const QRectF b = bounds(v.obstacles[index].polygon);
    v.obstacles[index].polygon = rectPoly(b.topLeft(), b.topLeft() + QPointF(w, h));
    return v;
}

core::Venue deletePart(core::Venue v, Selection s) {
    switch (s.kind) {
    case PartKind::Edge:
        if (s.index < v.edges.size()) v.edges.remove(s.index);
        break;
    case PartKind::Obstacle:
        if (s.index < v.obstacles.size()) v.obstacles.remove(s.index);
        break;
    case PartKind::Power:
        if (s.index < v.power.size()) v.power.remove(s.index);
        break;
    case PartKind::Note:
        if (s.index < v.notes.size()) v.notes.remove(s.index);
        break;
    case PartKind::Dimension:
        if (s.index < v.dimensions.size()) v.dimensions.remove(s.index);
        break;
    }
    return v;
}

int partCount(const core::Venue& v, PartKind kind) {
    switch (kind) {
    case PartKind::Edge: return static_cast<int>(v.edges.size());
    case PartKind::Obstacle: return static_cast<int>(v.obstacles.size());
    case PartKind::Power: return static_cast<int>(v.power.size());
    case PartKind::Note: return static_cast<int>(v.notes.size());
    case PartKind::Dimension: return static_cast<int>(v.dimensions.size());
    }
    return 0;
}

std::pair<core::Venue, Selection> duplicatePart(core::Venue v, Selection s) {
    if (s.index < 0 || s.index >= partCount(v, s.kind)) return { v, s };
    switch (s.kind) {
    case PartKind::Edge: v.edges.append(v.edges[s.index]); break;
    case PartKind::Obstacle: v.obstacles.append(v.obstacles[s.index]); break;
    case PartKind::Power: v.power.append(v.power[s.index]); break;
    case PartKind::Note: v.notes.append(v.notes[s.index]); break;
    case PartKind::Dimension: v.dimensions.append(v.dimensions[s.index]); break;
    }
    const Selection copy{ s.kind, partCount(v, s.kind) - 1 };
    const double off = 24.0 * (38.09814081 / 12.0);
    return { movePart(std::move(v), copy, { off, off }), copy };
}

int estimateCount(const core::Venue& v) {
    int n = 0;
    for (const auto& e : v.edges) n += e.estimated ? 1 : 0;
    for (const auto& x : v.notes) n += x.estimated ? 1 : 0;
    for (const auto& d : v.dimensions) n += d.estimated ? 1 : 0;
    return n;
}

std::optional<RoomSize> roomSize(const core::Venue& v) {
    QVector<QPointF> pts;
    for (const auto& e : v.edges) pts += e.polyline;
    if (pts.size() < 3) return std::nullopt;
    const QRectF b = bounds(pts);
    double a = 0;
    for (int i = 0; i < pts.size(); ++i) {
        const QPointF p = pts[i], q = pts[(i + 1) % pts.size()];
        a += p.x() * q.y() - q.x() * p.y();
    }
    return RoomSize{ b.width(), b.height(), std::abs(a) / 2 };
}

std::optional<Hit> hitTest(const core::Venue& v, QPointF p, double tol) {
    for (int i = static_cast<int>(v.notes.size()) - 1; i >= 0; --i)
        if (dist(v.notes[i].pos, p) <= tol * 2) return Hit{ { PartKind::Note, i }, {}, {}, 0 };
    for (int i = static_cast<int>(v.power.size()) - 1; i >= 0; --i)
        if (dist(v.power[i].pos, p) <= std::max(tol, 6.0)) return Hit{ { PartKind::Power, i }, {}, {}, 0 };
    for (int i = static_cast<int>(v.dimensions.size()) - 1; i >= 0; --i) {
        const auto& d = v.dimensions[i];
        if (dist(d.from, p) <= tol) return Hit{ { PartKind::Dimension, i }, 0, {}, 0 };
        if (dist(d.to, p) <= tol) return Hit{ { PartKind::Dimension, i }, 1, {}, 0 };
        if (segDist(p, d.from, d.to).d <= tol) return Hit{ { PartKind::Dimension, i }, {}, {}, 0 };
    }
    for (int i = static_cast<int>(v.obstacles.size()) - 1; i >= 0; --i) {
        const auto& poly = v.obstacles[i].polygon;
        for (int k = 0; k < poly.size(); ++k)
            if (dist(poly[k], p) <= tol) return Hit{ { PartKind::Obstacle, i }, k, {}, 0 };
        if (pointInPolygon(p, poly)) return Hit{ { PartKind::Obstacle, i }, {}, {}, 0 };
    }
    for (int i = static_cast<int>(v.edges.size()) - 1; i >= 0; --i) {
        const auto& poly = v.edges[i].polyline;
        for (int k = 0; k < poly.size(); ++k)
            if (dist(poly[k], p) <= tol) return Hit{ { PartKind::Edge, i }, k, {}, 0 };
        for (int s = 0; s + 1 < poly.size(); ++s) {
            const auto r = segDist(p, poly[s], poly[s + 1]);
            if (r.d <= tol) return Hit{ { PartKind::Edge, i }, {}, s, r.t };
        }
    }
    return std::nullopt;
}

double dist(QPointF a, QPointF b) {
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

double polylineLength(const QVector<QPointF>& poly) {
    double n = 0;
    for (int i = 1; i < poly.size(); ++i) n += dist(poly[i - 1], poly[i]);
    return n;
}

SegDist segDist(QPointF p, QPointF a, QPointF b) {
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    const double len2 = dx * dx + dy * dy;
    const double t =
        len2 == 0 ? 0 : std::clamp(((p.x() - a.x()) * dx + (p.y() - a.y()) * dy) / len2, 0.0, 1.0);
    return { dist(p, { a.x() + dx * t, a.y() + dy * t }), t };
}

bool pointInPolygon(QPointF p, const QVector<QPointF>& poly) {
    bool inside = false;
    for (int i = 0, j = static_cast<int>(poly.size()) - 1; i < poly.size(); j = i++) {
        const QPointF a = poly[i], b = poly[j];
        if ((a.y() > p.y()) != (b.y() > p.y())
            && p.x() < (b.x() - a.x()) * (p.y() - a.y()) / (b.y() - a.y()) + a.x())
            inside = !inside;
    }
    return inside;
}

QRectF bounds(const QVector<QPointF>& pts) {
    double x0 = std::numeric_limits<double>::max(), y0 = x0, x1 = -x0, y1 = -x0;
    for (const auto& p : pts)
        x0 = std::min(x0, p.x()), y0 = std::min(y0, p.y()), x1 = std::max(x1, p.x()),
        y1 = std::max(y1, p.y());
    return pts.isEmpty() ? QRectF() : QRectF(QPointF(x0, y0), QPointF(x1, y1));
}

namespace {
double roundTo(double x, double step) {
    return step > 0 ? std::round(x / step) * step : x;
}
} // namespace

Snapped snapPoint(const core::Venue& v, QPointF p, const SnapOptions& o) {
    if (o.toVenue) {
        std::optional<QPointF> best;
        double bestD = o.tolStuds;
        const auto consider = [&](QPointF c) {
            const double d = dist(c, p);
            if (d <= bestD) best = c, bestD = d;
        };
        for (const auto& e : v.edges)
            for (const auto& c : e.polyline) consider(c);
        for (const auto& ob : v.obstacles)
            for (const auto& c : ob.polygon) consider(c);
        if (best) return { *best, SnapKind::Corner };
    }
    if (o.from && o.angleStepDeg > 0) {
        const QPointF d = p - *o.from;
        const double len = std::hypot(d.x(), d.y());
        if (len > kEps) {
            const double step = o.angleStepDeg * M_PI / 180.0;
            const double a = std::round(std::atan2(d.y(), d.x()) / step) * step;
            const double l = roundTo(len, o.stepStuds);
            return { *o.from + QPointF(std::cos(a) * l, std::sin(a) * l), SnapKind::Angle };
        }
    }
    if (o.toVenue) {
        std::optional<QPointF> best;
        double bestD = o.tolStuds;
        for (const auto& e : v.edges)
            for (int i = 0; i + 1 < e.polyline.size(); ++i) {
                const QPointF a = e.polyline[i], b = e.polyline[i + 1];
                const auto r = segDist(p, a, b);
                if (r.d <= bestD) {
                    bestD = r.d;
                    const double len = dist(a, b);
                    const double along = len > 0 ? roundTo(r.t * len, o.stepStuds) / len : 0.0;
                    best = lerp(a, b, std::clamp(along, 0.0, 1.0));
                }
            }
        if (best) return { *best, SnapKind::Wall };
    }
    if (o.stepStuds > 0)
        return { { roundTo(p.x(), o.stepStuds), roundTo(p.y(), o.stepStuds) }, SnapKind::Grid };
    return { p, SnapKind::None };
}

QPointF pointAtLength(QPointF from, QPointF toward, double length, double angleStepDeg) {
    const QPointF d = toward - from;
    double a = std::hypot(d.x(), d.y()) > kEps ? std::atan2(d.y(), d.x()) : 0.0;
    if (angleStepDeg > 0) {
        const double step = angleStepDeg * M_PI / 180.0;
        a = std::round(a / step) * step;
    }
    return from + QPointF(std::cos(a) * length, std::sin(a) * length);
}

History commit(History h, core::Venue next) {
    h.past.push_back(std::move(h.present));
    while (static_cast<int>(h.past.size()) > kHistoryLimit) h.past.pop_front();
    h.present = std::move(next);
    h.future.clear();
    return h;
}

History undo(History h) {
    if (h.past.empty()) return h;
    h.future.push_front(std::move(h.present));
    h.present = std::move(h.past.back());
    h.past.pop_back();
    return h;
}

History redo(History h) {
    if (h.future.empty()) return h;
    h.past.push_back(std::move(h.present));
    h.present = std::move(h.future.front());
    h.future.pop_front();
    return h;
}

} // namespace bld::edit::venue
