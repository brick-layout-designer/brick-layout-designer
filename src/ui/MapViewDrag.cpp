// Drag-related MapView members, split out so MapView.cpp can focus on
// construction and the big event handlers. Everything here operates on
// MapView's private state via the class declaration in MapView.h.
//
// Connection snap follows BlueBrick's getMovedSnapPoint algorithm in
// MapData/LayerBrick.cs: one master brick (the one the user grabbed) and
// one active connection on that brick (the one nearest the click). Every
// drag frame aligns that single connection to the nearest free compatible
// target anywhere in the map, and the whole selected group translates by
// the same delta. This is simple, predictable, and matches upstream
// behaviour exactly.

#include "MapView.h"

#include "../core/Brick.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../edit/EditCommands.h"
#include "../edit/LabelCommands.h"
#include "../edit/RulerCommands.h"
#include "../parts/BrickPlacement.h"
#include "../parts/PartsLibrary.h"
#include "../rendering/SceneBuilder.h"
#include "ConnectionSnap.h"
#include "MapViewInternal.h"
#include "SelectionOverlay.h"
#include "theme/AppPrefs.h"

#include <QElapsedTimer>
#include <QGraphicsItem>
#include <QGuiApplication>
#include <QTimer>
#include <QTransform>
#include <QGraphicsScene>
#include <QStatusBar>
#include <QUndoStack>

#include <algorithm>
#include <cmath>
#include <optional>

namespace bld::ui {

using detail::kBrickDataLayerIndex;
using detail::kBrickDataGuid;
using detail::isBrickItem;
using detail::isRulerItem;
using detail::isLabelItem;
using detail::studToPx;

namespace {

// Locate a brick by (layer index, guid). O(bricks in that layer).
const core::Brick* findBrick(const core::Map& map, int layerIndex, const QString& guid) {
    if (layerIndex < 0 || layerIndex >= static_cast<int>(map.layers().size())) return nullptr;
    auto* L = map.layers()[layerIndex].get();
    if (!L || L->kind() != core::LayerKind::Brick) return nullptr;
    const auto& BL = static_cast<const core::LayerBrick&>(*L);
    for (const auto& b : BL.bricks) if (b.guid == guid) return &b;
    return nullptr;
}

// Pick the connection on `brick` whose world position is nearest to
// `clickStuds`. Returns -1 if the brick has no connections or the metadata
// isn't resolvable. Free-only: we skip already-linked connections so
// clicking on a brick that's already connected at one end still lets you
// grab and drag the OTHER end as the snap lead.
int nearestConnectionIndex(const core::Brick& brick, parts::PartsLibrary& lib,
                           QPointF clickStuds) {
    auto meta = lib.metadata(brick.partNumber);
    if (!meta) return -1;
    const int n = meta->connections.size();
    if (n == 0) return -1;
    const QPointF brickCentre = parts::placement::imageCentre(brick, lib);
    int bestIdx = -1;
    double bestDist = std::numeric_limits<double>::max();
    for (int i = 0; i < n; ++i) {
        // Skip already-linked connections; for single-end grabs this is
        // usually the end we *don't* want to snap with.
        if (i < static_cast<int>(brick.connections.size()) &&
            !brick.connections[i].linkedToId.isEmpty()) continue;
        const auto& c = meta->connections[i];
        if (c.type.isEmpty()) continue;
        const QPointF worldPos = brickCentre + rotatePoint(c.position, brick.orientation);
        const QPointF d = worldPos - clickStuds;
        const double sq = d.x() * d.x() + d.y() * d.y();
        if (sq < bestDist) { bestDist = sq; bestIdx = i; }
    }
    if (bestIdx >= 0) return bestIdx;
    // Fallback: no free connections (every end already linked). Pick the
    // nearest connection regardless of linkage — snap won't fire anyway
    // (computeSnap filters on free), but we keep the anchor identifiable.
    bestDist = std::numeric_limits<double>::max();
    for (int i = 0; i < n; ++i) {
        const auto& c = meta->connections[i];
        if (c.type.isEmpty()) continue;
        const QPointF worldPos = brickCentre + rotatePoint(c.position, brick.orientation);
        const QPointF d = worldPos - clickStuds;
        const double sq = d.x() * d.x() + d.y() * d.y();
        if (sq < bestDist) { bestDist = sq; bestIdx = i; }
    }
    return bestIdx;
}

}  // namespace

std::vector<MapView::BrickOriginSnapshot> MapView::selectedBrickSnapshots() const {
    std::vector<BrickOriginSnapshot> out;
    if (!map_) return out;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!isBrickItem(it)) continue;
        BrickOriginSnapshot s;
        s.item = it;
        s.layerIndex = it->data(kBrickDataLayerIndex).toInt();
        s.guid       = it->data(kBrickDataGuid).toString();
        s.scenePosAtPress = it->scenePos();
        if (auto* b = findBrick(*map_, s.layerIndex, s.guid)) {
            s.studTopLeftAtPress = b->displayArea.topLeft();
        }
        out.push_back(s);
    }
    return out;
}

