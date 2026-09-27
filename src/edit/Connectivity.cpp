#include "Connectivity.h"

#include "../core/Brick.h"
#include "../core/Ids.h"
#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/PartsLibrary.h"

#include <QHash>
#include <QPointF>

#include <algorithm>
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

// BlueBrick's arePositionsEqual: within half a stud on each axis.
constexpr double kTolStuds = 0.5;
QPair<int, int> bucketOf(QPointF p) {
    return { static_cast<int>(std::floor(p.x())), static_cast<int>(std::floor(p.y())) };
}

// BlueBrick's ConnectionPoint.ConnectionLink setter, when a link is made:
// if it lands on the brick's active connection, the active one moves to
// that connection's <nextConnexionPreference>, or failing that to the next
// free connection (wrapping; unchanged if none is free).
void onLinked(Brick& brick, int connIdx, const parts::PartMetadata& meta) {
    int& active = brick.activeConnectionPointIndex;
    if (active != connIdx) return;
    const int n = static_cast<int>(brick.connections.size());
    const int preferred = connIdx < meta.connections.size() ? meta.connections[connIdx].nextPreferredIndex : 0;
    active = std::clamp(preferred, 0, n - 1);
    // For a brick in a group BlueBrick moves the group's active connection
    // instead of looking for the brick's next free one.
    if (!brick.myGroupId.isEmpty()) return;
    if (brick.connections[active].linkedToId.isEmpty()) return;
    for (int step = 1; step < n; ++step) {
        const int candidate = (active + step) % n;
        if (brick.connections[candidate].linkedToId.isEmpty()) { active = candidate; return; }
    }
}

}  // namespace

void rebuildConnectivity(Map& map, parts::PartsLibrary& lib) {
    // Links are recomputed from positions, the way BlueBrick's
    // updateFullBrickConnectivity does it (per layer), but the previous
    // links are remembered so the active-connection rules only react to
    // links that actually changed.
    for (auto& layerPtr : map.layers()) {
        if (!layerPtr || layerPtr->kind() != LayerKind::Brick) continue;
        auto& layer = static_cast<LayerBrick&>(*layerPtr);

        QHash<QString, QString> previous;  // connection guid -> old partner connection guid
        std::vector<WorldConn> all;
        all.reserve(layer.bricks.size() * 2);
        for (auto& brick : layer.bricks) {
            const auto meta = lib.metadata(brick.partNumber);
            if (!meta) continue;
            const int n = meta->connections.size();
            while (static_cast<int>(brick.connections.size()) < n) brick.connections.push_back({});
            // Connection positions are relative to the sprite centre, which
            // is off the displayArea centre for parts with an XML hull.
            const QPointF centre = brick.displayArea.center() + lib.imageOffset(brick.partNumber, brick.orientation);
            for (int i = 0; i < n; ++i) {
                auto& cp = brick.connections[i];
                // <LinkedTo> in a .bbm names the partner's connection, so
                // every connection point needs an id.
                if (cp.guid.isEmpty()) cp.guid = core::newBbmId();
                if (!cp.linkedToId.isEmpty()) previous.insert(cp.guid, cp.linkedToId);
                cp.linkedToId.clear();
                const auto& c = meta->connections[i];
                if (c.type.isEmpty()) continue;
                all.push_back({ &brick, i, centre + rotatePoint(c.position, brick.orientation), c.type });
            }
        }

        // BlueBrick walks bricks in order and links each free connection to
        // the FIRST free connection of the same type (in the same order) at
        // an equal position. Buckets make that O(N) without changing which
        // one wins: the lowest index among the candidates.
        QHash<QPair<int, int>, std::vector<int>> bucket;
        for (int i = 0; i < static_cast<int>(all.size()); ++i) bucket[bucketOf(all[i].worldPos)].push_back(i);

        for (int i = 0; i < static_cast<int>(all.size()); ++i) {
            auto& a = all[i];
            if (!a.brick->connections[a.connIdx].linkedToId.isEmpty()) continue;
            const auto ab = bucketOf(a.worldPos);
            int bestJ = -1;
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    const auto it = bucket.constFind({ ab.first + dx, ab.second + dy });
                    if (it == bucket.constEnd()) continue;
                    for (int j : *it) {
                        if (j == i || (bestJ >= 0 && j >= bestJ)) continue;
                        const auto& b = all[j];
                        if (a.brick == b.brick || a.type != b.type) continue;
                        if (!b.brick->connections[b.connIdx].linkedToId.isEmpty()) continue;
                        if (std::abs(a.worldPos.x() - b.worldPos.x()) >= kTolStuds) continue;
                        if (std::abs(a.worldPos.y() - b.worldPos.y()) >= kTolStuds) continue;
                        bestJ = j;
                    }
                }
            }
            if (bestJ < 0) continue;
            auto& b = all[bestJ];
            auto& ca = a.brick->connections[a.connIdx];
            auto& cb = b.brick->connections[b.connIdx];
            // BlueBrick sets the two links one after the other, and hands
            // the active connection over BEFORE storing each link (so the
            // connection being linked still counts as free). Only new links
            // trigger the hand-over.
            const bool isNew = previous.value(ca.guid) != cb.guid;
            if (isNew) if (const auto m = lib.metadata(a.brick->partNumber)) onLinked(*a.brick, a.connIdx, *m);
            ca.linkedToId = cb.guid;
            if (isNew) if (const auto m = lib.metadata(b.brick->partNumber)) onLinked(*b.brick, b.connIdx, *m);
            cb.linkedToId = ca.guid;
        }

        // A link that broke frees its connection; if the brick's active
        // connection is taken, the freed one becomes active (BlueBrick).
        for (const auto& wc : all) {
            const auto& cp = wc.brick->connections[wc.connIdx];
            if (!previous.contains(cp.guid) || !cp.linkedToId.isEmpty()) continue;
            int& active = wc.brick->activeConnectionPointIndex;
            if (active >= 0 && active < static_cast<int>(wc.brick->connections.size())
                && !wc.brick->connections[active].linkedToId.isEmpty())
                active = wc.connIdx;
        }
    }
}

}  // namespace bld::edit
