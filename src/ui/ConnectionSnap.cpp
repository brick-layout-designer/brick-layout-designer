#include "ConnectionSnap.h"

#include "../core/Brick.h"
#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/BrickPlacement.h"
#include "../parts/PartsLibrary.h"

#include <cmath>

namespace bld::ui {

QString connKey(const QString& guid, int index) { return guid + QLatin1Char('#') + QString::number(index); }

QPointF rotatePoint(QPointF p, double degrees) {
    const double r = degrees * M_PI / 180.0;
    const double c = std::cos(r), s = std::sin(r);
    return { p.x() * c - p.y() * s, p.x() * s + p.y() * c };
}

double facingOrientation(double targetAngle, double connAngle) {
    double o = std::remainder(targetAngle + 180.0 - connAngle, 360.0);  // [-180, 180]
    if (o <= -180.0) o += 360.0;
    return o;
}

QSet<QString> linkKeys(const core::Map& map, const QSet<QString>& movingGuids) {
    QSet<QString> keys = movingGuids;
    if (movingGuids.isEmpty()) return keys;
    for (const auto& layerPtr : map.layers()) {
        if (!layerPtr || layerPtr->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*layerPtr).bricks) {
            if (!movingGuids.contains(b.guid)) continue;
            for (const auto& c : b.connections)
                if (!c.guid.isEmpty()) keys.insert(c.guid);
        }
    }
    return keys;
}

std::vector<FreeTarget> freeTargets(const core::Map& map, parts::PartsLibrary& lib, const QSet<QString>& exclude) {
    std::vector<FreeTarget> out;
    const QSet<QString> keys = linkKeys(map, exclude);
    for (const auto& layerPtr : map.layers()) {
        if (!layerPtr || layerPtr->kind() != core::LayerKind::Brick) continue;
        for (const auto& tb : static_cast<const core::LayerBrick&>(*layerPtr).bricks) {
            if (exclude.contains(tb.guid)) continue;
            auto meta = lib.metadata(tb.partNumber);
            if (!meta) continue;
            const QPointF centre = parts::placement::imageCentre(tb, lib);
            const int n = meta->connections.size();
            for (int i = 0; i < n; ++i) {
                const auto& c = meta->connections[i];
                if (c.type.isEmpty()) continue;
                if (i < static_cast<int>(tb.connections.size()) && takenWhileStill(tb.connections[i].linkedToId, keys))
                    continue;
                out.push_back({ connKey(tb.guid, i), c.type, centre + rotatePoint(c.position, tb.orientation),
                                c.angleDegrees + tb.orientation });
            }
        }
    }
    return out;
}

QPointF turnPoint(QPointF p, double degrees, QPointF pivot, QPointF to) { return to + rotatePoint(p - pivot, degrees); }

SnapPick pickConnectionSnap(const std::vector<MovingConn>& moving, const std::vector<FreeTarget>& targets,
                            double reach, snapfeel::Session* session, bool bypass, bool final, bool group) {
    std::vector<snapfeel::Candidate> cands;
    std::vector<SnapPick> pairs;
    if (!bypass && reach > 0.0) {
        const double limit = snapfeel::holdReach(reach);
        const double limitSq = limit * limit;
        for (int m = 0; m < static_cast<int>(moving.size()); ++m) {
            const MovingConn& mc = moving[m];
            for (int t = 0; t < static_cast<int>(targets.size()); ++t) {
                const FreeTarget& tc = targets[t];
                if (tc.type != mc.type) continue;
                const QPointF d = tc.world - mc.world;
                const double sq = d.x() * d.x() + d.y() * d.y();
                if (sq > limitSq) continue;
                // A group turns at most a quarter; a join needing more
                // isn't offered (no crooked half-snap).
                const double turn = snapfeel::facingTurn(tc.angle, mc.angle);
                if (group && !snapfeel::groupTurnAllowed(turn)) continue;
                cands.push_back({ mc.key, tc.key, std::sqrt(sq), mc.mouseDist, std::abs(turn) });
                pairs.push_back({ m, t, turn });
            }
        }
    }
    const int i = session ? session->step(cands, reach, bypass, final) : snapfeel::pick(cands, std::nullopt, reach);
    return i >= 0 ? pairs[i] : SnapPick{};
}

}  // namespace bld::ui
