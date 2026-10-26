#pragma once

// Which connection points of a set or a layout meet another and which are
// left open: a check for exported parts (Brick Track Designer's "Layout
// parts") and layouts, used by `bld-render --check-connections`. Points
// pair as the app links them (rebuildConnectivity): the same non-empty
// type, within 0.5 stud, on different parts. A pair that is off by more
// than a hair, or whose points don't face each other, is reported too.

#include "../core/Brick.h"

#include <QList>
#include <QMap>
#include <QPointF>
#include <QString>

#include <vector>

namespace bld::core {
class Map;
}
namespace bld::parts {
class PartsLibrary;
}

namespace bld::edit {

struct CheckedPoint {
    QString partNumber;
    int brick = -1;      // index in the checked bricks
    int connection = -1; // index in the part's connections
    QString type;
    QPointF at;         // world position, studs
    double angle = 0.0; // world direction it faces, degrees
};

struct CheckedPair {
    CheckedPoint a, b;
    double gap = 0.0;      // studs apart
    double angleOff = 0.0; // degrees from facing each other exactly
};

struct ConnectionReport {
    int parts = 0;
    int points = 0;
    QMap<QString, int> pointsByType;
    QList<CheckedPair> pairs;
    QList<CheckedPoint> open;
    // Pairs more than `exactGap` apart or more than `exactAngle` from
    // facing each other: they link, but not cleanly.
    QList<CheckedPair> rough;
};

struct ConnectionCheckOptions {
    double linkDistance = 0.5; // as rebuildConnectivity links
    double exactGap = 0.01;
    double exactAngle = 0.5;
};

// Every connection of `bricks` (at their places on the map).
ConnectionReport checkConnections(const std::vector<core::Brick>& bricks, parts::PartsLibrary& lib,
                                  const ConnectionCheckOptions& options = {});

// The set `setKey` as it is placed whole (its parts at the set's own layout).
ConnectionReport checkSetConnections(const QString& setKey, parts::PartsLibrary& lib,
                                     const ConnectionCheckOptions& options = {});

// Every brick sheet of `map`, each on its own (parts on different sheets
// never link), added together.
ConnectionReport checkMapConnections(const core::Map& map, parts::PartsLibrary& lib,
                                     const ConnectionCheckOptions& options = {});

// The report as text: a summary line per type, the counts, then each open
// end and each rough pair.
QString connectionReportText(const QString& title, const ConnectionReport& report);

} // namespace bld::edit
