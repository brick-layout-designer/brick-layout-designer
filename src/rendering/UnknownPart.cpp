#include "UnknownPart.h"

#include "MapText.h"

#include <QBrush>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace bld::rendering {

UnknownPartLook unknownPartLook(const QString& partNumber, double widthStuds, double heightStuds) {
    constexpr double kPxPerStud = 8;  // vanilla's part pictures, and the scene
    UnknownPartLook l;
    l.width = std::trunc(widthStuds) * kPxPerStud;
    l.height = std::trunc(heightStuds) * kPxPerStud;
    l.penPx = std::max(1.0, std::trunc(std::max(l.width, l.height) / 16));
    const double pt = partNumber.isEmpty()
                          ? 4.0
                          : std::max(4.0, std::min(l.width, l.height) / static_cast<double>(partNumber.size()));
    l.fontPx = pt * 4 / 3;
    return l;
}

void addUnknownPartDrawing(QGraphicsItem* parent, const QString& partNumber, const UnknownPartLook& look) {
    const double x = -look.width / 2, y = -look.height / 2;
    QPen pen(QColor(255, 0, 0));
    pen.setWidthF(look.penPx);
    pen.setCapStyle(Qt::FlatCap);
    for (const QLineF& l : { QLineF(x, y, -x, -y), QLineF(x, -y, -x, y) }) {
        auto* line = new QGraphicsLineItem(l, parent);
        line->setPen(pen);
    }
    if (partNumber.isEmpty()) return;
    // Konva sizes text in fractional px; the outline is drawn at the
    // rounded size and scaled to the exact one.
    const QFont f = mapFont(QString(), look.fontPx);
    const double s = look.fontPx / f.pixelSize();
    const double lw = mapLineWidth(QString())(partNumber, f.pixelSize());
    auto* text = new QGraphicsPathItem(
        textPath(f, { { partNumber, (look.width / s - lw) / 2, 0 } }, kMapLineHeight), parent);
    text->setPen(Qt::NoPen);
    text->setBrush(QColor(0, 0, 0));
    QTransform tr;
    tr.translate(x, -(look.fontPx * kMapLineHeight) / 2);
    tr.scale(s, s);
    text->setTransform(tr);
}

}  // namespace bld::rendering
