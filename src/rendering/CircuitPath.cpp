#include "CircuitPath.h"

#include <algorithm>
#include <cmath>

namespace bld::rendering {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxStep = 2.0 * kPi / 180.0;  // 2 degrees

double dot(QPointF a, QPointF b) { return a.x() * b.x() + a.y() * b.y(); }
double cross(QPointF a, QPointF b) { return a.x() * b.y() - a.y() * b.x(); }
QPointF unit(QPointF v) {
    const double l = std::hypot(v.x(), v.y());
    return l > 0.0 ? v / l : QPointF(1.0, 0.0);
}
QPointF leftNormal(QPointF t) { return { -t.y(), t.x() }; }
QPointF direction(double degrees) {
    const double r = degrees * kPi / 180.0;
    return { std::cos(r), std::sin(r) };
}

// Points from `from` (leaving along unit tangent `t`) to `to`, on the circle
// tangent to t at `from`, without the first point. Appends (point, tangent).
void appendArc(std::vector<CircuitPathPoint>& out, QPointF from, QPointF t, QPointF to) {
    const QPointF d = to - from;
    const QPointF n = leftNormal(t);
    const double dd = dot(d, d);
    const double denom = 2.0 * dot(d, n);
    if (dd <= 1e-18) return;
    if (std::abs(denom) <= 1e-9 * std::sqrt(dd)) {  // straight
        out.push_back({ to, leftNormal(unit(d)), 0.0 });
        return;
    }
    const double r = dd / denom;  // signed: centre is r along the left normal
    const QPointF c = from + n * r;
    const QPointF a = from - c, b = to - c;
    const double turn = cross(a, t) >= 0.0 ? 1.0 : -1.0;  // +1: the angle grows along the path
    double sweep = std::atan2(cross(a, b), dot(a, b));
    if (turn > 0.0 && sweep < 0.0) sweep += 2.0 * kPi;
    if (turn < 0.0 && sweep > 0.0) sweep -= 2.0 * kPi;
    const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / kMaxStep - 1e-9)));
    const double a0 = std::atan2(a.y(), a.x());
    const double radius = std::abs(r);
    for (int k = 1; k <= steps; ++k) {
        const double ang = a0 + sweep * k / steps;
        const QPointF p = k == steps ? to : c + QPointF(std::cos(ang), std::sin(ang)) * radius;
        const QPointF tangent = QPointF(-std::sin(ang), std::cos(ang)) * turn;
        out.push_back({ p, leftNormal(tangent), 0.0 });
    }
}

}  // namespace

std::vector<CircuitPathPoint> circuitPath(QPointF p1, double angle1, QPointF p2, double angle2) {
    const QPointF t1 = -direction(angle1);
    const QPointF t2 = direction(angle2);
    const QPointF v = p2 - p1;
    std::vector<CircuitPathPoint> out;
    out.push_back({ p1, leftNormal(t1), 0.0 });
    const double vv = dot(v, v);
    if (vv <= 1e-18) return out;

    // Equal-tangent biarc: control points q1 = p1 + d t1 and q2 = p2 - d t2
    // with |q2 - q1| = 2d; the arcs meet halfway between them.
    const double tt = dot(t1, t2);
    const double vt = dot(v, t1 + t2);
    double d = -1.0;
    if (std::abs(1.0 - tt) < 1e-12) {
        if (vt > 1e-12) d = vv / (2.0 * vt);
    } else {
        d = (vt - std::sqrt(vt * vt + 2.0 * (1.0 - tt) * vv)) / (2.0 * (tt - 1.0));
    }
    if (!(d > 0.0) || !std::isfinite(d)) {
        out.push_back({ p2, leftNormal(unit(v)), 0.0 });  // no sensible curve: BlueBrick's straight line
        out.front().normal = out.back().normal;
    } else {
        const QPointF q1 = p1 + t1 * d, q2 = p2 - t2 * d;
        const QPointF j = (q1 + q2) / 2.0;
        appendArc(out, p1, t1, j);
        appendArc(out, j, unit(q2 - q1), p2);
        // Both ends keep the connections' own direction.
        out.back().normal = leftNormal(t2);
    }
    for (size_t i = 1; i < out.size(); ++i) {
        const QPointF step = out[i].p - out[i - 1].p;
        out[i].s = out[i - 1].s + std::hypot(step.x(), step.y());
    }
    return out;
}

std::vector<QPointF> offsetPath(const std::vector<CircuitPathPoint>& path, double offset) {
    std::vector<QPointF> out;
    out.reserve(path.size());
    for (const auto& pt : path) out.push_back(pt.p + pt.normal * offset);
    return out;
}

CircuitPathPoint pointAt(const std::vector<CircuitPathPoint>& path, double s) {
    if (path.empty()) return {};
    if (s <= path.front().s) return path.front();
    for (size_t i = 1; i < path.size(); ++i) {
        if (s > path[i].s) continue;
        const double span = path[i].s - path[i - 1].s;
        const double f = span > 0.0 ? (s - path[i - 1].s) / span : 0.0;
        CircuitPathPoint pt;
        pt.p = path[i - 1].p + (path[i].p - path[i - 1].p) * f;
        pt.normal = unit(path[i - 1].normal + (path[i].normal - path[i - 1].normal) * f);
        pt.s = s;
        return pt;
    }
    return path.back();
}

std::vector<QPointF> offsetPathBetween(const std::vector<CircuitPathPoint>& path, double offset, double s0, double s1) {
    std::vector<QPointF> out;
    if (path.empty() || s1 <= s0) return out;
    const auto at = [&](const CircuitPathPoint& pt) { return pt.p + pt.normal * offset; };
    out.push_back(at(pointAt(path, s0)));
    for (const auto& pt : path)
        if (pt.s > s0 && pt.s < s1) out.push_back(at(pt));
    out.push_back(at(pointAt(path, s1)));
    return out;
}

}  // namespace bld::rendering