void MapView::captureDragStart() {
    dragStart_ = selectedBrickSnapshots();
    dragSnap_.reset();
    rulerDragStart_.clear();
    labelDragStart_.clear();
    if (!map_) return;
    QSet<QString> rulerSeen;
    QSet<QString> labelSeen;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (isRulerItem(it)) {
            const QString guid = it->data(kBrickDataGuid).toString();
            if (guid.isEmpty() || rulerSeen.contains(guid)) continue;
            rulerSeen.insert(guid);
            RulerDragSnapshot r;
            r.anyPiece = it;
            r.layerIndex = it->data(kBrickDataLayerIndex).toInt();
            r.guid = guid;
            r.scenePosAtPress = it->scenePos();
            rulerDragStart_.push_back(r);
        } else if (isLabelItem(it)) {
            const QString lid = it->data(kBrickDataGuid).toString();
            if (lid.isEmpty() || labelSeen.contains(lid)) continue;
            labelSeen.insert(lid);
            LabelDragSnapshot l;
            l.item = it;
            l.labelId = lid;
            l.scenePosAtPress = it->scenePos();
            labelDragStart_.push_back(l);
        }
    }
}

void MapView::captureGrabAnchor(QPointF clickScenePos) {
    grabBrickGuid_.clear();
    grabBrickLayerIndex_ = -1;
    grabActiveConnIdx_   = -1;
    if (!map_) return;

    // Find the brick at the click. Walk scene->items() in z-order instead
    // of itemAt(): itemAt returns the topmost hit, which includes the
    // SelectionOverlay (zValue 1e9) and connection-dot children. We want
    // the first BRICK-kind item at that point.
    const core::Brick* brick = nullptr;
    QString guid;
    int     li = -1;
    for (QGraphicsItem* it : scene()->items(clickScenePos)) {
        if (!isBrickItem(it)) continue;
        guid = it->data(kBrickDataGuid).toString();
        li   = it->data(kBrickDataLayerIndex).toInt();
        brick = findBrick(*map_, li, guid);
        if (brick) break;
    }
    if (!brick) return;

    const double px = studToPx();
    const QPointF clickStuds(clickScenePos.x() / px, clickScenePos.y() / px);
    const int idx = nearestConnectionIndex(*brick, parts_, clickStuds);
    if (idx < 0) return;

    grabBrickGuid_        = guid;
    grabBrickLayerIndex_  = li;
    grabActiveConnIdx_    = idx;

    // Persist the click-picked active connection on the brick so the
    // gold "active" marker renders in the right place. This matches
    // BlueBrick's setActiveConnectionPointUnder — a small UI-state
    // mutation that also survives a save (BlueBrick serializes
    // activeConnectionPointIndex too).
    //
    // CRITICAL: we do NOT call rebuildScene() synchronously here. This
    // runs inside mousePressEvent; rebuildScene destroys + recreates
    // every scene item, INCLUDING the grabber item Qt just picked. On
    // the follow-up mouse move Qt would then dereference a freed
    // pointer and crash. Defer the visual refresh to the next event
    // loop tick so the current mouse-press handler unwinds first.
    if (li >= 0 && li < static_cast<int>(map_->layers().size())) {
        auto* L = map_->layers()[li].get();
        if (L && L->kind() == core::LayerKind::Brick) {
            auto& BL = static_cast<core::LayerBrick&>(*L);
            for (auto& b : BL.bricks) {
                if (b.guid == guid) {
                    if (b.activeConnectionPointIndex != idx) {
                        b.activeConnectionPointIndex = idx;
                        // Note: we don't rebuild the scene right now.
                        // The next mouseMove that triggers a live snap
                        // will update the visual via its own path; a
                        // final rebuild happens on mouseRelease via
                        // the undo-stack commit.
                    }
                    break;
                }
            }
        }
    }
}

