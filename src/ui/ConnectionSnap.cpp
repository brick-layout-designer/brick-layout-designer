#include "ConnectionSnap.h"

#include "../core/Brick.h"
#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/PartsLibrary.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bld::ui {

namespace {

QPointF rotatePoint(QPointF p, double degrees) {
    const double r = degrees * M_PI / 180.0;
    const double c = std::cos(r), s = std::sin(r);
    return { p.x() * c - p.y() * s, p.x() * s + p.y() * c };
}

// Core scan: given an active connection's world position + type, find the
// nearest free compatible target connection within threshold. Excludes
// any brick in `movingGuids`.
struct TargetScan {
    bool found = false;
    QPointF worldPos;
    double  angle = 0.0;
    double  distSq = 0.0;
};
TargetScan scanForNearestFreeTarget(
    const core::Map& map, parts::PartsLibrary& lib,
    const QString& activeType, QPointF activeWorldPos,
    const QSet<QString>& movingGuids, double thresholdStuds) {

    TargetScan out;
    const double thresholdSq = thresholdStuds * thresholdStuds;
    double bestSq = thresholdSq;

    for (const auto& layerPtr : map.layers()) {
        if (!layerPtr || layerPtr->kind() != core::LayerKind::Brick) continue;
        const auto& bl = static_cast<const core::LayerBrick&>(*layerPtr);
        for (const auto& tb : bl.bricks) {
            if (movingGuids.contains(tb.guid)) continue;
            auto tmeta = lib.metadata(tb.partNumber);
            if (!tmeta) continue;
            const QPointF tCenter = tb.displayArea.center();
            const int nT = tmeta->connections.size();
            for (int tci = 0; tci < nT; ++tci) {
                const auto& tc = tmeta->connections[tci];
                if (tc.type != activeType) continue;
                // Target must be free.
                if (tci < static_cast<int>(tb.connections.size()) &&
                    !tb.connections[tci].linkedToId.isEmpty()) continue;
                const QPointF tWorld = tCenter + rotatePoint(tc.position, tb.orientation);
                const QPointF d = tWorld - activeWorldPos;
                const double sq = d.x() * d.x() + d.y() * d.y();
                if (sq < bestSq) {
                    bestSq = sq;
                    out.worldPos = tWorld;
                    out.angle = tc.angleDegrees + tb.orientation;
                    out.distSq = sq;
                    out.found = true;
                }
            }
        }
    }
    return out;
}

}  // namespace

ConnectionSnapResult masterBrickSnap(
    const core::Map& map,
    parts::PartsLibrary& lib,
    const core::Brick& master,
    QPointF masterCenterStuds,
    int activeConnIdx,
    const QSet<QString>& movingGuids,
    double thresholdStuds) {

    ConnectionSnapResult out;
    auto meta = lib.metadata(master.partNumber);
    if (!meta) return out;
    if (activeConnIdx < 0 || activeConnIdx >= meta->connections.size()) return out;
    const auto& activeConn = meta->connections[activeConnIdx];
    if (activeConn.type.isEmpty()) return out;

    // Active connection must be free on the master side; grabbing a
    // linked joint means the user is moving the whole connected chain
    // and we don't re-snap it.
    if (activeConnIdx < static_cast<int>(master.connections.size()) &&
        !master.connections[activeConnIdx].linkedToId.isEmpty()) return out;

    const QPointF activeWorldPos =
        masterCenterStuds + rotatePoint(activeConn.position, master.orientation);

    auto target = scanForNearestFreeTarget(map, lib, activeConn.type,
                                            activeWorldPos, movingGuids, thresholdStuds);
    if (!target.found) return out;

    out.applied = true;
    out.translationStuds = target.worldPos - activeWorldPos;

    // Rotation-aligned variant: orient so angles match 180°.
    //   newOrient + activeConn.angle == target.angle + 180
    double newOrient = target.angle + 180.0 - activeConn.angleDegrees;
    newOrient = std::remainder(newOrient, 360.0);  // (-180, 180], no loop on absurd angles
    if (newOrient <= -180.0) newOrient += 360.0;
    const QPointF newCenter = target.worldPos - rotatePoint(activeConn.position, newOrient);
    out.rotationAlignedTranslationStuds = newCenter - masterCenterStuds;
    out.newOrientation = static_cast<float>(newOrient);
    return out;
}

ConnectionSnapResult newPartPlacementSnap(
    const core::Map& map,
    parts::PartsLibrary& lib,
    const QString& partKey,
    QPointF placementCenterStuds,
    float   placementOrientation,
    double  thresholdStuds) {

    ConnectionSnapResult best;
    auto meta = lib.metadata(partKey);
    if (!meta) return best;
    const int n = meta->connections.size();
    if (n == 0) return best;

    double bestDistSq = std::numeric_limits<double>::max();

    for (int ci = 0; ci < n; ++ci) {
        const auto& c = meta->connections[ci];
        if (c.type.isEmpty()) continue;
        const QPointF activeWorldPos =
            placementCenterStuds + rotatePoint(c.position, placementOrientation);
        auto target = scanForNearestFreeTarget(map, lib, c.type, activeWorldPos,
                                                /*movingGuids=*/{}, thresholdStuds);
        if (!target.found) continue;
        if (target.distSq < bestDistSq) {
            bestDistSq = target.distSq;
            best.applied = true;
            best.translationStuds = target.worldPos - activeWorldPos;

            double newOrient = target.angle + 180.0 - c.angleDegrees;
            newOrient = std::remainder(newOrient, 360.0);  // (-180, 180], no loop on absurd angles
            if (newOrient <= -180.0) newOrient += 360.0;
            const QPointF newCenter =
                target.worldPos - rotatePoint(c.position, newOrient);
            best.rotationAlignedTranslationStuds = newCenter - placementCenterStuds;
            best.newOrientation = static_cast<float>(newOrient);
        }
    }
    return best;
}

}  // namespace bld::ui
