#include "ConnectionCheck.h"

#include "Sets.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/BrickPlacement.h"
#include "../parts/PartsLibrary.h"

#include <QStringList>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace bld::edit {

namespace {

// -180..180
double normalised(double degrees) {
    double d = std::fmod(degrees, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d <= -180.0) d += 360.0;
    return d;
}

QString pointText(const CheckedPoint& p) {
    return QStringLiteral("%1 #%2 (type %3) at (%4, %5) facing %6°")
        .arg(p.partNumber)
        .arg(p.connection)
        .arg(p.type)
        .arg(p.at.x(), 0, 'f', 3)
        .arg(p.at.y(), 0, 'f', 3)
        .arg(normalised(p.angle), 0, 'f', 1);
}

void add(ConnectionReport& into, const ConnectionReport& more) {
    into.parts += more.parts;
    into.points += more.points;
    for (auto it = more.pointsByType.begin(); it != more.pointsByType.end(); ++it)
        into.pointsByType[it.key()] += it.value();
    into.pairs += more.pairs;
    into.open += more.open;
    into.rough += more.rough;
}

} // namespace

ConnectionReport checkConnections(const std::vector<core::Brick>& bricks, parts::PartsLibrary& lib,
                                  const ConnectionCheckOptions& options) {
    ConnectionReport report;
    report.parts = static_cast<int>(bricks.size());
    std::vector<CheckedPoint> points;
    for (size_t i = 0; i < bricks.size(); ++i) {
        const core::Brick& b = bricks[i];
        const auto meta = lib.metadata(b.partNumber);
        if (!meta) continue;
        for (int c = 0; c < meta->connections.size(); ++c) {
            const auto& conn = meta->connections[c];
            if (conn.type.isEmpty()) continue;
            CheckedPoint p;
            p.partNumber = b.partNumber;
            p.brick = static_cast<int>(i);
            p.connection = c;
            p.type = conn.type;
            p.at = parts::placement::connectionWorld(b, c, lib);
            p.angle = b.orientation + conn.angleDegrees;
            points.push_back(p);
            ++report.pointsByType[p.type];
        }
    }
    report.points = static_cast<int>(points.size());

    // Every candidate pair, closest first; each point pairs once.
    std::vector<std::tuple<double, size_t, size_t>> candidates;
    for (size_t i = 0; i < points.size(); ++i)
        for (size_t j = i + 1; j < points.size(); ++j) {
            if (points[i].brick == points[j].brick || points[i].type != points[j].type) continue;
            const double gap =
                std::hypot(points[i].at.x() - points[j].at.x(), points[i].at.y() - points[j].at.y());
            if (gap <= options.linkDistance) candidates.emplace_back(gap, i, j);
        }
    std::sort(candidates.begin(), candidates.end());
    std::vector<bool> paired(points.size(), false);
    for (const auto& [gap, i, j] : candidates) {
        if (paired[i] || paired[j]) continue;
        paired[i] = paired[j] = true;
        CheckedPair pair{ points[i], points[j], gap,
                          std::abs(normalised(points[i].angle - points[j].angle + 180.0)) };
        if (pair.gap > options.exactGap || pair.angleOff > options.exactAngle) report.rough << pair;
        report.pairs << pair;
    }
    for (size_t i = 0; i < points.size(); ++i)
        if (!paired[i]) report.open << points[i];
    return report;
}

ConnectionReport checkSetConnections(const QString& setKey, parts::PartsLibrary& lib,
                                     const ConnectionCheckOptions& options) {
    return checkConnections(expandSet(lib, setKey, QPointF(0, 0)).bricks, lib, options);
}

ConnectionReport checkMapConnections(const core::Map& map, parts::PartsLibrary& lib,
                                     const ConnectionCheckOptions& options) {
    ConnectionReport report;
    for (const auto& layer : map.layers())
        if (layer->kind() == core::LayerKind::Brick)
            add(report, checkConnections(static_cast<const core::LayerBrick&>(*layer).bricks, lib, options));
    return report;
}

QString connectionReportText(const QString& title, const ConnectionReport& report) {
    QStringList types;
    for (auto it = report.pointsByType.begin(); it != report.pointsByType.end(); ++it)
        types << QStringLiteral("type %1: %2").arg(it.key()).arg(it.value());
    QStringList lines;
    lines << QStringLiteral("%1: %2 parts, %3 connection points (%4)")
                 .arg(title)
                 .arg(report.parts)
                 .arg(report.points)
                 .arg(types.isEmpty() ? QStringLiteral("none") : types.join(QStringLiteral(", ")));
    lines << QStringLiteral("%1 pairs, %2 open ends, %3 rough pairs")
                 .arg(report.pairs.size())
                 .arg(report.open.size())
                 .arg(report.rough.size());
    for (const CheckedPoint& p : report.open) lines << QStringLiteral("open: ") + pointText(p);
    for (const CheckedPair& p : report.rough)
        lines << QStringLiteral("rough: %1 and %2: %3 studs apart, %4° from facing")
                     .arg(pointText(p.a), pointText(p.b))
                     .arg(p.gap, 0, 'f', 3)
                     .arg(p.angleOff, 0, 'f', 1);
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

} // namespace bld::edit