void MapView::clearGrabAnchor() {
    grabBrickGuid_.clear();
    grabBrickLayerIndex_ = -1;
    grabActiveConnIdx_   = -1;
}

double MapView::connectionSnapReachStuds() const {
    // Screen px per stud: the view's zoom times the scene's px per stud.
    const QTransform& t = transform();
    const double zoom = std::hypot(t.m11(), t.m12());
    const auto strength = snapfeel::strengthFromId(theme::PrefsStore::instance().prefs().connectionSnap)
                              .value_or(snapfeel::Strength::Gentle);
    return snapfeel::reachStuds(zoom * studToPx(), strength);
}

bool MapView::snapBypassed() const {
    // Alt on Windows / Linux; Option on a Mac (Qt calls it Alt there too),
    // as the latest pointer or drag event had it.
    return snapMods_.testFlag(Qt::AltModifier);
}

double MapView::snapClockMs() const {
    static QElapsedTimer clock;
    if (!clock.isValid()) clock.start();
    return static_cast<double>(clock.nsecsElapsed()) / 1e6;
}

void MapView::sampleSnapSpeed(snapfeel::Session& session, QPointF vp) const {
    session.sample(vp.x(), vp.y(), snapClockMs());
}

void MapView::setSnapMarks(bool active, QPointF ringScene, std::optional<QPointF> movingScene) {
    const bool changed = active != liveSnapActive_ || (active && ringScene != liveSnapPointScene_)
                         || movingScene != liveSnapMovingScene_;
    liveSnapActive_ = active;
    if (active) liveSnapPointScene_ = ringScene;
    liveSnapMovingScene_ = movingScene;
    if (!changed) return;
    if (auto* ov = static_cast<SelectionOverlay*>(selectionOverlay_)) {
        const QTransform& t = transform();
        ov->setSnapState(liveSnapActive_, liveSnapPointScene_, liveSnapMovingScene_, std::hypot(t.m11(), t.m12()));
    }
    viewport()->update();
}

