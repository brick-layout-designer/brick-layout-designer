#include "LDDGeomReader.h"

#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace bld::import {

namespace {

// LDD .g vertex coords are in LDD coordinate units. 1 LDD unit =
// 1.25 studs and 1 stud = 20 LDU, so 1 LDD unit = 25 LDU. Apply that
// scale here so .g triangles share the LDU coord system used by the
// LDraw mesh-loader and the rasterizer. Verified by an LXFML import
// where a 20-stud-wide brick measured 16 studs at 20× — the missing
// factor was exactly 25/20.
constexpr double kLddToLdu = 25.0;

bool readU32(const QByteArray& d, qsizetype& off, quint32& out) {
    if (off + 4 > d.size()) return false;
    out = qFromLittleEndian<quint32>(d.constData() + off);
    off += 4;
    return true;
}

bool readF32(const QByteArray& d, qsizetype& off, float& out) {
    if (off + 4 > d.size()) return false;
    quint32 raw = qFromLittleEndian<quint32>(d.constData() + off);
    std::memcpy(&out, &raw, 4);
    off += 4;
    return true;
}

}  // namespace

LDDGeomReadResult readLDDGeom(const QByteArray& bytes) {
    LDDGeomReadResult r;
    qsizetype off = 0;
    if (bytes.size() < 16) {
        r.error = QStringLiteral("File too small to contain a .g header (%1 bytes)")
                      .arg(bytes.size());
        return r;
    }
    // Magic bytes: ASCII "10GB" → 0x42 0x47 0x30 0x31 read as LE u32.
    // Verified against both LU brickprimitives/*.g and LDD
    // Assets/db/Primitives/LOD0/*.g.
    if (bytes.left(4) != QByteArray("10GB", 4)) {
        r.error = QStringLiteral("Not a .g file (magic mismatch)");
        return r;
    }
    off = 4;
    quint32 vCount = 0, iCount = 0, options = 0;
    if (!readU32(bytes, off, vCount) ||
        !readU32(bytes, off, iCount) ||
        !readU32(bytes, off, options)) {
        r.error = QStringLiteral("Truncated header");
        return r;
    }
    if (iCount % 3 != 0) {
        r.error = QStringLiteral("Index count not divisible by 3 (%1)").arg(iCount);
        return r;
    }

    const bool hasUVs   = (options & 3)  == 3;
    const bool hasBones = (options & 48) == 48;
    (void)hasBones;  // bone data is at the tail and skipped — we only need pos+tri.

    // Position buffer.
    // Positions and normals, 12 bytes each, must fit before anything is
    // reserved: a 16-byte header can claim four billion vertices.
    if (qsizetype(vCount) * 24 > bytes.size() - off) {
        r.error = QStringLiteral("Truncated vertex data (%1 vertices)").arg(vCount);
        return r;
    }
    std::vector<geom::Vec3> positions;
    positions.reserve(vCount);
    for (quint32 i = 0; i < vCount; ++i) {
        float x, y, z;
        if (!readF32(bytes, off, x) || !readF32(bytes, off, y) || !readF32(bytes, off, z)) {
            r.error = QStringLiteral("Truncated vertex positions at vertex %1").arg(i);
            return r;
        }
        positions.push_back(geom::Vec3{ x * kLddToLdu, y * kLddToLdu, z * kLddToLdu });
    }
    // Normals — we keep these to detect "top-facing" vertices (stud
    // rim tracing in the wireframe overlay). LDD `.g` stores one
    // normal per vertex; we don't transform them yet because the bake
    // pipeline applies the world transform later.
    std::vector<geom::Vec3> normals;
    if (off + qsizetype(vCount) * 12 > bytes.size()) {
        r.error = QStringLiteral("Truncated vertex normals");
        return r;
    }
    normals.reserve(vCount);
    for (quint32 i = 0; i < vCount; ++i) {
        float nx, ny, nz;
        if (!readF32(bytes, off, nx) || !readF32(bytes, off, ny) || !readF32(bytes, off, nz)) {
            r.error = QStringLiteral("Truncated normal at vertex %1").arg(i);
            return r;
        }
        normals.push_back(geom::Vec3{ nx, ny, nz });
    }

    // Skip UVs when present.
    if (hasUVs) {
        if (off + qsizetype(vCount) * 8 > bytes.size()) {
            r.error = QStringLiteral("Truncated UVs");
            return r;
        }
        off += qsizetype(vCount) * 8;
    }

    // Indices. Triangle = (i*3, i*3+1, i*3+2). Each index is a u32.
    const quint32 triCount = iCount / 3;
    if (off + qsizetype(iCount) * 4 > bytes.size()) {
        r.error = QStringLiteral("Truncated index buffer");
        return r;
    }
    r.mesh.tris.reserve(triCount);
    const QColor whiteOpaque = QColor::fromRgb(220, 220, 220);

    // First pass: read every triangle, store (indices + face normal).
    // We need two passes because crease detection looks at the *pair*
    // of triangles sharing an edge — we can only know that after every
    // triangle has been seen.
    struct FaceTri { quint32 ia, ib, ic; geom::Vec3 fn; };
    std::vector<FaceTri> faces;
    faces.reserve(triCount);
    for (quint32 t = 0; t < triCount; ++t) {
        quint32 ia, ib, ic;
        if (!readU32(bytes, off, ia) || !readU32(bytes, off, ib) || !readU32(bytes, off, ic)) {
            r.error = QStringLiteral("Truncated index buffer at triangle %1").arg(t);
            return r;
        }
        if (ia >= vCount || ib >= vCount || ic >= vCount) {
            r.error = QStringLiteral("Index out of range at triangle %1 (%2,%3,%4 vs %5)")
                          .arg(t).arg(ia).arg(ib).arg(ic).arg(vCount);
            return r;
        }
        geom::Triangle tri;
        tri.v[0] = positions[ia];
        tri.v[1] = positions[ib];
        tri.v[2] = positions[ic];
        tri.n[0] = normals[ia];
        tri.n[1] = normals[ib];
        tri.n[2] = normals[ic];
        tri.color = whiteOpaque;
        r.mesh.tris.push_back(tri);

        // Face normal from the geometry (not the per-vertex normals,
        // which are smoothed across cylinder walls and would falsely
        // collapse the wall/cap crease). Right-hand rule on (v1-v0,
        // v2-v0).
        const geom::Vec3 e1{ positions[ib].x - positions[ia].x,
                             positions[ib].y - positions[ia].y,
                             positions[ib].z - positions[ia].z };
        const geom::Vec3 e2{ positions[ic].x - positions[ia].x,
                             positions[ic].y - positions[ia].y,
                             positions[ic].z - positions[ia].z };
        geom::Vec3 fn{
            e1.y*e2.z - e1.z*e2.y,
            e1.z*e2.x - e1.x*e2.z,
            e1.x*e2.y - e1.y*e2.x };
        const double fnLen = std::sqrt(fn.x*fn.x + fn.y*fn.y + fn.z*fn.z);
        if (fnLen > 1e-9) { fn.x /= fnLen; fn.y /= fnLen; fn.z /= fnLen; }
        faces.push_back({ ia, ib, ic, fn });
    }

    // Second pass: build edge → adjacent-faces map, then emit only
    // crease edges between two top-facing triangles. That's exactly
    // the stud-top rim where the cylinder wall meets the flat cap —
    // the visual outline we want without the interior fan diagonals.
    // Encode edge as (min<<32 | max) for cheap hashing.
    std::unordered_map<quint64, std::vector<quint32>> edgeToFaces;
    edgeToFaces.reserve(triCount * 3);
    auto edgeKey = [](quint32 a, quint32 b) -> quint64 {
        const quint32 lo = std::min(a, b), hi = std::max(a, b);
        return (quint64(hi) << 32) | quint64(lo);
    };
    for (quint32 t = 0; t < faces.size(); ++t) {
        const auto& f = faces[t];
        edgeToFaces[edgeKey(f.ia, f.ib)].push_back(t);
        edgeToFaces[edgeKey(f.ib, f.ic)].push_back(t);
        edgeToFaces[edgeKey(f.ic, f.ia)].push_back(t);
    }
    const QColor edgeColor = QColor::fromRgb(40, 40, 40, 220);
    r.mesh.edges.reserve(triCount);
    for (const auto& [key, adjFaces] : edgeToFaces) {
        const quint32 ia = static_cast<quint32>(key & 0xFFFFFFFFu);
        const quint32 ib = static_cast<quint32>(key >> 32);

        // Gate by FACE normal, not vertex normal: a vertex sitting on
        // a cylinder seam has its smooth-shaded normal averaged with
        // the side wall, so its y-component drops well below 0.7 even
        // though that vertex IS on a top-facing cap triangle. Using
        // the face normal of the adjacent triangle is exact: the cap
        // triangle's geometric normal really does point straight up.
        // We require AT LEAST ONE adjacent face to be top-facing —
        // that's the visibility criterion (the edge has to bound a
        // surface visible from above) — and then apply the crease
        // filter to drop interior fan diagonals.
        bool anyTopFacing = false;
        for (quint32 fi : adjFaces) {
            if (faces[fi].fn.y > 0.7) { anyTopFacing = true; break; }
        }
        if (!anyTopFacing) continue;

        // Decide if this edge is a real outline:
        //  - Boundary edges (shared by 1 face) — silhouette of an
        //    open mesh, always draw.
        //  - Interior edges — draw only when the two adjacent faces
        //    have noticeably different face normals (a crease ≥ ~25°).
        //    Across a flat tile/stud top both faces point straight up
        //    (dot ≈ 1) → the diagonal is dropped. At the rim of a tile
        //    or stud the top-facing cap meets a side-wall whose normal
        //    is horizontal (dot ≈ 0) → we keep the silhouette.
        //  - Edges shared by 3+ faces (non-manifold) — keep, treat as
        //    feature edges.
        bool keep = false;
        if (adjFaces.size() == 1 || adjFaces.size() >= 3) {
            keep = true;
        } else {
            const geom::Vec3& na = faces[adjFaces[0]].fn;
            const geom::Vec3& nb = faces[adjFaces[1]].fn;
            const double dot = na.x*nb.x + na.y*nb.y + na.z*nb.z;
            // cos(25°) ≈ 0.906. Anything below that is a visible crease.
            if (dot < 0.906) keep = true;
        }
        if (!keep) continue;

        geom::Edge e;
        e.v[0] = positions[ia];
        e.v[1] = positions[ib];
        e.color = edgeColor;
        r.mesh.edges.push_back(e);
    }
    r.ok = true;
    return r;
}

}  // namespace bld::import
