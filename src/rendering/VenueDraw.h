#pragma once

// How the venue model's newer parts are drawn (the web repo's
// references/VENUE-MODEL.md, "Drawing"), as plain geometry in studs so the
// desktop and the web draw the same thing: the web's
// editor/render/venueDraw.ts is the twin of this file, and both tests check
// the same numbers.

#include "../core/Venue.h"

#include <QColor>
#include <QLineF>
#include <QString>
#include <QVector>

#include <optional>

namespace bld::rendering::venuedraw {

struct ObstacleStyle {
    std::optional<QColor> fill; // none: outline only
    QColor stroke;
    double strokeWidthPx = 1.0;
};
ObstacleStyle obstacleStyle(core::ObstacleKind kind);

constexpr double kTreadSpacingStuds = 10.0;
struct StairMarks {
    QVector<QLineF> treads; // across the stairs, every kTreadSpacingStuds
    QVector<QLineF> arrow;  // shaft, then the two head strokes
};
// Nothing without upDegrees or a polygon.
StairMarks stairMarks(const QVector<QPointF>& poly, std::optional<double> upDegrees);

// The two diagonals of the bounding box.
QVector<QLineF> elevatorCross(const QVector<QPointF>& poly);

constexpr double kPowerRadiusStuds = 6.0;
inline QColor powerColor() {
    return { 220, 100, 20 };
}
// "Stage · 20 A · 120 V", or empty.
QString powerText(const core::VenuePower& p);

inline QColor dimensionColor() {
    return { 40, 90, 140 };
}
inline QColor estimateColor() {
    return { 120, 120, 120 };
}
constexpr double kDimensionTickStuds = 5.0;
constexpr double kDimensionLabelOffsetStuds = 8.0;
struct DimensionGeometry {
    QLineF line;
    QLineF ticks[2];
    QPointF label;   // centre of the label
    double angleDeg; // turned to read left to right
};
std::optional<DimensionGeometry> dimensionGeometry(QPointF from, QPointF to);

// Text shown for an estimated value.
QString estimatedText(const QString& s);

} // namespace bld::rendering::venuedraw
