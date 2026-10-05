#include "SelectionOverlay.h"

#include "SelectionStyle.h"

#include "../rendering/MapText.h"
#include "../rendering/ModuleLabels.h"

#include <QBrush>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>

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
    if (polys_.isEmpty() && bands_.isEmpty() && !snapActive_ && !moving_ && names_.isEmpty()) return;
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

    // Screen-sized marks: radii are divided by the painter's zoom, pens are cosmetic.
    const QTransform& wt = p->worldTransform();
    const double scale = std::max(1e-6, std::hypot(wt.m11(), wt.m12()));
    if (snapActive_) {
        using namespace snapmarks;
        QPen halo(kHalo); halo.setWidthF(kHaloWidth); halo.setCosmetic(true);
        p->setPen(halo);
        p->setBrush(Qt::NoBrush);
        const double hr = (kRingRadius + kRingWidth) / scale;
        p->drawEllipse(snapPoint_, hr, hr);
        QPen ring(kRing); ring.setWidthF(kRingWidth); ring.setCosmetic(true);
        p->setPen(ring);
        p->setBrush(kRingFill);
        p->drawEllipse(snapPoint_, kRingRadius / scale, kRingRadius / scale);
    }
    if (moving_) {
        using namespace snapmarks;
        QPen halo(kHalo); halo.setWidthF(kDotHaloWidth); halo.setCosmetic(true);
        p->setPen(halo);
        p->setBrush(kDot);
        p->drawEllipse(*moving_, kDotRadius / scale, kDotRadius / scale);
    }

    for (const FullName& n : names_) {
        p->save();
        p->setTransform(n.toScene, true);
        p->setPen(Qt::NoPen);
        p->setBrush(rendering::kModuleFullNameBackground);
        p->drawRoundedRect(n.box, n.box.height() / 2, n.box.height() / 2);
        p->setBrush(n.fill);
        p->drawPath(rendering::textPath(n.font, { { n.name, n.textTopLeft.x(), n.textTopLeft.y() } },
                                        rendering::kModuleNameLineHeight));
        p->restore();
    }
    p->restore();
}

void SelectionOverlay::setFullNames(QList<FullName> names) {
    names_ = std::move(names);
    setOutlines(polys_);
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
    total = total.united(marksRect());
    for (const FullName& n : names_) total = total.united(n.toScene.mapRect(n.box));
    bounds_ = total.isEmpty() ? QRectF() : total.adjusted(-6, -6, 6, 6);
    update();
}

void SelectionOverlay::setSnapState(bool active, QPointF snapPoint, std::optional<QPointF> moving, double viewScale) {
    snapActive_ = active;
    snapPoint_  = snapPoint;
    moving_     = moving;
    viewScale_  = viewScale > 0 ? viewScale : 1.0;
    setOutlines(polys_);  // bounds take the marks in
}

QRectF SelectionOverlay::marksRect() const {
    using namespace selection::snapmarks;
    const double r = (kRingRadius + kRingWidth + kHaloWidth + 2.0) / viewScale_;
    QRectF out;
    if (snapActive_) out = QRectF(snapPoint_ - QPointF(r, r), QSizeF(2 * r, 2 * r));
    if (moving_) out = out.united(QRectF(*moving_ - QPointF(r, r), QSizeF(2 * r, 2 * r)));
    return out;
}

}  // namespace bld::ui
