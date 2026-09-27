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

#include <QGraphicsItem>
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

QPointF rotatePoint(QPointF p, double degrees) {
    const double r = degrees * M_PI / 180.0;
    const double c = std::cos(r), s = std::sin(r);
    return { p.x() * c - p.y() * s, p.x() * s + p.y() * c };
}

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

double MapView::connectionSnapThresholdStuds() const {
    // Snap distance = user's grid-snap step + 2 studs of grace. Rule:
    // connections should engage a little BEFORE the grid step finalises
    // positioning — otherwise the user lands exactly one grid cell off
    // and has to nudge to make the snap fire. The +2 gives just enough
    // "magnetic reach" without pulling connections across the map.
    //
    // When the grid step is disabled (0), fall back to a 4-stud reach
    // — half a brick unit — so freehand placements still snap when the
    // user drags roughly into the right spot.
    if (snapStepStuds_ <= 0.0) return 4.0;
    return snapStepStuds_ + 2.0;
}

void MapView::applyLiveConnectionSnap() {
    if (!map_ || dragStart_.empty()) {
        if (liveSnapActive_) { liveSnapActive_ = false; viewport()->update(); }
        return;
    }

    const double px = studToPx();
    const QPointF mouseStuds(lastMouseScenePos_.x() / px,
                             lastMouseScenePos_.y() / px);
    QSet<QString> movingGuids;
    for (const auto& s : dragStart_) movingGuids.insert(s.guid);
    const double threshold = connectionSnapThresholdStuds();

    // Collect every free connection across every moving brick, tagged with
    // its CURRENT world position (factoring in Qt's drag translation) and
    // its distance to the cursor. The one nearest the cursor is the
    // "lead" the user wants to snap — that matches their intuition of
    // "the connection I'm dragging toward".
    struct FreeConn {
        const core::Brick* brick;
        const BrickOriginSnapshot* snap;
        int connIdx;
        QPointF worldPos;
        double mouseDistSq;
    };
    std::vector<FreeConn> free;

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
            FreeConn fc;
            fc.brick = b;
            fc.snap = &s;
            fc.connIdx = i;
            fc.worldPos = centerStuds + rotatePoint(c.position, b->orientation);
            const QPointF d = fc.worldPos - mouseStuds;
            fc.mouseDistSq = d.x() * d.x() + d.y() * d.y();
            free.push_back(fc);
        }
    }

    // Try EVERY free moving conn. Pick the pair with the smallest
    // conn-to-target distance. Mouse proximity is a tiebreaker when two
    // candidates have near-equal translations — this lets the user's
    // cursor position nudge the choice among otherwise-equal snaps, but
    // doesn't bail out on the first mouse-nearest conn that happens to
    // have a mediocre target.
    ConnectionSnapResult best;
    int bestConnIdx = -1;
    const core::Brick* bestBrick = nullptr;
    const BrickOriginSnapshot* bestSnap = nullptr;
    double bestTranslationSq = std::numeric_limits<double>::max();
    double bestMouseDistSq   = std::numeric_limits<double>::max();
    constexpr double kTieStudsSq = 4.0 * 4.0;   // within 4 studs = "tied"

    for (const auto& fc : free) {
        const QPointF centerPx = fc.snap->item->scenePos();
        const QPointF centerStuds = QPointF(centerPx.x() / px, centerPx.y() / px)
                                  + parts_.imageOffset(fc.brick->partNumber, fc.brick->orientation);
        auto r = ::bld::ui::masterBrickSnap(*map_, parts_, *fc.brick, centerStuds,
                                            fc.connIdx, movingGuids, threshold);
        if (!r.applied) continue;
        const double magSq = r.translationStuds.x() * r.translationStuds.x()
                           + r.translationStuds.y() * r.translationStuds.y();
        bool take = false;
        if (magSq + kTieStudsSq < bestTranslationSq) {
            take = true;
        } else if (std::abs(magSq - bestTranslationSq) <= kTieStudsSq
                   && fc.mouseDistSq < bestMouseDistSq) {
            take = true;
        }
        if (take) {
            bestTranslationSq = magSq;
            bestMouseDistSq   = fc.mouseDistSq;
            best = r;
            bestConnIdx = fc.connIdx;
            bestBrick = fc.brick;
            bestSnap = fc.snap;
        }
    }

    // Status-bar diagnostic so the user can tell WHY snap did or didn't
    // fire. Shown only when live dragging; cleared by commitDragIfMoved
    // on release.
    auto statusHint = [this](const QString& msg) {
        if (auto* mw = window())
            if (auto* sb = mw->findChild<QStatusBar*>())
                sb->showMessage(msg, 1500);
    };

    if (!best.applied || !bestBrick || !bestSnap) {
        // No connection snap available (e.g., dragging parts without
        // connection points like Tables, or nothing within threshold).
        // Fall back to live grid snap so the group still tracks the grid
        // while dragging. Single-brick live drags already get grid snap
        // via SnappingPixmap::itemChange; this covers the multi-brick
        // case where that per-item snap is deliberately disabled.
        if (snapStepStuds_ > 0.0 && dragStart_.size() > 1) {
            const auto& anchor = dragStart_.front();
            if (anchor.item) {
                const double px2 = px;  // readability
                const QPointF anchorCenterPx = anchor.item->scenePos();
                const QPointF anchorCenterStuds(anchorCenterPx.x() / px2,
                                                anchorCenterPx.y() / px2);
                const QPointF snapped(
                    std::round(anchorCenterStuds.x() / snapStepStuds_) * snapStepStuds_,
                    std::round(anchorCenterStuds.y() / snapStepStuds_) * snapStepStuds_);
                const QPointF shiftStuds = snapped - anchorCenterStuds;
                const QPointF shiftPxGrid(shiftStuds.x() * px2, shiftStuds.y() * px2);
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
        // Diagnose *why* no connection snap fired: free-conn count,
        // threshold, and whether any target of matching type exists.
        if (free.empty()) {
            statusHint(tr("Connection snap: no free connections in selection"));
        } else {
            statusHint(tr("Connection snap: %1 moving conn(s), no target within %2 studs")
                .arg(free.size()).arg(threshold, 0, 'f', 0));
        }
        if (liveSnapActive_) { liveSnapActive_ = false; viewport()->update(); }
        return;
    }

    statusHint(tr("Connection snap active (%1 candidate conn(s))")
               .arg(free.size()));

    // Shift every dragged item by the same translation. Suppress the
    // per-item grid-snap itemChange for this pass so our connection
    // alignment survives the setPos round-trip.
    const QPointF shiftPx(best.translationStuds.x() * px,
                          best.translationStuds.y() * px);
    if (std::abs(shiftPx.x()) > 0.01 || std::abs(shiftPx.y()) > 0.01) {
        rendering::SceneBuilder::setSuppressItemSnap(true);
        for (const auto& s : dragStart_) {
            if (!s.item) continue;
            s.item->setPos(s.item->scenePos() + shiftPx);
        }
        rendering::SceneBuilder::setSuppressItemSnap(false);
    }

    // Draw the snap ring at the active connection's post-snap world pos.
    if (auto meta = parts_.metadata(bestBrick->partNumber);
        meta && bestConnIdx >= 0 && bestConnIdx < meta->connections.size()) {
        const auto& ac = meta->connections[bestConnIdx];
        const QPointF centerPx = bestSnap->item->scenePos() + shiftPx;
        const QPointF centerStuds = QPointF(centerPx.x() / px, centerPx.y() / px)
                                  + parts_.imageOffset(bestBrick->partNumber, bestBrick->orientation);
        const QPointF activeConnWorldAfter =
            centerStuds + rotatePoint(ac.position, bestBrick->orientation);
        liveSnapPointScene_ = QPointF(activeConnWorldAfter.x() * px,
                                      activeConnWorldAfter.y() * px);
    }
    liveSnapActive_ = true;
    viewport()->update();
}

void MapView::commitDragIfMoved() {
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
        const double threshold = connectionSnapThresholdStuds();

        struct FreeConn {
            const core::Brick* brick;
            int connIdx;
            QPointF worldPos;
            QPointF centerStuds;
            double mouseDistSq;
        };
        std::vector<FreeConn> free;

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
                FreeConn fc;
                fc.brick = b;
                fc.connIdx = i;
                fc.centerStuds = centerStuds;
                fc.worldPos = centerStuds + rotatePoint(c.position, b->orientation);
                const QPointF d = fc.worldPos - mouseStuds;
                fc.mouseDistSq = d.x() * d.x() + d.y() * d.y();
                free.push_back(fc);
            }
        }

        ConnectionSnapResult best;
        const core::Brick* bestBrick = nullptr;
        double bestTranslationSq = std::numeric_limits<double>::max();
        double bestMouseDistSq   = std::numeric_limits<double>::max();
        constexpr double kTieStudsSq = 4.0 * 4.0;

        for (const auto& fc : free) {
            auto r = ::bld::ui::masterBrickSnap(*map_, parts_, *fc.brick, fc.centerStuds,
                                                fc.connIdx, movingGuids, threshold);
            if (!r.applied) continue;
            const double magSq = r.translationStuds.x() * r.translationStuds.x()
                               + r.translationStuds.y() * r.translationStuds.y();
            bool take = false;
            if (magSq + kTieStudsSq < bestTranslationSq) {
                take = true;
            } else if (std::abs(magSq - bestTranslationSq) <= kTieStudsSq
                       && fc.mouseDistSq < bestMouseDistSq) {
                take = true;
            }
            if (take) {
                bestTranslationSq = magSq;
                bestMouseDistSq   = fc.mouseDistSq;
                best = r;
                bestBrick = fc.brick;
            }
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