void MapView::applyLiveConnectionSnap() {
    if (!map_ || dragStart_.empty()) {
        setSnapMarks(false, {}, std::nullopt);
        return;
    }

    const double px = studToPx();
    const QPointF mouseStuds(lastMouseScenePos_.x() / px,
                             lastMouseScenePos_.y() / px);
    QSet<QString> movingGuids;
    for (const auto& s : dragStart_) movingGuids.insert(s.guid);
    const double reach = connectionSnapReachStuds();
    const bool bypass = snapBypassed();

    // Every free connection across every moving brick, where the pointer
    // has it (Qt has just put each item at press position + mouse delta).
    std::vector<MovingConn> moving;
    int grabbed = -1;  // the grab anchor's entry, if free
    for (const auto& s : dragStart_) {
        if (!s.item) continue;
        const auto* b = findBrick(*map_, s.layerIndex, s.guid);
        if (!b) continue;
        auto meta = parts_.metadata(b->partNumber);
        if (!meta) continue;
        // The item sits at the displayArea centre; connections hang off
        // the sprite centre.
        const QPointF centerPx = s.item->scenePos();
        const QPointF centerStuds = QPointF(centerPx.x() / px, centerPx.y() / px)
                                  + parts_.imageOffset(b->partNumber, b->orientation);
        const int n = meta->connections.size();
        for (int i = 0; i < n; ++i) {
            const auto& c = meta->connections[i];
            if (c.type.isEmpty()) continue;
            if (i < static_cast<int>(b->connections.size()) &&
                !b->connections[i].linkedToId.isEmpty()) continue;
            const QPointF world = centerStuds + rotatePoint(c.position, b->orientation);
            const QPointF d = world - mouseStuds;
            if (s.guid == grabBrickGuid_ && i == grabActiveConnIdx_) grabbed = static_cast<int>(moving.size());
            moving.push_back({ connKey(s.guid, i), c.type, world, std::hypot(d.x(), d.y()) });
        }
    }

    const auto targets = reach > 0.0 && !bypass && !moving.empty()
                             ? freeTargets(*map_, parts_, movingGuids)
                             : std::vector<FreeTarget>{};
    const SnapPick best = pickConnectionSnap(moving, targets, reach, &dragSnap_, bypass, false);

    // A fast drag that stops dead gets no more moves: snap shortly after.
    if (!snapSettle_) {
        snapSettle_ = new QTimer(this);
        snapSettle_->setSingleShot(true);
        connect(snapSettle_, &QTimer::timeout, this, [this] {
            if (dragStart_.empty() || !(QGuiApplication::mouseButtons() & Qt::LeftButton)) return;
            // Still where it was: a still sample slows the speed.
            if (dragSnap_.meter.hasSamples())
                dragSnap_.sample(dragSnap_.meter.lastX(), dragSnap_.meter.lastY(), snapClockMs());
            applyLiveConnectionSnap();
        });
    }
    snapSettle_->stop();
    if (!best.applied() && dragSnap_.meter.isFast()) snapSettle_->start(snapfeel::kSettleMs);

    // Status-bar diagnostic so the user can tell WHY snap did or didn't
    // fire. Shown only when live dragging; cleared by commitDragIfMoved
    // on release.
    auto statusHint = [this](const QString& msg) {
        if (auto* mw = window())
            if (auto* sb = mw->findChild<QStatusBar*>())
                sb->showMessage(msg, 1500);
    };

    // The connection to mark: the grab anchor, else the free one nearest
    // the cursor.
    int shown = grabbed;
    if (shown < 0)
        for (int i = 0; i < static_cast<int>(moving.size()); ++i)
            if (shown < 0 || moving[i].mouseDist < moving[shown].mouseDist) shown = i;

    if (!best.applied()) {
        // No connection snap (parts without connection points like Tables,
        // nothing within reach, Alt held, or a fast drag). Fall back to
        // live grid snap so the group still tracks the grid while
        // dragging. Single-brick live drags already get grid snap via
        // SnappingPixmap::itemChange; this covers the multi-brick case
        // where that per-item snap is deliberately disabled.
        QPointF gridShiftStuds;
        if (snapStepStuds_ > 0.0 && dragStart_.size() > 1) {
            const auto& anchor = dragStart_.front();
            if (anchor.item) {
                const QPointF anchorCenterPx = anchor.item->scenePos();
                const QPointF anchorCenterStuds(anchorCenterPx.x() / px, anchorCenterPx.y() / px);
                const QPointF snapped(
                    std::round(anchorCenterStuds.x() / snapStepStuds_) * snapStepStuds_,
                    std::round(anchorCenterStuds.y() / snapStepStuds_) * snapStepStuds_);
                gridShiftStuds = snapped - anchorCenterStuds;
                const QPointF shiftPxGrid(gridShiftStuds.x() * px, gridShiftStuds.y() * px);
                if (std::abs(shiftPxGrid.x()) > 0.01 || std::abs(shiftPxGrid.y()) > 0.01) {
                    rendering::SceneBuilder::setSuppressItemSnap(true);
                    for (const auto& s : dragStart_) {
                        if (!s.item) continue;
                        s.item->setPos(s.item->scenePos() + shiftPxGrid);
                    }
                    rendering::SceneBuilder::setSuppressItemSnap(false);
                }
            }
        }
        if (moving.empty()) {
            statusHint(tr("Connection snap: no free connections in selection"));
        } else if (bypass) {
            statusHint(tr("Connection snap: off while Alt is held"));
        } else {
            statusHint(tr("Connection snap: %1 moving conn(s), no target within %2 studs")
                .arg(moving.size()).arg(reach, 0, 'f', 1));
        }
        std::optional<QPointF> dot;
        if (shown >= 0) dot = (moving[shown].world + gridShiftStuds) * px;
        setSnapMarks(false, {}, dot);
        return;
    }

    statusHint(tr("Connection snap active (%1 candidate conn(s))")
               .arg(moving.size()));

    // Shift every dragged item by the same translation. Suppress the
    // per-item grid-snap itemChange for this pass so our connection
    // alignment survives the setPos round-trip.
    const QPointF target = targets[best.target].world;
    const QPointF shift = target - moving[best.moving].world;
    const QPointF shiftPx(shift.x() * px, shift.y() * px);
    if (std::abs(shiftPx.x()) > 0.01 || std::abs(shiftPx.y()) > 0.01) {
        rendering::SceneBuilder::setSuppressItemSnap(true);
        for (const auto& s : dragStart_) {
            if (!s.item) continue;
            s.item->setPos(s.item->scenePos() + shiftPx);
        }
        rendering::SceneBuilder::setSuppressItemSnap(false);
    }
    // The ring on the target; the joined connection's dot inside it.
    setSnapMarks(true, target * px, target * px);
}

