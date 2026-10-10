#include "VenueLabels.h"

#include "VenueDraw.h"

#include <QLineF>
#include <QPolygonF>

#include <algorithm>
#include <array>
#include <cmath>

namespace bld::rendering {

namespace {
constexpr double kK = 8.0;  // scene px per stud

std::array<QPointF, 2> axes(const VenueLabel& r) {
    const double t = r.angle * M_PI / 180.0;
    return { QPointF(std::cos(t), std::sin(t)), QPointF(-std::sin(t), std::cos(t)) };
}

std::array<QPointF, 4> corners(const VenueLabel& r) {
    const auto [u, v] = axes(r);
    const double hw = r.width / 2, hh = r.height / 2;
    std::array<QPointF, 4> out;
    const int s[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    for (int k = 0; k < 4; ++k) out[k] = r.centre + u * (hw * s[k][0]) + v * (hh * s[k][1]);
    return out;
}
}  // namespace

VenueLabelColors venueLabelColors(bool dark) {
    if (dark)
        return { QColor::fromRgbF(30 / 255.0f, 41 / 255.0f, 59 / 255.0f, 0.92f), QColor::fromRgbF(1, 1, 1, 0.25f),
                 QColor(241, 245, 249) };
    return { QColor::fromRgbF(1, 1, 1, 0.92f), QColor::fromRgbF(15 / 255.0f, 23 / 255.0f, 42 / 255.0f, 0.2f),
             QColor(20, 20, 20) };
}

QString venueDistanceText(double lenStuds) {
    const double ft = lenStuds * 0.026248;
    return ft < 1.0 ? QStringLiteral("%1\"").arg(ft * 12.0, 0, 'f', 1) : QStringLiteral("%1 ft").arg(ft, 0, 'f', 2);
}

double uprightAngle(double dx, double dy) {
    double a = std::atan2(dy, dx) * 180.0 / M_PI;
    if (a >= 90.0) a -= 180.0;
    else if (a < -90.0) a += 180.0;
    return a;
}

bool pillsOverlap(const VenueLabel& a, const VenueLabel& b) {
    const auto ca = corners(a), cb = corners(b);
    const auto aa = axes(a), ab = axes(b);
    for (const QPointF& ax : { aa[0], aa[1], ab[0], ab[1] }) {
        double a0 = 1e300, a1 = -1e300, b0 = 1e300, b1 = -1e300;
        for (const QPointF& p : ca) {
            const double d = p.x() * ax.x() + p.y() * ax.y();
            a0 = std::min(a0, d);
            a1 = std::max(a1, d);
        }
        for (const QPointF& p : cb) {
            const double d = p.x() * ax.x() + p.y() * ax.y();
            b0 = std::min(b0, d);
            b1 = std::max(b1, d);
        }
        if (a1 <= b0 || b1 <= a0) return false;
    }
    return true;
}

std::vector<VenueLabel> venueEdgeLabels(const QVector<core::VenueEdge>& edges, const VenueLabelOptions& opts) {
    const double f = opts.fontPx;
    std::vector<VenueLabel> out;
    // The room's middle: labels go on the far side of each wall from it.
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
    bool any = false;
    for (const auto& e : edges)
        for (const QPointF& p : e.polyline) {
            any = true;
            x0 = std::min(x0, p.x());
            y0 = std::min(y0, p.y());
            x1 = std::max(x1, p.x());
            y1 = std::max(y1, p.y());
        }
    if (!any) return out;
    const QPointF c((x0 + x1) / 2 * kK, (y0 + y1) / 2 * kK);
    const double height = f * (1 + 2 * VenueLabelPill::padY);
    const auto widthOf = [&](const QString& t) { return opts.measure(t, f) + 2 * VenueLabelPill::padX * f; };

    std::vector<QString> shorts;
    std::vector<QPointF> normals;
    for (int i = 0; i < static_cast<int>(edges.size()); ++i) {
        const auto& poly = edges[i].polyline;
        if (poly.size() < 2) continue;
        const QPointF a = poly.first(), b = poly.last();
        const double dx = b.x() - a.x(), dy = b.y() - a.y();
        const double len = std::hypot(dx, dy);
        if (len <= 0.5) continue;
        const auto& edge = edges[i];
        const QString distance = venueDistanceText(len);
        const QString base = edge.label.isEmpty() ? distance : edge.label + QStringLiteral(" — ") + distance;
        const QString full = edge.estimated ? venuedraw::estimatedText(base) : base;
        const QString shortText = edge.estimated ? venuedraw::estimatedText(distance) : distance;
        const QPointF m = (a + b) / 2 * kK;
        QPointF n(-dy / len, dx / len);
        if (n.x() * (m.x() - c.x()) + n.y() * (m.y() - c.y()) < 0) n = -n;
        const double off = VenueLabelPill::gap * f + height / 2;
        VenueLabel l;
        l.edge = i;
        l.text = full;
        l.full = full;
        l.centre = m + n * off;
        l.angle = uprightAngle(dx, dy);
        l.width = widthOf(full);
        l.height = height;
        out.push_back(l);
        shorts.push_back(shortText);
        normals.push_back(n);
    }

    // Overlapping labels show just their length (the selected wall's stays whole).
    std::vector<bool> clash(out.size(), false);
    for (size_t i = 0; i < out.size(); ++i)
        for (size_t j = 0; j < out.size() && !clash[i]; ++j)
            if (j != i && pillsOverlap(out[i], out[j])) clash[i] = true;
    for (size_t i = 0; i < out.size(); ++i) {
        auto& l = out[i];
        if (!clash[i] || (opts.selectedEdge && *opts.selectedEdge == l.edge) || shorts[i] == l.full) continue;
        l.text = shorts[i];
        l.shortened = true;
        l.width = widthOf(shorts[i]);
    }

    // Out from under the selection handles.
    const double hs = opts.handleHalfPx;
    if (hs > 0 && !opts.handles.isEmpty()) {
        std::vector<VenueLabel> boxes;
        for (const QPointF& h : opts.handles) {
            VenueLabel b;
            b.centre = h * kK;
            b.width = b.height = hs * 2;
            boxes.push_back(b);
        }
        for (size_t i = 0; i < out.size(); ++i) {
            auto& l = out[i];
            for (int k = 0; k < 8; ++k) {
                const bool covered = std::any_of(boxes.begin(), boxes.end(), [&](const VenueLabel& b) { return pillsOverlap(l, b); });
                if (!covered) break;
                l.centre += normals[i] * (hs * 2 + VenueLabelPill::handleGap * f);
            }
        }
    }
    return out;
}

}  // namespace bld::rendering
