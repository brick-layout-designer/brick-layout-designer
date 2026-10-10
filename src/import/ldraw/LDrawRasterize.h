#pragma once

#include "LDrawReader.h"

#include <QImage>

namespace bld::import {

// Top-down rasterize every primitive (type 2 / 3 / 4) in `src` onto a
// QImage at the supplied `pxPerStud` resolution. LDraw's Y axis is up;
// we project onto the XZ plane and flip Y so "positive Z forward"
// reads as "down the page" (matches our BlueBrick convention where
// the first curve in a set points roughly up-right).
//
// Returns a transparent QImage sized to the XZ bounding box of the
// primitives, padded by a small margin. Null QImage if there are
// zero primitives to draw.
//
// Intended for fallback use when an LDraw / Studio / LDD file's
// referenced subparts don't resolve in our BlueBrickParts library —
// rendering the inline primitives at least gives the user a tinted
// silhouette rather than a dashed placeholder.
//
// Triangles and quads are filled with the LDraw color for that
// primitive (via LDrawColors lookup). Lines are stroked thin black.
// Transparent color codes honour their alpha so windscreens / glass
// render as see-through overlays.
QImage rasterizeTopDown(const LDrawReadResult& src,
                        double pxPerStud = 8.0,
                        int marginPx = 4);

}  // namespace bld::import
