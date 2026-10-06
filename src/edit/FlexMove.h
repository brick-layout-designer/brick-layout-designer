#pragma once

// BlueBrick's flex move (Actions/Bricks/FlexMove.cs + MapData/Tools/
// IKSolver.cs): double-click-drag a piece of a selected chain that has
// hinged connections (PFS flex track, magnet couplings...) and the chain
// bends, with each hinge limited to its connection type's hinge angle, so
// its free end follows the mouse (a CCD inverse-kinematics solve).

#include "../core/Brick.h"

#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

namespace bld::core  { class LayerBrick; }
namespace bld::parts { class PartsLibrary; }

namespace bld::edit {

// The hinge angle (degrees) of a connection type, from BlueBrick's
// ConnectionTypeList.xml; 0 for rigid connections.
float connectionHingeAngle(const QString& type);

// A free end of a flexible run that a bend handle sits on.
struct FlexEnd {
    QString guid;        // the brick at the end
    int connection = -1; // its free connection
    QPointF world;       // where that connection is, studs
};

// The flexible run through the selected bricks of `layer`: every brick with
// a hinged connection (and at most two connections) reachable through
// linked connections from a selected one, so a chain of flex track sets is
// one run. Returns its free ends (none when every end is joined) and puts
// the run's bricks in `run`.
std::vector<FlexEnd> flexRunEnds(const core::LayerBrick& layer, const QSet<QString>& selection,
                                 parts::PartsLibrary& lib, QSet<QString>* run = nullptr);

class FlexMove {
public:
    // Starts a flex move of `grabbedGuid` within the `selection` of
    // `layer`, or nothing when the selection has no flexible chain through
    // that brick. The layer's bricks are edited in place while the move
    // runs; their vector must not change size meanwhile.
    // `activeConnection` (when >= 0) is the grabbed brick's connection that
    // follows the mouse and snaps (a bend handle's end), instead of its
    // active connection.
    static std::unique_ptr<FlexMove> start(core::LayerBrick& layer, const QSet<QString>& selection,
                                           const QString& grabbedGuid, QPointF mouseStuds,
                                           parts::PartsLibrary& lib, int activeConnection = -1);
    ~FlexMove();

    // Bend the chain so its end reaches `mouseStuds`, snapping (unless
    // `snap` is off) to a free connection of another brick within
    // `reachStuds` (MapView::connectionSnapReachStuds). Returns
    // the snap point if any.
    std::optional<QPointF> moveTo(QPointF mouseStuds, double reachStuds, bool snap = true);

    struct State { QString guid; float orientation = 0.0f; QRectF area; };
    // The chain's bricks (those the move changes) before the move and now.
    const std::vector<State>& initialState() const { return initial_; }
    std::vector<State> currentState() const;
    // The joints bent as far as their hinge allows (studs, where the
    // joint is): shown while bending, so it's clear why the end stops.
    std::vector<QPointF> hingesAtLimit() const;

    // Put the chain back as it was.
    void restore();

    FlexMove(const FlexMove&) = delete;
    FlexMove& operator=(const FlexMove&) = delete;

private:
    struct Impl;
    explicit FlexMove(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> d_;
    std::vector<State> initial_;
};

}  // namespace bld::edit