void MapView::commitDragIfMoved() {
    if (snapSettle_) snapSettle_->stop();
    if (!map_) return;

    // Rulers and labels first: push Move* commands based on scene-pos
    // deltas. Done BEFORE the brick path so an empty brick snapshot
    // doesn't short-circuit the ruler/label commits. Wrapped in a single
    // macro so a mixed drag undoes as one.
    if (!rulerDragStart_.empty() || !labelDragStart_.empty()) {
        const double pxToStud = 1.0 / studToPx();
        std::vector<std::tuple<int, QString, QPointF>> rulerCmds;
        std::vector<std::pair<QString, QPointF>>       labelCmds;
        for (const auto& r : rulerDragStart_) {
            if (!r.anyPiece) continue;
            const QPointF d = r.anyPiece->scenePos() - r.scenePosAtPress;
            if (std::abs(d.x()) < 0.5 && std::abs(d.y()) < 0.5) continue;
            rulerCmds.emplace_back(r.layerIndex, r.guid,
                                    QPointF(d.x() * pxToStud, d.y() * pxToStud));
        }
        for (const auto& l : labelDragStart_) {
            if (!l.item) continue;
            const QPointF d = l.item->scenePos() - l.scenePosAtPress;
            if (std::abs(d.x()) < 0.5 && std::abs(d.y()) < 0.5) continue;
            labelCmds.emplace_back(l.labelId,
                                    QPointF(d.x() * pxToStud, d.y() * pxToStud));
        }
        if (!rulerCmds.empty() || !labelCmds.empty()) {
            undoStack_->beginMacro(tr("Drag"));
            for (const auto& [li, g, d] : rulerCmds) {
                undoStack_->push(new edit::MoveRulerItemCommand(*map_, li, g, d));
            }
            for (const auto& [id, d] : labelCmds) {
                undoStack_->push(new edit::MoveAnchoredLabelCommand(*map_, id, d));
            }
            undoStack_->endMacro();
        }
        rulerDragStart_.clear();
        labelDragStart_.clear();
    }

    if (dragStart_.empty()) return;

    const double px = studToPx();

    // Derive the group delta from the FIRST dragged snapshot — Qt moves
    // every selected movable item by the same vector, so the delta at
    // any one item represents the whole group.
    std::vector<edit::MoveBricksCommand::Entry> entries;
    QPointF groupDelta(0, 0);
    bool haveDelta = false;
    for (const auto& s : dragStart_) {
        if (!s.item) continue;
        if (!haveDelta) {
            const QPointF d = s.item->scenePos() - s.scenePosAtPress;
            if (std::abs(d.x()) < 0.5 && std::abs(d.y()) < 0.5) {
                dragStart_.clear();
                return;
            }
            groupDelta = QPointF(d.x() / px, d.y() / px);
            haveDelta = true;
        }
        edit::MoveBricksCommand::Entry e;
        e.ref.layerIndex = s.layerIndex;
        e.ref.guid = s.guid;
        e.beforeTopLeft = s.studTopLeftAtPress;
        e.afterTopLeft  = s.studTopLeftAtPress + groupDelta;
        entries.push_back(e);
    }
    if (entries.empty()) { dragStart_.clear(); return; }

    // Connection snap at drop time: mouse-nearest free moving connection
    // leads the snap. Same strategy as the live drag so release just
    // locks in what the user already saw on-screen.
    bool connectionSnapped = false;
    std::optional<edit::RotateBricksCommand::Entry> connectionRotate;
    {
        const QPointF mouseStuds(lastMouseScenePos_.x() / px,
                                 lastMouseScenePos_.y() / px);
        QSet<QString> movingGuids;
        for (const auto& e : entries) movingGuids.insert(e.ref.guid);
        const double reach = connectionSnapReachStuds();
        const bool bypass = snapBypassed();

        struct FreeConn {
            const core::Brick* brick;
            int connIdx;
            QPointF centerStuds;
        };
        std::vector<FreeConn> free;
        std::vector<MovingConn> moving;

        for (const auto& e : entries) {
            const auto* b = findBrick(*map_, e.ref.layerIndex, e.ref.guid);
            if (!b) continue;
            auto meta = parts_.metadata(b->partNumber);
            if (!meta) continue;
            const QPointF centerStuds = e.afterTopLeft + QPointF(
                b->displayArea.width()  / 2.0,
                b->displayArea.height() / 2.0)
                + parts_.imageOffset(b->partNumber, b->orientation);
            const int n = meta->connections.size();
            for (int i = 0; i < n; ++i) {
                const auto& c = meta->connections[i];
                if (c.type.isEmpty()) continue;
                if (i < static_cast<int>(b->connections.size()) &&
                    !b->connections[i].linkedToId.isEmpty()) continue;
                const QPointF world = centerStuds + rotatePoint(c.position, b->orientation);
                const QPointF d = world - mouseStuds;
                free.push_back({ b, i, centerStuds });
                moving.push_back({ connKey(b->guid, i), c.type, world, std::hypot(d.x(), d.y()) });
            }
        }

        // The drop: one last snap at the normal reach, keeping the join the
        // drag held (a fast drag's held-back snap happens now).
        const auto targets = reach > 0.0 && !bypass && !moving.empty()
                                 ? freeTargets(*map_, parts_, movingGuids)
                                 : std::vector<FreeTarget>{};
        const SnapPick pick = pickConnectionSnap(moving, targets, reach, &dragSnap_, bypass, true);
        dragSnap_.reset();

        struct Best {
            bool applied = false;
            QPointF translationStuds;
            QPointF rotationAlignedTranslationStuds;
            std::optional<float> newOrientation;
        } best;
        const core::Brick* bestBrick = nullptr;
        if (pick.applied()) {
            const FreeConn& fc = free[pick.moving];
            const FreeTarget& tc = targets[pick.target];
            const auto meta = parts_.metadata(fc.brick->partNumber);
            const auto& ac = meta->connections[fc.connIdx];
            best.applied = true;
            best.translationStuds = tc.world - moving[pick.moving].world;
            const double newOrient = facingOrientation(tc.angle, ac.angleDegrees);
            const QPointF newCenter = tc.world - rotatePoint(ac.position, newOrient);
            best.rotationAlignedTranslationStuds = newCenter - fc.centerStuds;
            best.newOrientation = static_cast<float>(newOrient);
            bestBrick = fc.brick;
        }

        if (best.applied && bestBrick) {
            const bool singleBrick = (entries.size() == 1);
            const QPointF t = (singleBrick && best.newOrientation)
                                  ? best.rotationAlignedTranslationStuds
                                  : best.translationStuds;
            for (auto& e : entries) e.afterTopLeft += t;
            connectionSnapped = true;
            if (singleBrick && best.newOrientation
                && std::abs(*best.newOrientation - bestBrick->orientation) > 0.01f) {
                // The translation above put the sprite centre where the
                // turned brick's sprite centre belongs; turn around it.
                core::Brick turned = *bestBrick;
                turned.displayArea.moveTo(entries.front().afterTopLeft);
                edit::RotateBricksCommand::Entry re;
                re.ref = entries.front().ref;
                re.beforeOrientation = bestBrick->orientation;
                re.beforeArea = turned.displayArea;
                parts::placement::rotateAroundImageCentre(turned, *best.newOrientation, parts_);
                re.afterOrientation  = turned.orientation;
                re.afterArea = turned.displayArea;
                connectionRotate = re;
            }
        }
    }

    // Grid snap fallback.
    if (snapStepStuds_ > 0.0 && !connectionSnapped) {
        const QPointF tl = entries.front().afterTopLeft;
        const QPointF snapped(std::round(tl.x() / snapStepStuds_) * snapStepStuds_,
                              std::round(tl.y() / snapStepStuds_) * snapStepStuds_);
        const QPointF extra = snapped - tl;
        if (!extra.isNull()) {
            for (auto& e : entries) e.afterTopLeft += extra;
        }
    }

    dragStart_.clear();
    if (!entries.empty()) {
        if (connectionRotate) {
            undoStack_->beginMacro(tr("Snap to connection"));
            undoStack_->push(new edit::MoveBricksCommand(*map_, std::move(entries)));
            undoStack_->push(new edit::RotateBricksCommand(*map_, { *connectionRotate }));
            undoStack_->endMacro();
        } else {
            undoStack_->push(new edit::MoveBricksCommand(*map_, std::move(entries)));
        }
    }

    if (auto* mw = window())
        if (auto* sb = mw->findChild<QStatusBar*>())
            sb->showMessage(connectionSnapped ? tr("Connection snap")
                                              : tr("Moved"), 1500);
}

}  // namespace bld::ui
