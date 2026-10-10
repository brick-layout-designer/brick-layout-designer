#pragma once

// Where a venue's wall labels go ("Stage wall — 24.50 ft"), placed as the
// web places them (apps/web/src/editor/render/venueLabels.ts;
// fixtures/render-parity/venue-labels.json holds the cases both check):
//   - just outside the room, a little off the wall, in a pill;
//   - turned along the wall but always upright: left to right, and bottom
//     to top on a vertical wall, never upside down;
//   - a label that would overlap another shows just the length (the full
//     text shows while its wall is selected, and as a tooltip);
//   - never under a selection handle: pushed further out until clear.
// Every size is a multiple of the font size, so labels stay the same size
// on screen when the font is (the Venue Designer sets it from the zoom).

#include "../core/Venue.h"

#include <QColor>
#include <QPointF>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>
#include <vector>

namespace bld::rendering {

// The pill, in font sizes: padding along and across, the gap to the wall,
// how much further a handle pushes it, and its corner radius.
struct VenueLabelPill {
    static constexpr double padX = 0.5;
    static constexpr double padY = 0.25;
    static constexpr double gap = 0.6;
    static constexpr double handleGap = 0.3;
    static constexpr double radius = 0.75;
};

// QGraphicsItem data role holding a wall label's whole text on its pill.
inline constexpr int kVenueLabelTextRole = 12;

struct VenueLabelColors {
    QColor fill, border, text;
};
// Light: white with a slate edge; dark: slate with a light edge.
VenueLabelColors venueLabelColors(bool dark);
// Over the Venue Designer's grid inside the room, so the grid fades there.
inline const QColor kVenueGridFade = QColor::fromRgbF(1.0f, 1.0f, 1.0f, 0.55f);

struct VenueLabel {
    int edge = -1;
    QString text;   // what's shown
    QString full;   // the whole text
    bool shortened = false;
    QPointF centre;  // scene px
    double angle = 0;  // degrees
    double width = 0;  // along the wall
    double height = 0;
};

struct VenueLabelOptions {
    double fontPx = 28;
    std::function<double(const QString&, double)> measure;
    std::optional<int> selectedEdge;
    QVector<QPointF> handles;  // studs
    double handleHalfPx = 0;    // scene px
};

// "24.50 ft", or inches under a foot.
QString venueDistanceText(double lenStuds);
// A wall's direction turned to read upright: in [-90, 90).
double uprightAngle(double dx, double dy);
// Whether two turned rectangles (centre, angle, width, height) overlap.
bool pillsOverlap(const VenueLabel& a, const VenueLabel& b);

std::vector<VenueLabel> venueEdgeLabels(const QVector<core::VenueEdge>& edges, const VenueLabelOptions& opts);

}  // namespace bld::rendering
