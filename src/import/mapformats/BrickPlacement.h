#pragma once

// BlueBrick's brick geometry, shared by the map-format readers/writers.
// A brick's displayArea is the box around its rotated hull; the sprite
// centre sits PartsLibrary::footprint().imageOffset away from it, and
// connection points are relative to the sprite centre.

#include "../../core/Brick.h"
#include "../../parts/PartsLibrary.h"

#include <QPointF>
#include <QtMath>

#include <cmath>

namespace bld::import::placement {

inline QPointF rotated(QPointF v, double degrees) {
    const double r = qDegreesToRadians(degrees);
    const double c = std::cos(r), s = std::sin(r);
    return { v.x() * c - v.y() * s, v.x() * s + v.y() * c };
}

inline QPointF imageCentre(const core::Brick& b, parts::PartsLibrary& lib) {
    QPointF c = b.displayArea.center();
    if (const auto fp = lib.footprint(b.partNumber, b.orientation)) c += fp->imageOffset;
    return c;
}

inline QPointF connectionWorld(const core::Brick& b, int index, parts::PartsLibrary& lib) {
    const auto meta = lib.metadata(b.partNumber);
    if (!meta || index < 0 || index >= meta->connections.size()) return imageCentre(b, lib);
    return imageCentre(b, lib) + rotated(meta->connections[index].position, b.orientation);
}

// Sets displayArea for the brick's current orientation so its sprite centre
// is `centre`. Unknown parts get a 2x2-stud placeholder.
inline void placeByImageCentre(core::Brick& b, QPointF centre, parts::PartsLibrary& lib) {
    QSizeF size(2, 2);
    if (const auto fp = lib.footprint(b.partNumber, b.orientation)) {
        centre -= fp->imageOffset;
        size = fp->size;
    }
    b.displayArea = QRectF(centre.x() - size.width() / 2.0, centre.y() - size.height() / 2.0,
                           size.width(), size.height());
}

// Places the brick so its connection `index` sits at `world`.
inline void placeByConnection(core::Brick& b, int index, QPointF world, parts::PartsLibrary& lib) {
    const auto meta = lib.metadata(b.partNumber);
    QPointF centre = world;
    if (meta && index >= 0 && index < meta->connections.size())
        centre -= rotated(meta->connections[index].position, b.orientation);
    placeByImageCentre(b, centre, lib);
}

}  // namespace bld::import::placement
