#include "ImportConnections.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../edit/Connectivity.h"
#include "../parts/BrickPlacement.h"
#include "../parts/PartsLibrary.h"

#include <QtMath>

#include <cmath>

namespace bld::import {

namespace {

// Rounding noise from a model's float coordinates (LDD's 0.99999988s, a
// bottom layer a hair short) shouldn't reach the part: a position within
// a few hundredths of a half stud, or an angle within a tenth of a degree
// of a multiple of 11.25° (every BlueBrick track angle), is that value.
double tidyPosition(double v) {
    const double half = std::round(v * 2.0) / 2.0;
    return std::abs(v - half) < 0.03 ? half : v;
}

double tidyAngle(double deg) {
    const double step = std::round(deg / 11.25) * 11.25;
    return std::remainder(std::abs(deg - step) < 0.1 ? step : deg, 360.0);
}

}  // namespace

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
            const QPointF centre = parts::placement::imageCentre(brick, lib);
            for (int i = 0; i < meta->connections.size(); ++i) {
                const auto& c = meta->connections[i];
                if (c.type.isEmpty()) continue;
                if (i < static_cast<int>(brick.connections.size())
                    && !brick.connections[i].linkedToId.isEmpty()) continue;
                const QPointF world = centre + rotate(c.position, brick.orientation);
                ImportedConnection ic;
                ic.type     = c.type;
                ic.xStuds   = tidyPosition(world.x() - partOriginStuds.x());
                ic.yStuds   = tidyPosition(world.y() - partOriginStuds.y());
                ic.angleDeg = tidyAngle(c.angleDegrees + brick.orientation);
                out.append(ic);
            }
        }
    }
    return out;
}

}  // namespace bld::import
