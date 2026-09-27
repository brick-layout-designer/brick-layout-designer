#pragma once

// BlueBrick's brick geometry. A brick's displayArea is the box around its
// rotated hull; the sprite centre (BlueBrick's "pivot") sits
// PartsLibrary::footprint().imageOffset away from the box centre, which
// is only non-zero for parts with an XML <hull>. Connection points and
// ruler attachments are relative to the sprite centre, and bricks rotate
// around it.

#include "../core/Brick.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "PartsLibrary.h"

#include <QPointF>
#include <QtMath>

#include <cmath>
#include <vector>

namespace bld::parts::placement {

inline QPointF rotated(QPointF v, double degrees) {
    const double r = qDegreesToRadians(degrees);
    const double c = std::cos(r), s = std::sin(r);
    return { v.x() * c - v.y() * s, v.x() * s + v.y() * c };
}

inline QPointF imageCentre(const core::Brick& b, parts::PartsLibrary& lib) {
    return b.displayArea.center() + lib.imageOffset(b.partNumber, b.orientation);
}

inline QPointF connectionWorld(const core::Brick& b, int index, parts::PartsLibrary& lib) {
    const auto meta = lib.metadata(b.partNumber);
    if (!meta || index < 0 || index >= meta->connections.size()) return imageCentre(b, lib);
    return imageCentre(b, lib) + rotated(meta->connections[index].position, b.orientation);
}

// The displayArea size for the brick's current orientation. Unknown parts
// keep their size (BlueBrick draws a placeholder of it), or get 2x2 studs.
inline QSizeF areaSize(const core::Brick& b, parts::PartsLibrary& lib) {
    if (const auto fp = lib.footprint(b.partNumber, b.orientation)) return fp->size;
    return b.displayArea.isEmpty() ? QSizeF(2, 2) : b.displayArea.size();
}

// Sets displayArea for the brick's current orientation so its centre is
// `centre` (BlueBrick's Brick.Center, used by sets).
inline void placeByAreaCentre(core::Brick& b, QPointF centre, parts::PartsLibrary& lib) {
    const QSizeF size = areaSize(b, lib);
    b.displayArea = QRectF(centre.x() - size.width() / 2.0, centre.y() - size.height() / 2.0,
                           size.width(), size.height());
}

// Sets displayArea for the brick's current orientation so its sprite
// centre is `centre`.
inline void placeByImageCentre(core::Brick& b, QPointF centre, parts::PartsLibrary& lib) {
    placeByAreaCentre(b, centre - lib.imageOffset(b.partNumber, b.orientation), lib);
}

// Places the brick so its connection `index` sits at `world`.
inline void placeByConnection(core::Brick& b, int index, QPointF world, parts::PartsLibrary& lib) {
    const auto meta = lib.metadata(b.partNumber);
    QPointF centre = world;
    if (meta && index >= 0 && index < meta->connections.size())
        centre -= rotated(meta->connections[index].position, b.orientation);
    placeByImageCentre(b, centre, lib);
}

// Turns the brick to `orientation` around its sprite centre.
inline void rotateAroundImageCentre(core::Brick& b, float orientation, parts::PartsLibrary& lib) {
    const QPointF centre = imageCentre(b, lib);
    b.orientation = orientation;
    placeByImageCentre(b, centre, lib);
}

// Earlier versions of this app kept a brick's displayArea size when turning
// or placing it, and drew the sprite at the box centre. BlueBrick
// recomputes the size on load and keeps the stored corner, so it shows such
// bricks shifted. Re-sizes bricks whose box doesn't match their footprint,
// keeping the sprite where this app drew it; bricks saved by BlueBrick
// already match and are left untouched. Returns how many changed.
inline int fixStaleAreas(std::vector<core::Brick>& bricks, parts::PartsLibrary& lib) {
    int fixed = 0;
    for (auto& b : bricks) {
        const auto fp = lib.footprint(b.partNumber, b.orientation);
        if (!fp) continue;
        if (std::abs(fp->size.width() - b.displayArea.width()) <= 0.01
            && std::abs(fp->size.height() - b.displayArea.height()) <= 0.01) continue;
        placeByImageCentre(b, b.displayArea.center(), lib);
        ++fixed;
    }
    return fixed;
}

inline int fixStaleAreas(core::Map& map, parts::PartsLibrary& lib) {
    int fixed = 0;
    for (auto& layer : map.layers()) {
        if (layer->kind() == core::LayerKind::Brick)
            fixed += fixStaleAreas(static_cast<core::LayerBrick&>(*layer).bricks, lib);
    }
    return fixed;
}

}  // namespace bld::parts::placement
