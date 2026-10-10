#pragma once

#include "../../geom/Mesh.h"

#include <QByteArray>
#include <QString>

namespace bld::import {

// Read LDD's binary `.g` brick-geometry format into a flat triangle
// list ready for the rasterizer.
//
// Format (reference: LU-Rebuilt/lu-assets src/lego/brick_geometry):
//
//   header (16 bytes, little-endian)
//     u32 magic        = 'B','G','0','1' little-endian == 0x42420D31
//                        (the file starts with "10GB" ASCII bytes)
//     u32 vertex_count
//     u32 index_count  (total indices; triangles = index_count / 3)
//     u32 options      bitflags:
//                        (options & 3)  == 3  → has UVs
//                        (options & 48) == 48 → has per-vertex bone index
//
//   vertex_count * 3 f32  positions (x, y, z)
//   vertex_count * 3 f32  normals  (nx, ny, nz)
//   if has_uvs: vertex_count * 2 f32  uvs (u, v)
//   index_count * u32     indices
//   if has_bones: u32 bone_length, then bone_length * u32 bone-indices
//
// Both the LU client and the LDD distribution use this same format,
// just with different `options` and trailing data. LDD additionally
// tacks on round-edge shader / average-normals / culling blocks AFTER
// the indices; we don't need any of that for top-down rendering, so
// we stop reading once we have the triangles.
//
// Coordinates are in LDD's "module" units — 1 module = 8 mm = 1 stud.
// To match the LDraw pipeline (LDU = 1/20 stud) we scale positions by
// 20 at parse time so the rasterizer + downstream code see one
// consistent unit system.
struct LDDGeomReadResult {
    bool       ok = false;
    QString    error;
    geom::Mesh mesh;          // tris in LDU, white color (caller paints)
};

// Parse a `.g` file's bytes. Always returns; on failure ok=false and
// error is set. Mesh tris carry an opaque white color — callers must
// repaint with the LDD material color before rasterizing.
LDDGeomReadResult readLDDGeom(const QByteArray& bytes);

}  // namespace bld::import
