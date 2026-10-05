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

class FlexMove {
public:
    // Starts a flex move of `grabbedGuid` within the `selection` of
    // `layer`, or nothing when the selection has no flexible chain through
    // that brick. The layer's bricks are edited in place while the move
    // runs; their vector must not change size meanwhile.
    static std::unique_ptr<FlexMove> start(core::LayerBrick& layer, const QSet<QString>& selection,
                                           const QString& grabbedGuid, QPointF mouseStuds,
                                           parts::PartsLibrary& lib);
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
