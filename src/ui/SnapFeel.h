#pragma once

// How connection snapping feels: how far it reaches, how it holds on, when
// it lets go, and how fast drags are left alone. Pure and shared by every
// snapping path (dragging parts and groups, placing a new part, inserting a
// module, touch drags; a flex move only takes the reach). The web has the
// same numbers in apps/web/src/editor/snapFeel.ts, and both test the same
// cases (fixtures/snap-vectors.json).
//
// The rules:
//   - Reach is a distance on screen (about 14 px), turned into studs with
//     the current zoom and kept between 0.5 and 4 studs. It no longer
//     depends on the grid. The Snap strength setting scales it.
//   - Once snapped, a part stays on that target until its connection is
//     more than 1.6x the reach away from it.
//   - It only moves to a different target that is clearly closer (by 30%
//     and by at least half a stud).
//   - Near-equal targets are settled the same way every time: the current
//     one, then the one whose connection is nearest the cursor, then a
//     fixed order.
//   - While the pointer moves fast (over 1200 screen px/s, over the last
//     few moves) no new snap starts; one already made holds. The drop
//     always runs one last snap at the normal reach.
//   - Alt (Option on a Mac) while dragging places without connection snap.
//   - As in BlueBrick, the active connection is the grabbed part's end
//     nearest the grab, kept for the whole drag; it snaps to the nearest
//     free end in reach at ANY angle, the part or the whole selection
//     (group, module) turning about it so the two ends face each other.
//     Distances are measured from where the pointer has the parts, never
//     the snapped pose. Level targets prefer the smaller turn.

#include <QString>

#include <cmath>
#include <deque>
#include <optional>
#include <vector>

namespace bld::ui::snapfeel {

inline constexpr double kReachScreenPx = 14.0;
inline constexpr double kMinReachStuds = 0.5;
inline constexpr double kMaxReachStuds = 4.0;
// Stay snapped until the connection is this many reaches away.
inline constexpr double kHoldFactor = 1.6;
// Switch only to a target at most this fraction of the current distance...
inline constexpr double kSwitchRatio = 0.7;
// ...and at least this many studs closer.
inline constexpr double kSwitchMinStuds = 0.5;
// Targets within this many studs of the nearest count as level.
inline constexpr double kTieStuds = 0.25;
// Faster than this (screen px/s) is a fast drag.
inline constexpr double kFastPxPerSecond = 1200.0;
// Pointer moves averaged for the speed.
inline constexpr int kSpeedSamples = 4;
// Older moves than this (ms) don't count towards the speed.
inline constexpr double kSpeedWindowMs = 200.0;
// Turns closer than this (degrees) count as the same.
inline constexpr double kTurnTieDeg = 1.0;
// A fast drag that stops dead snaps after this long (ms).
inline constexpr int kSettleMs = 120;

enum class Strength { Off, Gentle, Strong };

// "off" / "gentle" / "strong" (the account preference connectionSnap).
QString strengthId(Strength s);
// Unknown ids give std::nullopt.
std::optional<Strength> strengthFromId(const QString& id);
// Reach multiplier: 0 / 1 / 1.6.
double strengthScale(Strength s);

// Connection-snap reach in studs for a view showing `screenPxPerStud`
// screen pixels per stud: the screen reach in studs, kept between the
// limits, times the strength. 0 when snapping is off.
double reachStuds(double screenPxPerStud, Strength strength = Strength::Gentle);

// Candidates worth handing to pick(): those within this distance.
inline double holdReach(double reach) { return reach * kHoldFactor; }

// One possible join: a moving connection and a free target connection.
struct Candidate {
    QString movingKey;      // which moving connection (stable for the drag)
    QString targetKey;      // which target connection
    double dist = 0.0;      // studs between them, where the pointer has the part
    double mouseDist = 0.0; // studs from the moving connection to the cursor
    double turn = 0.0;      // degrees the moving part(s) turn to face the target (absolute)
};

// The join a drag is holding on to.
struct Lock {
    QString movingKey;
    QString targetKey;
};

// Index into `candidates` of this frame's join, or -1. `candidates` may
// include any within holdReach(reach); new ones only count within
// `reach`. `fast`: keep a held join, start none. `bypass` (Alt): none.
int pick(const std::vector<Candidate>& candidates, const std::optional<Lock>& lock, double reach,
         bool fast = false, bool bypass = false);

// `deg` folded into (-180, 180].
double wrap180(double deg);
// The turn (degrees, (-180, 180]) that makes a connection facing
// `movingAngle` (world) face one facing `targetAngle`: mouth to mouth.
inline double facingTurn(double targetAngle, double movingAngle) {
    return wrap180(targetAngle + 180.0 - movingAngle);
}

// Pointer speed in screen px/s over the last few moves.
class SpeedMeter {
public:
    void sample(double x, double y, double tMs);
    // px/s; 0 until there are two moves within the window.
    double speed() const;
    bool isFast() const { return speed() > kFastPxPerSecond; }
    void reset() { samples_.clear(); }
    // The newest sample, if any (to add a still one when the pointer stops).
    bool hasSamples() const { return !samples_.empty(); }
    double lastX() const { return samples_.back().x; }
    double lastY() const { return samples_.back().y; }

private:
    struct S { double x, y, t; };
    std::deque<S> samples_;
};

// One drag's snap state: the held join and the pointer speed.
class Session {
public:
    std::optional<Lock> lock;
    SpeedMeter meter;

    void sample(double x, double y, double tMs) { meter.sample(x, y, tMs); }
    // This frame's join (index into `candidates`, or -1), remembered for
    // the next. `final` is the drop: the speed gate is off.
    int step(const std::vector<Candidate>& candidates, double reach, bool bypass = false, bool final = false);
    void reset() {
        lock.reset();
        meter.reset();
    }
};

}  // namespace bld::ui::snapfeel
