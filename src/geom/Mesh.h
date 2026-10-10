#pragma once

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <array>
#include <vector>

namespace bld::geom {

// Minimal CPU-side 3D types so all importers (LDraw, Studio, LDD) feed
// into one common pipeline before rasterization. Convention: +Y is up
// (LDD's native frame), units are LDU, and the top-down view maps
// x -> image x, z -> image y. LDraw is -Y up, so bakeMeshFromLDraw
// rotates its output 180° about X (negates y and z) into this frame.
// LDrawMeshLoader::loadPart still returns raw LDraw part coordinates.
struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;

    constexpr Vec3 operator+(Vec3 o) const { return { x + o.x, y + o.y, z + o.z }; }
    constexpr Vec3 operator-(Vec3 o) const { return { x - o.x, y - o.y, z - o.z }; }
    constexpr Vec3 operator*(double k) const { return { x * k, y * k, z * k }; }
};

// 4x4 matrix in column-major layout. We only ever do
//   v' = M * v + t
// so storing as separate 3x3 + translation is plenty, but a full 4x4
// keeps composition trivial when we walk a chain of LDraw subfile
// transforms (each "1" line carries its own 12-element transform).
struct Mat4 {
    // Stored row-major — m[r][c]. Initial state is identity.
    std::array<std::array<double, 4>, 4> m{ {
        { 1, 0, 0, 0 },
        { 0, 1, 0, 0 },
        { 0, 0, 1, 0 },
        { 0, 0, 0, 1 },
    } };

    static Mat4 identity() { return Mat4{}; }

    // Build from a translation + 3x3 rotation block.
    // a..i are the 3x3 in row-major: a b c / d e f / g h i.
    static Mat4 fromTranslationAndRotation(Vec3 t,
                                            double a, double b, double c,
                                            double d, double e, double f,
                                            double g, double h, double i) {
        Mat4 r;
        r.m[0] = { a, b, c, t.x };
        r.m[1] = { d, e, f, t.y };
        r.m[2] = { g, h, i, t.z };
        r.m[3] = { 0, 0, 0, 1 };
        return r;
    }

    // Transform a position (treats the vec as homogeneous w=1).
    Vec3 transform(Vec3 p) const {
        return {
            m[0][0]*p.x + m[0][1]*p.y + m[0][2]*p.z + m[0][3],
            m[1][0]*p.x + m[1][1]*p.y + m[1][2]*p.z + m[1][3],
            m[2][0]*p.x + m[2][1]*p.y + m[2][2]*p.z + m[2][3],
        };
    }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                double s = 0;
                for (int k = 0; k < 4; ++k) s += m[i][k] * o.m[k][j];
                r.m[i][j] = s;
            }
        return r;
    }
};

// One filled triangle in a part's mesh, fully resolved to world coords.
// Color is the final per-tri color after walking the LDraw color
// inheritance chain (16 = inherit from parent ref, 24 = inherit edge).
// Quads from LDraw type-4 lines are split into two triangles at load.
// Per-vertex normals (transformed into world space alongside the
// vertex) drive the rasterizer's lighting pass — without them, flat
// surfaces render as featureless color blobs and you can't see
// studs on a baseplate top.
struct Triangle {
    Vec3   v[3];
    Vec3   n[3];   // world-space vertex normals; zero ⇒ skip lighting
    QColor color;
};

// A line segment between two world-space points. LDraw type-2 lines
// in a .dat file get loaded as Edges so the top-down rasterizer can
// stroke them as a wireframe overlay (essential for showing stud
// outlines on flat plates). Color is the resolved palette color.
struct Edge {
    Vec3   v[2];
    QColor color;
};

// A full part's geometry as a flat list of triangles in part-local
// coords. Library loaders return one Mesh per `.dat` they resolve.
// Subfile references are baked into the mesh by the resolver — the
// rasterizer never sees nested transforms.
struct Mesh {
    std::vector<Triangle> tris;
    std::vector<Edge>     edges;

    // Bbox helpers, lazily computed by callers as needed.
    QRectF bbox2dXZ() const {  // top-down projection: drop Y
        if (tris.empty()) return {};
        double xmin =  std::numeric_limits<double>::infinity();
        double xmax = -std::numeric_limits<double>::infinity();
        double zmin =  std::numeric_limits<double>::infinity();
        double zmax = -std::numeric_limits<double>::infinity();
        for (const auto& t : tris) {
            for (const auto& v : t.v) {
                xmin = std::min(xmin, v.x); xmax = std::max(xmax, v.x);
                zmin = std::min(zmin, v.z); zmax = std::max(zmax, v.z);
            }
        }
        return QRectF(QPointF(xmin, zmin), QPointF(xmax, zmax));
    }

    bool empty() const { return tris.empty(); }
};

}  // namespace bld::geom
