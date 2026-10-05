#pragma once

#include <QColor>
#include <QFont>
#include <QGraphicsItem>
#include <QLineF>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QTransform>

#include <optional>

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
// While dragging, an amber dot also marks the moving connection that
// joins (or would join). Ring and dot are screen-sized (SelectionStyle.h
// snapmarks), the same at any zoom.
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
    // `moving`: the moving connection to mark, if any. `viewScale`: the
    // view's zoom (screen px per scene px), to size the marks' bounds.
    void setSnapState(bool active, QPointF snapPoint, std::optional<QPointF> moving = std::nullopt,
                      double viewScale = 1.0);
    // A selected module's whole name over its shortened one: a dark pill
    // and the name, drawn in the name's own turned frame.
    struct FullName {
        QTransform toScene;
        QRectF box;
        QPointF textTopLeft;
        QString name;
        QFont font;
        QColor fill;
    };
    void setFullNames(QList<FullName> names);

private:
    QList<QPolygonF> polys_;
    QList<RulerBand> bands_;
    QList<FullName> names_;
    QRectF  bounds_;
    bool    snapActive_ = false;
    QPointF snapPoint_;
    std::optional<QPointF> moving_;
    double  viewScale_ = 1.0;
    QRectF  marksRect() const;
};

}  // namespace bld::ui
