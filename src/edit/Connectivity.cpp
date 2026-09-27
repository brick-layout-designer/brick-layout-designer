#include "Connectivity.h"

#include "../core/Brick.h"
#include "../core/Ids.h"
#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/PartsLibrary.h"

#include <QHash>
#include <QPointF>

#include <cmath>
#include <vector>

namespace bld::edit {

using core::Brick;
using core::LayerBrick;
using core::LayerKind;
using core::Map;

namespace {

QPointF rotatePoint(QPointF p, double degrees) {
    const double r = degrees * M_PI / 180.0;
    const double c = std::cos(r), s = std::sin(r);
    return { p.x() * c - p.y() * s, p.x() * s + p.y() * c };
}

struct WorldConn {
    Brick*   brick = nullptr;
    int      connIdx = -1;
    QPointF  worldPos;
    QString  type;
};

// Round a stud coord to a fixed grid cell for spatial bucketing so we
// find coincident connections in O(N). Bucket size 2 studs keeps the
// bucket lookup's ±1 neighbourhood wider than our 1-stud matching
// tolerance, so a coincident pair always lands in the same or an
// adjacent bucket.
constexpr double kBucketSize = 2.0;
QPair<int, int> bucketOf(QPointF p) {
    return { static_cast<int>(std::floor(p.x() / kBucketSize)),
             static_cast<int>(std::floor(p.y() / kBucketSize)) };
}

}  // namespace

void rebuildConnectivity(core::Map& map, parts::PartsLibrary& lib) {
    // Collect every brick's connection in world coords. Clear any
    // linkedToId as we go — we're recomputing from scratch based on
    // current world positions.
    std::vector<WorldConn> all;
    all.reserve(256);

    for (auto& layerPtr : map.layers()) {
        if (!layerPtr || layerPtr->kind() != LayerKind::Brick) continue;
        auto& BL = static_cast<LayerBrick&>(*layerPtr);
        for (auto& brick : BL.bricks) {
            auto meta = lib.metadata(brick.partNumber);
            if (!meta) continue;
            const int n = meta->connections.size();
            // Ensure connections vector covers every library connection so
            // indices align between part metadata and stored ConnectionPoint.
            while (static_cast<int>(brick.connections.size()) < n) {
                brick.connections.push_back({});
            }
            for (int i = 0; i < n; ++i) {
                // Every connection point needs its own id: <LinkedTo> in a
                // .bbm names the partner's connection, not the partner brick.
                if (brick.connections[i].guid.isEmpty()) brick.connections[i].guid = core::newBbmId();
                brick.connections[i].linkedToId.clear();  // rebuild
                const auto& c = meta->connections[i];
                if (c.type.isEmpty()) continue;
                WorldConn wc;
                wc.brick = &brick;
                wc.connIdx = i;
                wc.worldPos = brick.displayArea.center()
                              + rotatePoint(c.position, brick.orientation);
                wc.type = c.type;
                all.push_back(wc);
            }
        }
    }

    // Spatial bucketing for O(N) pair finding.
    QHash<QPair<int, int>, std::vector<int>> bucket;
    bucket.reserve(all.size() * 2);
    for (int i = 0; i < static_cast<int>(all.size()); ++i) {
        bucket[bucketOf(all[i].worldPos)].push_back(i);
    }

    // Matching tolerance. With mOffsetFromOriginalImage applied during
    // set expansion (see MapView set-placement branch), connection
    // world positions should coincide to within sub-stud precision for
    // visually-touching tracks. 1 stud gives comfortable slack for
    // multi-decimal set positions without linking bricks that are
    // obviously separated (vanilla track chord ≥ 8 studs).
    constexpr double kTolStuds = 1.0;
    constexpr double kTolSq = kTolStuds * kTolStuds;

    // When a connection has multiple candidates inside the tolerance
    // (common at switch junctions where 3+ tracks share a point)
    // prefer the nearest one. Falling back to "first in bucket" leaves
    // the wrong pair linked while the true-nearest stays red.

    for (int i = 0; i < static_cast<int>(all.size()); ++i) {
        const auto& a = all[i];
        if (!a.brick->connections[a.connIdx].linkedToId.isEmpty()) continue;
        const auto ab = bucketOf(a.worldPos);
        int bestJ = -1;
        double bestDistSq = kTolSq;
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                auto it = bucket.constFind({ ab.first + dx, ab.second + dy });
                if (it == bucket.constEnd()) continue;
                for (int j : *it) {
                    if (j <= i) continue;
                    const auto& b = all[j];
                    if (a.brick == b.brick) continue;
                    if (a.type != b.type) continue;
                    if (!b.brick->connections[b.connIdx].linkedToId.isEmpty()) continue;
                    const QPointF d = a.worldPos - b.worldPos;
                    const double distSq = d.x() * d.x() + d.y() * d.y();
                    if (distSq > bestDistSq) continue;
                    bestJ = j;
                    bestDistSq = distSq;
                }
            }
        }
        if (bestJ >= 0) {
            const auto& b = all[bestJ];
            a.brick->connections[a.connIdx].linkedToId = b.brick->connections[b.connIdx].guid;
            b.brick->connections[b.connIdx].linkedToId = a.brick->connections[a.connIdx].guid;
        }
    }
}

}  // namespace bld::edit
