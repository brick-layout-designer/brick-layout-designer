#include "ImportConnections.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../edit/Connectivity.h"
#include "../parts/PartsLibrary.h"

#include <QtMath>

#include <cmath>

namespace bld::import {

QVector<ImportedConnection> externalConnections(core::Map& map,
                                                parts::PartsLibrary& lib,
                                                QPointF partOriginStuds) {
    edit::rebuildConnectivity(map, lib);

    const auto rotate = [](QPointF p, double deg) {
        const double r = qDegreesToRadians(deg);
        const double c = std::cos(r), s = std::sin(r);
        return QPointF(p.x() * c - p.y() * s, p.x() * s + p.y() * c);
    };

    QVector<ImportedConnection> out;
    for (const auto& layerPtr : map.layers()) {
        if (!layerPtr || layerPtr->kind() != core::LayerKind::Brick) continue;
        const auto& layer = static_cast<const core::LayerBrick&>(*layerPtr);
        for (const auto& brick : layer.bricks) {
            const auto meta = lib.metadata(brick.partNumber);
            if (!meta) continue;
            const QPointF centre = brick.displayArea.center();
            for (int i = 0; i < meta->connections.size(); ++i) {
                const auto& c = meta->connections[i];
                if (c.type.isEmpty()) continue;
                if (i < static_cast<int>(brick.connections.size())
                    && !brick.connections[i].linkedToId.isEmpty()) continue;
                const QPointF world = centre + rotate(c.position, brick.orientation);
                ImportedConnection ic;
                ic.type     = c.type;
                ic.xStuds   = world.x() - partOriginStuds.x();
                ic.yStuds   = world.y() - partOriginStuds.y();
                ic.angleDeg = std::remainder(c.angleDegrees + brick.orientation, 360.0);
                out.append(ic);
            }
        }
    }
    return out;
}

}  // namespace bld::import
