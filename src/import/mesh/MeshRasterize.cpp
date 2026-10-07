#include "MeshRasterize.h"

#include <QColor>
#include <QMargins>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <vector>

namespace bld::import {

namespace {

// Per-pixel z-buffering replaces painter's-algorithm sort. With
// thousands of overlapping bricks, no sort key (min-Y, max-Y, centroid)
// is correct everywhere — the same triangle can be both above one face
// and below another. Z-buffer is exact at the pixel level. lxfml-viewer's
// clean output is what proper depth testing looks like.
//
// We rasterise each triangle in the XZ plane with barycentric edge
// functions, interpolate the world-Y at each fragment, and keep the
// fragment with the maximum Y (closest to the top-down camera; LDD is
// Y-up). Anti-aliasing is supersampled — render at (final px/stud × ssaa)
// resolution, then bilinear-downscale.

struct Frag {
    float    yWorld;   // depth (interpolated world Y; bigger = closer to camera)
    quint32  argb;     // packed ARGB premultiplied
};

// Pack QColor into premultiplied ARGB32. Zero alpha collapses to 0
// so the empty-cell sentinel stays transparent.
inline quint32 packPremul(QColor c) {
    const int a = c.alpha();
    if (a == 0) return 0u;
    const int r = (c.red()   * a + 127) / 255;
    const int g = (c.green() * a + 127) / 255;
    const int b = (c.blue()  * a + 127) / 255;
    return (static_cast<quint32>(a) << 24) |
           (static_cast<quint32>(r) << 16) |
           (static_cast<quint32>(g) <<  8) |
            static_cast<quint32>(b);
}

}  // namespace

std::optional<QRectF> baseLattice(const geom::Mesh& mesh, double studsPerLdu) {
    // The bottom layer: the lowest level of flat (horizontal) faces that
    // covers a real share of the model, with the sizeable ones up to a few
    // LDU above it (a plate is 8 LDU; its underside, tubes and walls'
    // bottom edges sit there). A part hanging below the base (a chain, a
    // lamp) covers too little to count, at its height or in the band.
    constexpr double kBandLdu = 4.0;
    constexpr double kMinShare = 0.05;
    // LDD shapes are 0.1 mm short of the stud on every side (0.025 stud).
    constexpr double kWholeSlackStuds = 0.06;
    struct Level {
        double area = 0;
        QRectF box;  // studs
    };
    std::map<long long, Level> levels;  // by height, whole LDU
    double bx0 = std::numeric_limits<double>::infinity(), bx1 = -bx0, bz0 = bx0, bz1 = -bx0;
    for (const auto& t : mesh.tris) {
        for (const auto& v : t.v) {
            bx0 = std::min(bx0, v.x); bx1 = std::max(bx1, v.x);
            bz0 = std::min(bz0, v.z); bz1 = std::max(bz1, v.z);
        }
        const double y = t.v[0].y;
        if (std::abs(t.v[1].y - y) > 0.01 || std::abs(t.v[2].y - y) > 0.01) continue;
        const double area = std::abs((t.v[1].x - t.v[0].x) * (t.v[2].z - t.v[0].z)
                                     - (t.v[2].x - t.v[0].x) * (t.v[1].z - t.v[0].z)) / 2.0;
        if (area <= 0) continue;
        double x0 = t.v[0].x, x1 = x0, z0 = t.v[0].z, z1 = z0;
        for (const auto& v : t.v) {
            x0 = std::min(x0, v.x); x1 = std::max(x1, v.x);
            z0 = std::min(z0, v.z); z1 = std::max(z1, v.z);
        }
        Level& l = levels[std::llround(std::floor(y))];
        l.area += area;
        l.box |= QRectF(QPointF(x0 * studsPerLdu, z0 * studsPerLdu), QPointF(x1 * studsPerLdu, z1 * studsPerLdu));
    }
    const double total = (bx1 - bx0) * (bz1 - bz0);
    if (!(total > 0)) return std::nullopt;
    for (auto it = levels.cbegin(); it != levels.cend(); ++it) {
        if (it->second.area < kMinShare * total) continue;
        // With the faces just above it that are more than specks.
        QRectF box;
        for (auto band = it; band != levels.cend() && band->first <= it->first + kBandLdu; ++band)
            if (band->second.area >= kMinShare / 5 * total) box |= band->second.box;
        const double w = box.width(), h = box.height();
        const double wr = std::round(w), hr = std::round(h);
        // Not whole studs (a curve, a base turned at an angle): no lattice.
        if (wr < 1 || hr < 1 || std::abs(w - wr) > kWholeSlackStuds || std::abs(h - hr) > kWholeSlackStuds)
            return std::nullopt;
        // Centred on the bottom layer, so LDD's shortfall splits evenly.
        const QPointF c = box.center();
        return QRectF(c.x() - wr / 2.0, c.y() - hr / 2.0, wr, hr);
    }
    return std::nullopt;
}

RasterizeResult rasterizeMeshTopDown(const geom::Mesh& mesh,
                                      const RasterizeOptions& opt) {
    RasterizeResult out;
    if (mesh.tris.empty()) return out;

    // Compute bounding box in stud space. Mesh is in LDU; convert
    // when projecting.
    double xmin =  std::numeric_limits<double>::infinity();
    double xmax = -std::numeric_limits<double>::infinity();
    double zmin =  std::numeric_limits<double>::infinity();
    double zmax = -std::numeric_limits<double>::infinity();
    for (const auto& t : mesh.tris) {
        for (const auto& v : t.v) {
            const double xs = v.x * opt.studsPerLdu;
            const double zs = v.z * opt.studsPerLdu;
            xmin = std::min(xmin, xs); xmax = std::max(xmax, xs);
            zmin = std::min(zmin, zs); zmax = std::max(zmax, zs);
        }
    }
    out.meshBoundsXZ = QRectF(QPointF(xmin, zmin), QPointF(xmax, zmax));

    // Canvas = the mesh bounds padded out to whole studs, centred on the
    // mesh. Placement derives the part's footprint as pixels / pxPerStud,
    // so whole studs keep grid snapping clean; padding (rather than
    // scaling the mesh to fit) keeps every feature at its true stud
    // position, which is what imported connection points are measured
    // against. Overhangs under kSnapSlackStuds are trimmed instead of
    // adding a whole stud of padding.
    constexpr double kSnapSlackStuds = 0.05;
    if (const auto base = baseLattice(mesh, opt.studsPerLdu)) {
        // The bottom layer is flat and whole studs: its studs go on the
        // grid. Overhangs above it add whole studs around it.
        const auto extra = [](double overhang) {
            return std::max(0, static_cast<int>(std::ceil(overhang - kSnapSlackStuds)));
        };
        const int left = extra(base->left() - xmin), right = extra(xmax - base->right());
        const int top = extra(base->top() - zmin), bottom = extra(zmax - base->bottom());
        out.snapMargin = QMargins(left, top, right, bottom);
        out.spriteStuds = QRectF(base->left() - left, base->top() - top,
                                 qRound(base->width()) + left + right, qRound(base->height()) + top + bottom);
    } else {
        const int w = std::max(1, static_cast<int>(std::ceil((xmax - xmin) - kSnapSlackStuds)));
        const int h = std::max(1, static_cast<int>(std::ceil((zmax - zmin) - kSnapSlackStuds)));
        const QPointF centre = out.meshBoundsXZ.center();
        out.spriteStuds = QRectF(centre.x() - w / 2.0, centre.y() - h / 2.0, w, h);
    }
    const int wStud = qRound(out.spriteStuds.width());
    const int hStud = qRound(out.spriteStuds.height());
    const double canvasX0 = out.spriteStuds.left();
    const double canvasZ0 = out.spriteStuds.top();

    // Render at supersampled px/stud, then downscale by exactly `ssaa`.
    // Big models supersample less: at 4x a 50 x 50 stud set needed a
    // 40-megapixel buffer (over 300 MB) for no visible gain.
    constexpr qint64 kMaxSuperPixels = 16'000'000;
    const auto superSize = [&](int s) {
        return qint64(wStud * opt.pxPerStud * s + 2 * opt.marginPx * s)
             * qint64(hStud * opt.pxPerStud * s + 2 * opt.marginPx * s);
    };
    int ssaa = std::max(1, opt.ssaa);
    while (ssaa > 1 && superSize(ssaa) > kMaxSuperPixels) --ssaa;
    const int superPxPerStud = opt.pxPerStud * ssaa;
    const int superMarginPx  = opt.marginPx  * ssaa;

    const int W = wStud * superPxPerStud + 2 * superMarginPx;
    const int H = hStud * superPxPerStud + 2 * superMarginPx;

    out.imageOriginInStuds = QPointF(canvasX0 - opt.marginPx / static_cast<double>(opt.pxPerStud),
                                      canvasZ0 - opt.marginPx / static_cast<double>(opt.pxPerStud));

    // Z-buffer + colour buffer. Initial depth is -inf so any valid
    // fragment beats it; initial colour is transparent.
    std::vector<Frag> buf(static_cast<size_t>(W) * static_cast<size_t>(H));
    for (auto& f : buf) {
        f.yWorld = -std::numeric_limits<float>::infinity();
        f.argb   = 0u;
    }

    // Directional light from straight up — top-down sprite, so the
    // light shares the camera direction. Heavy ambient so flat tile
    // tops (e.g. hazard-stripe tiles laid flat) read as solid colour
    // blocks instead of getting broken up by stud-cylinder shading
    // from neighbouring bricks.
    constexpr double kLx = 0.0, kLy = 1.0, kLz = 0.0;
    constexpr double kAmbient = 0.85;
    constexpr double kDiffuse = 0.15;

    // Rasterise each triangle. Project each vertex into (px, py) screen
    // space, interpolate world Y for depth and the world-space normal
    // for lighting.
    for (const auto& tri : mesh.tris) {
        double px[3], py[3], wy[3];
        for (int k = 0; k < 3; ++k) {
            const double xs = tri.v[k].x * opt.studsPerLdu;
            const double zs = tri.v[k].z * opt.studsPerLdu;
            px[k] = (xs - canvasX0) * superPxPerStud + superMarginPx;
            py[k] = (zs - canvasZ0) * superPxPerStud + superMarginPx;
            wy[k] = tri.v[k].y;
        }

        double bxMin = std::min({px[0], px[1], px[2]});
        double bxMax = std::max({px[0], px[1], px[2]});
        double byMin = std::min({py[0], py[1], py[2]});
        double byMax = std::max({py[0], py[1], py[2]});
        const int xLo = std::max(0, static_cast<int>(std::floor(bxMin)));
        const int xHi = std::min(W - 1, static_cast<int>(std::ceil(bxMax)));
        const int yLo = std::max(0, static_cast<int>(std::floor(byMin)));
        const int yHi = std::min(H - 1, static_cast<int>(std::ceil(byMax)));
        if (xLo > xHi || yLo > yHi) continue;

        const double dx10 = px[1] - px[0], dy10 = py[1] - py[0];
        const double dx20 = px[2] - px[0], dy20 = py[2] - py[0];
        const double denom = dx10 * dy20 - dy10 * dx20;
        if (std::abs(denom) < 1e-9) continue;
        const double invDenom = 1.0 / denom;

        // Per-triangle pre-shade. We use the average of the three
        // vertex normals (flat shade, sufficient at sprite resolution
        // and avoids per-pixel barycentric interpolation of normals).
        double nx = tri.n[0].x + tri.n[1].x + tri.n[2].x;
        double ny = tri.n[0].y + tri.n[1].y + tri.n[2].y;
        double nz = tri.n[0].z + tri.n[1].z + tri.n[2].z;
        const double nLen = std::sqrt(nx*nx + ny*ny + nz*nz);
        double shade = 1.0;
        if (nLen > 1e-6) {
            nx /= nLen; ny /= nLen; nz /= nLen;
            const double NdotL = nx * kLx + ny * kLy + nz * kLz;
            shade = kAmbient + kDiffuse * std::max(0.0, NdotL);
            if (shade > 1.0) shade = 1.0;
        }

        // Apply shading to the triangle's base colour.
        const QColor base = tri.color;
        const int alpha = base.alpha();
        if (alpha == 0) continue;
        const int rr = static_cast<int>(std::lround(base.red()   * shade));
        const int gg = static_cast<int>(std::lround(base.green() * shade));
        const int bb = static_cast<int>(std::lround(base.blue()  * shade));
        const int rrC = std::clamp(rr, 0, 255);
        const int ggC = std::clamp(gg, 0, 255);
        const int bbC = std::clamp(bb, 0, 255);
        const quint32 argb = packPremul(QColor::fromRgb(rrC, ggC, bbC, alpha));
        if (argb == 0u) continue;

        for (int yy = yLo; yy <= yHi; ++yy) {
            const double sampY = yy + 0.5;
            for (int xx = xLo; xx <= xHi; ++xx) {
                const double sampX = xx + 0.5;
                const double dx = sampX - px[0], dy = sampY - py[0];
                const double b1 = (dx * dy20 - dy * dx20) * invDenom;
                const double b2 = (dy * dx10 - dx * dy10) * invDenom;
                const double b0 = 1.0 - b1 - b2;
                if (b0 < -1e-7 || b1 < -1e-7 || b2 < -1e-7) continue;
                const float depth = static_cast<float>(b0 * wy[0] + b1 * wy[1] + b2 * wy[2]);
                Frag& f = buf[static_cast<size_t>(yy) * W + static_cast<size_t>(xx)];
                if (depth > f.yWorld) {
                    f.yWorld = depth;
                    f.argb   = argb;
                }
            }
        }
    }

    // Materialise the supersampled image from the colour buffer.
    QImage img(W, H, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    for (int yy = 0; yy < H; ++yy) {
        auto* row = reinterpret_cast<quint32*>(img.scanLine(yy));
        const Frag* src = buf.data() + static_cast<size_t>(yy) * W;
        for (int xx = 0; xx < W; ++xx) row[xx] = src[xx].argb;
    }

    // Downscale by exactly `ssaa`, so the final image is whole studs at
    // pxPerStud (plus margin) with no distortion.
    const int finalW = wStud * opt.pxPerStud + 2 * opt.marginPx;
    const int finalH = hStud * opt.pxPerStud + 2 * opt.marginPx;
    out.image = (ssaa > 1)
        ? img.scaled(finalW, finalH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        : std::move(img);

    // Wireframe overlay. Stud rims (LDD-synthesised) and brick-top
    // silhouettes (LDraw type-2 edges) trace the visible top of the
    // model with thin dark lines. Depth-tested against the supersampled
    // buffer so an edge that lies under a brick above it is skipped.
    if (opt.wireframe && !mesh.edges.empty() && !out.image.isNull()) {
        QPainter p(&out.image);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen;
        pen.setWidthF(std::max(0.5, opt.pxPerStud / 32.0));
        pen.setCapStyle(Qt::RoundCap);
        // Final-image (x,y) → supersampled depth-buffer Y at that pixel.
        const double scaleSX = double(W) / out.image.width();
        const double scaleSZ = double(H) / out.image.height();
        auto bufDepthAt = [&](double finalX, double finalY) -> float {
            const int sx = static_cast<int>(finalX * scaleSX);
            const int sz = static_cast<int>(finalY * scaleSZ);
            if (sx < 0 || sx >= W || sz < 0 || sz >= H) {
                return -std::numeric_limits<float>::infinity();
            }
            return buf[static_cast<size_t>(sz) * W + static_cast<size_t>(sx)].yWorld;
        };
        for (const auto& e : mesh.edges) {
            const double xs0 = e.v[0].x * opt.studsPerLdu;
            const double zs0 = e.v[0].z * opt.studsPerLdu;
            const double xs1 = e.v[1].x * opt.studsPerLdu;
            const double zs1 = e.v[1].z * opt.studsPerLdu;
            const double x0 = (xs0 - canvasX0) * opt.pxPerStud + opt.marginPx;
            const double y0 = (zs0 - canvasZ0) * opt.pxPerStud + opt.marginPx;
            const double x1 = (xs1 - canvasX0) * opt.pxPerStud + opt.marginPx;
            const double y1 = (zs1 - canvasZ0) * opt.pxPerStud + opt.marginPx;

            // Depth test: skip edges that are occluded by a brick
            // above them. Sample three points along the edge and
            // require all three to be visible (within tolerance) so
            // partly-occluded edges still render where they're seen.
            const double midY = 0.5 * (e.v[0].y + e.v[1].y);
            int visibleSamples = 0;
            for (int s = 0; s < 3; ++s) {
                const double t = (s + 1) / 4.0;  // 0.25, 0.5, 0.75
                const double sx = x0 + (x1 - x0) * t;
                const double sy = y0 + (y1 - y0) * t;
                const double sampY = e.v[0].y + (e.v[1].y - e.v[0].y) * t;
                const float bufY = bufDepthAt(sx, sy);
                if (!std::isfinite(bufY) || bufY - sampY <= 1.0) ++visibleSamples;
            }
            if (visibleSamples == 0) continue;
            (void)midY;

            QColor c = e.color;
            if (!c.isValid()) c = QColor(40, 40, 40);
            else c.setAlpha(160);
            pen.setColor(c);
            p.setPen(pen);
            p.drawLine(QPointF(x0, y0), QPointF(x1, y1));
        }
        p.end();
    }

    return out;
}

}  // namespace bld::import
