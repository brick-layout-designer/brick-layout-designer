#include "LDrawRasterize.h"

#include "LDrawColors.h"

#include <QBrush>
#include <QPainter>
#include <QPen>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace bld::import {

namespace {

// 20 LDU = 1 stud (BlueBrick convention, matches the rest of the
// importer). We map LDU → studs once up front, then scale to pixels
// via `pxPerStud` inside the painter.
constexpr double kLduPerStud = 20.0;

// Project an LDU (x, y, z) onto the top-down plane, viewed from above.
// LDraw is -Y up, so looking down puts -Z ("back") at the bottom of the
// page: image y = -z. (Mapping +z to image y rendered a mirror image.)
QPointF projectStuds(const double v[3]) {
    return QPointF(v[0] / kLduPerStud, -v[2] / kLduPerStud);
}

struct Bounds {
    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    bool   isEmpty() const { return minX > maxX || minY > maxY; }
    void   add(const QPointF& p) {
        minX = std::min(minX, p.x());
        minY = std::min(minY, p.y());
        maxX = std::max(maxX, p.x());
        maxY = std::max(maxY, p.y());
    }
};

}  // namespace

QImage rasterizeTopDown(const LDrawReadResult& src,
                        double pxPerStud,
                        int marginPx) {
    if (src.primitives.empty()) return {};

    // First pass: axis-aligned bbox in studs. Determines canvas size.
    Bounds bb;
    for (const auto& p : src.primitives) {
        const int n = p.kind;
        for (int i = 0; i < n; ++i) {
            bb.add(projectStuds(p.v[i]));
        }
    }
    if (bb.isEmpty()) return {};

    const double widthStuds  = bb.maxX - bb.minX;
    const double heightStuds = bb.maxY - bb.minY;
    const int widthPx  = std::max(8,
        static_cast<int>(std::ceil(widthStuds  * pxPerStud)) + 2 * marginPx);
    const int heightPx = std::max(8,
        static_cast<int>(std::ceil(heightStuds * pxPerStud)) + 2 * marginPx);

    QImage img(widthPx, heightPx, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QPainter g(&img);
    g.setRenderHint(QPainter::Antialiasing);

    // Canvas transform: (stud bbox origin + margin) → (0, 0) at the
    // top-left of the image, with +X right and +Y down.
    g.translate(marginPx - bb.minX * pxPerStud,
                marginPx - bb.minY * pxPerStud);
    g.scale(pxPerStud, pxPerStud);

    // Painter's order: physically lower primitives first (LDraw is -Y
    // up, so height = -y) so tops cover what sits beneath them.
    std::vector<const LDrawPrimitive*> order;
    order.reserve(src.primitives.size());
    for (const auto& p : src.primitives) order.push_back(&p);
    const auto height = [](const LDrawPrimitive* p) {
        double h = -std::numeric_limits<double>::infinity();
        for (int i = 0; i < p->kind; ++i) h = std::max(h, -p->v[i][1]);
        return h;
    };
    std::stable_sort(order.begin(), order.end(),
                     [&](const LDrawPrimitive* a, const LDrawPrimitive* b) {
                         return height(a) < height(b);
                     });

    for (const LDrawPrimitive* pp : order) {
        const LDrawPrimitive& p = *pp;
        const QColor fill = ldrawColor(p.colorCode);
        // Convert the 2 / 3 / 4 vertices to screen-space polygon.
        QPolygonF poly;
        for (int i = 0; i < p.kind; ++i) poly << projectStuds(p.v[i]);

        switch (p.kind) {
            case 2: {
                // Thin black line for part edges — vanilla LDraw
                // renders type-2s as a silhouette outline.
                QPen pen(QColor(20, 20, 20));
                pen.setWidthF(0.1);  // 0.1 stud wide at current scale
                pen.setCosmetic(false);
                g.setPen(pen);
                g.drawLine(poly[0], poly[1]);
                break;
            }
            case 3:
            case 4: {
                g.setPen(Qt::NoPen);
                g.setBrush(QBrush(fill));
                g.drawPolygon(poly);
                break;
            }
        }
    }

    g.end();
    return img;
}

}  // namespace bld::import
