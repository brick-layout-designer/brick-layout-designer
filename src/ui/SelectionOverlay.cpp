#include "SelectionOverlay.h"

#include "SelectionStyle.h"

#include <QBrush>
#include <QPainter>
#include <QPen>

namespace bld::ui {

SelectionOverlay::SelectionOverlay() {
    setZValue(1e9);
    setFlag(QGraphicsItem::ItemIsSelectable, false);
    setFlag(QGraphicsItem::ItemIsMovable,    false);
    // Pure visual: never respond to clicks. Without this, the overlay's
    // enormous boundingRect can intercept itemAt() hit-tests and steal
    // the grab away from the brick the user actually clicked.
    setAcceptedMouseButtons(Qt::NoButton);
    setAcceptHoverEvents(false);
}


void SelectionOverlay::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) {
    if (polys_.isEmpty() && bands_.isEmpty()) return;
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    using namespace selection;

    for (const RulerBand& b : bands_) {
        QPen band(kRulerHalo);
        band.setWidthF(b.width);
        band.setCapStyle(Qt::FlatCap);
        p->setPen(band);
        p->setBrush(Qt::NoBrush);
        if (b.circle) p->drawEllipse(b.centre, b.radius, b.radius);
        else p->drawLine(b.line);
    }

    const QColor inColor = snapActive_ ? kSnapStroke : kPartTint;
    QColor fillColor = snapActive_ ? kSnapFill : kPartTint;
    if (!snapActive_) fillColor.setAlpha(kPartFillAlpha);
    QPen outer(kPartOuter); outer.setWidthF(kPartOuterWidth); outer.setCosmetic(true);
    outer.setJoinStyle(Qt::MiterJoin);
    QPen inner(inColor); inner.setWidthF(kPartInnerWidth); inner.setCosmetic(true);
    inner.setJoinStyle(Qt::MiterJoin);
    const QBrush fill(fillColor);

    for (const QPolygonF& poly : polys_) {
        p->setPen(outer); p->setBrush(Qt::NoBrush); p->drawPolygon(poly);
        p->setPen(inner); p->setBrush(fill);        p->drawPolygon(poly);
    }

    if (snapActive_) {
        QPen ring(QColor(20, 180, 80)); ring.setWidthF(3.0); ring.setCosmetic(true);
        p->setPen(ring);
        p->setBrush(QColor(80, 255, 120, 80));
        p->drawEllipse(snapPoint_, 10.0, 10.0);
    }
    p->restore();
}

void SelectionOverlay::setRulerBands(QList<RulerBand> bands) {
    prepareGeometryChange();
    bands_ = std::move(bands);
    setOutlines(polys_);
}

void SelectionOverlay::setOutlines(QList<QPolygonF> polys) {
    prepareGeometryChange();
    polys_ = std::move(polys);
    QRectF total;
    for (const QPolygonF& poly : polys_) {
        total = total.united(poly.boundingRect());
    }
    for (const RulerBand& b : bands_) {
        const QRectF r = b.circle ? QRectF(b.centre - QPointF(b.radius, b.radius), QSizeF(2 * b.radius, 2 * b.radius))
                                  : QRectF(b.line.p1(), b.line.p2()).normalized();
        total = total.united(r.adjusted(-b.width, -b.width, b.width, b.width));
    }
    if (snapActive_)
        total = total.united(QRectF(snapPoint_ - QPointF(12, 12), QSizeF(24, 24)));
    bounds_ = total.isEmpty() ? QRectF() : total.adjusted(-6, -6, 6, 6);
    update();
}

void SelectionOverlay::setSnapState(bool active, QPointF snapPoint) {
    prepareGeometryChange();
    snapActive_ = active;
    snapPoint_  = snapPoint;
    update();
}

}  // namespace bld::ui
