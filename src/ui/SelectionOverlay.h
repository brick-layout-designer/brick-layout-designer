#pragma once

#include <QColor>
#include <QGraphicsItem>
#include <QLineF>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>

namespace bld::ui {

// Persistent scene item that paints the selection outline around every
// currently-selected brick/text/ruler/label/venue. Lives at an enormous
// z-value so it's always on top. MapView owns one of these and feeds it
// a fresh list of outlines every time the selection changes.
//
// Two visual modes:
//   * Normal: black + yellow double stroke with a translucent yellow fill.
//   * Live-snap: black + green double stroke, translucent green fill, plus
//     a ring drawn at the snap point so the user sees exactly where the
//     connection lock happened.
class SelectionOverlay : public QGraphicsItem {
public:
    SelectionOverlay();

    QRectF boundingRect() const override { return bounds_; }
    // Empty shape so scene hit-testing (scene()->items(pos), itemAt) skips
    // the overlay. Otherwise its huge boundingRect steals clicks away
    // from the bricks it's highlighting.
    QPainterPath shape() const override { return {}; }
    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override;

    void setOutlines(QList<QPolygonF> polys);
    // A selected ruler: a see-through band along its line or circle.
    struct RulerBand {
        bool circle = false;
        QLineF line;      // linear: the offset line, scene px
        QPointF centre;   // circular
        double radius = 0;
        double width = 0;  // scene px
    };
    void setRulerBands(QList<RulerBand> bands);
    void setSnapState(bool active, QPointF snapPoint);

private:
    QList<QPolygonF> polys_;
    QList<RulerBand> bands_;
    QRectF  bounds_;
    bool    snapActive_ = false;
    QPointF snapPoint_;
};

}  // namespace bld::ui
