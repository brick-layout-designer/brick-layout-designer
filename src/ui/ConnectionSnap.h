#pragma once

// Connection snap: which free connection of the parts being moved joins
// which free connection already on the map. Every snapping path (dragging
// parts and groups, placing a new part, inserting a module) lists its
// moving connections and the map's free targets, and pickConnectionSnap
// chooses one join with the feel in SnapFeel.h (reach, hold, clearly
// better switching, steady ties, the speed gate, Alt). Callers then move
// (and for a single new or dropped part, turn) what they move so the two
// connections meet.

#include "SnapFeel.h"

#include <QPointF>
#include <QSet>
#include <QString>

#include <vector>

namespace bld::core  { class Map; struct Brick; }
namespace bld::parts { class PartsLibrary; }

namespace bld::ui {

// Stable key of a brick's connection, for snap locks.
QString connKey(const QString& guid, int index);

// A free (unlinked) connection already on the map.
struct FreeTarget {
    QString key;
    QString type;
    QPointF world;      // studs
    double angle = 0.0; // world outward angle, degrees
};

// What the links of a moving set can point at: its bricks' guids and their
// connections' guids (linkedToId may name either: the partner connection
// is the usual, a partner brick guid also turns up; see MapView's group
// walk).
QSet<QString> linkKeys(const core::Map& map, const QSet<QString>& movingGuids);

// During a drag a link to the parts left behind no longer holds: a moving
// connection is taken only when it links inside the moving set, and a
// still one only when it links to something that isn't moving. `keys`
// comes from linkKeys.
inline bool takenWhileMoving(const QString& linkedToId, const QSet<QString>& keys) {
    return !linkedToId.isEmpty() && keys.contains(linkedToId);
}
inline bool takenWhileStill(const QString& linkedToId, const QSet<QString>& keys) {
    return !linkedToId.isEmpty() && !keys.contains(linkedToId);
}

// Every free typed connection on the map's brick layers, skipping the
// bricks in `exclude` (the ones being moved). An end linked to one of
// them counts as free: that part is moving away.
std::vector<FreeTarget> freeTargets(const core::Map& map, parts::PartsLibrary& lib,
                                    const QSet<QString>& exclude = {});

// A connection of what is being moved, where the pointer has it now.
struct MovingConn {
    QString key;
    QString type;
    QPointF world;          // studs
    double mouseDist = 0.0; // studs to the cursor
};

struct SnapPick {
    int moving = -1;  // index into the moving connections
    int target = -1;  // index into the targets
    bool applied() const { return moving >= 0 && target >= 0; }
};

// Choose the join for this frame. With a session the hold and the speed
// gate apply and the choice is remembered; without one it's a one-off
// snap at `reach`. `final` is the drop.
SnapPick pickConnectionSnap(const std::vector<MovingConn>& moving, const std::vector<FreeTarget>& targets,
                            double reach, snapfeel::Session* session, bool bypass = false, bool final = false);

// The orientation (-180, 180] that turns a connection of local angle
// `connAngle` to face a target whose world angle is `targetAngle`.
double facingOrientation(double targetAngle, double connAngle);

// Rotate a local point by `degrees`.
QPointF rotatePoint(QPointF p, double degrees);

}  // namespace bld::ui
