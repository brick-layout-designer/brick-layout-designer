#include "MapView.h"
#include "LoadingCard.h"

#include "../core/Brick.h"
#include "../core/Layer.h"
#include "../core/LayerArea.h"
#include "../core/LayerBrick.h"
#include "../core/LayerGrid.h"
#include "../core/LayerRuler.h"
#include "../core/LayerText.h"
#include "../core/Map.h"
#include "../core/TextCell.h"
#include "../edit/AreaCommands.h"
#include "../edit/Connectivity.h"
#include "../edit/EditCommands.h"
#include "../edit/LayerCommands.h"
#include "BudgetSession.h"
#include "../edit/FlexMove.h"
#include "../edit/RulerCommands.h"
#include "../edit/LabelCommands.h"
#include "../edit/TextCommands.h"
#include "../edit/VenueCommands.h"
#include "../core/Venue.h"
#include "../parts/BrickPlacement.h"
#include "../parts/PartsLibrary.h"
#include "../rendering/SceneBuilder.h"
#include "ConnectionSnap.h"
#include "MapViewInternal.h"
#include "SelectionOverlay.h"
#include "SelectionStyle.h"
#include "TouchMode.h"
#include "EditDialogs.h"
#include "ModuleLibraryPanel.h"   // kModuleDragMimeType
#include "PartsBrowser.h"         // kPartMimeType
#include "VenueDialog.h"
#include "../edit/ModuleCommands.h"
#include "../saveload/BbmReader.h"

#include <QDragEnterEvent>
#include <QTimer>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsSimpleTextItem>
#include <QImage>
#include <QCheckBox>
#include <QSettings>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QHash>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QFileInfo>
#include <QScrollBar>
#include <QStatusBar>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QSet>
#include <QUndoStack>
#include <QUuid>
#include <QWheelEvent>

#include <cmath>
#include <optional>

namespace bld::ui {

using detail::kBrickDataLayerIndex;
using detail::kBrickDataGuid;
using detail::kBrickDataKind;
using detail::isBrickItem;
using detail::isTextItem;
using detail::isRulerItem;
using detail::isLabelItem;
using detail::isVenueItem;
using detail::studToPx;

using detail::kMinZoom;
using detail::kMaxZoom;

MapView::MapView(parts::PartsLibrary& parts, QWidget* parent)
    : QGraphicsView(parent), parts_(parts) {
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setAcceptDrops(true);   // accept part drags from the PartsBrowser panel
    // Left-button drag: rubber-band select (item drag is still available via
    // ItemIsMovable on individual brick items). Middle-button drag: pan the
    // view (handled manually in mousePress/Move/Release).
    setDragMode(QGraphicsView::RubberBandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    // Full-viewport repaints avoid the "trails" behind rotated bricks that
    // SmartViewportUpdate leaves when the item's bounding rect in scene
    // coords changes more than its local rect signals.
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    // No scrollbars: pan is middle-button drag. Visible scrollbars would
    // steal wheel events (breaking zoom when the cursor is over them) and
    // can take keyboard focus. The internal scroll-position machinery is
    // still used by mouseMoveEvent's pan code via scrollBar()->setValue().
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    horizontalScrollBar()->setFocusPolicy(Qt::NoFocus);
    verticalScrollBar()->setFocusPolicy(Qt::NoFocus);
    // Install ourselves as an event filter on the viewport. The filter
    // routes wheel events straight into our wheelEvent() and consumes
    // them so QAbstractScrollArea's base class never gets to pan the
    // viewport via the (invisible) scrollbars. Without this, trackpad
    // wheels ended up zooming AND panning vertically at the same time.
    viewport()->installEventFilter(this);

    auto* scene = new QGraphicsScene(this);
    scene->setBackgroundBrush(QColor(100, 149, 237));
    setScene(scene);

    // Selection overlay: a persistent scene item that paints outlines
    // around every selected brick. Lives with the highest z-value so it's
    // always on top of bricks/text/rulers.
    selectionOverlay_ = new SelectionOverlay();
    scene->addItem(selectionOverlay_);

    connect(scene, &QGraphicsScene::selectionChanged, this, [this]{
        // When the user clicks any single piece of a ruler (one line
        // segment or the midtext label), extend the scene selection to
        // every other piece sharing the same ruler guid. With unique
        // guids (restored by migrateNonNumericIds in 3.78) this scopes
        // to exactly one ruler. Needed so Qt's built-in multi-item drag
        // translates all pieces of the ruler rigidly when the user
        // drags any of them.
        //
        // Same reentrancy guard handles Group expansion: clicking any
        // brick that has myGroupId set auto-selects every sibling in
        // that group. Without this, Group did nothing user-visible —
        // groups were saved/loaded but neither dragged nor selected
        // together. We do this only for bricks we can resolve in the
        // current map; groupId lookups walk every brick layer once.
        static bool reentrant = false;
        if (!reentrant) {
            reentrant = true;
            QSet<QString> rulerGuids;
            QSet<QString> brickGroupIds;
            QSet<QString> selectedBrickGuids;
            for (QGraphicsItem* it : this->scene()->selectedItems()) {
                if (!it) continue;
                if (isRulerItem(it)) {
                    const QString g = it->data(kBrickDataGuid).toString();
                    if (!g.isEmpty()) rulerGuids.insert(g);
                } else if (isBrickItem(it)) {
                    selectedBrickGuids.insert(it->data(kBrickDataGuid).toString());
                }
            }
            if (!rulerGuids.isEmpty()) {
                for (QGraphicsItem* any : this->scene()->items()) {
                    if (!isRulerItem(any)) continue;
                    if (any->isSelected()) continue;
                    const QString g = any->data(kBrickDataGuid).toString();
                    if (g.isEmpty()) continue;
                    if (rulerGuids.contains(g)) any->setSelected(true);
                }
            }
            if (map_ && !selectedBrickGuids.isEmpty()) {
                // Look up the group id of each selected brick.
                for (const auto& L : map_->layers()) {
                    if (!L || L->kind() != core::LayerKind::Brick) continue;
                    for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks) {
                        if (b.myGroupId.isEmpty()) continue;
                        if (selectedBrickGuids.contains(b.guid))
                            brickGroupIds.insert(b.myGroupId);
                    }
                }
                if (!brickGroupIds.isEmpty()) {
                    // Find every brick whose myGroupId matches any of those.
                    QSet<QString> siblingGuids;
                    for (const auto& L : map_->layers()) {
                        if (!L || L->kind() != core::LayerKind::Brick) continue;
                        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks) {
                            if (b.myGroupId.isEmpty()) continue;
                            if (brickGroupIds.contains(b.myGroupId))
                                siblingGuids.insert(b.guid);
                        }
                    }
                    for (QGraphicsItem* any : this->scene()->items()) {
                        if (!isBrickItem(any) || any->isSelected()) continue;
                        const QString g = any->data(kBrickDataGuid).toString();
                        if (siblingGuids.contains(g)) any->setSelected(true);
                    }
                }
            }
            reentrant = false;
        }
        refreshSelectionOverlay();
        emit selectionChanged();
    });
    // Also refresh the overlay whenever anything in the scene changes
    // geometry — this keeps the outline glued to the brick while the user
    // drags it. Qt debounces `changed` to once per paint cycle, so this
    // doesn't over-fire during live drag.
    connect(scene, &QGraphicsScene::changed, this, [this]{
        if (!this->scene()->selectedItems().isEmpty()) refreshSelectionOverlay();
    });

    builder_ = std::make_unique<rendering::SceneBuilder>(*scene, parts_);

    // No per-item live snap hook: connection snap runs as a single rigid
    // group shift in MapView::mouseMoveEvent after Qt has moved each item
    // by the drag delta. See applyLiveConnectionSnap().

    undoStack_ = std::make_unique<QUndoStack>(this);
    // Every undo / redo mutates core::Map; the scene items were built before
    // the mutation, so we need to rebuild the scene afterwards for the UI to
    // reflect the restored state. Without this, Ctrl+Z appears to do nothing.
    connect(undoStack_.get(), &QUndoStack::indexChanged, this, [this](int){
        if (!map_) return;
        // Recompute every brick's linkedToId against current world
        // positions before rebuilding the scene. Otherwise a brick moved
        // away from its partner keeps the old link, which makes the
        // connection appear "occupied" to snap + display.
        edit::rebuildConnectivity(*map_, parts_);
        // Go through rebuildScene() (not builder_->build directly) so an
        // undo / redo fired mid-drag also drops the drag snapshots that
        // point at the items the rebuild is about to delete.
        rebuildScene();
    });

    // Touch: fingers reach the viewport as touches (pinch, pan, long
    // press); the touch action bar follows touch mode and the selection.
    viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
    setProperty("bldNoTouchScroll", true);
    connect(this, &MapView::selectionChanged, this, &MapView::refreshTouchBar);
    connect(&TouchMode::instance(), &TouchMode::changed, this, &MapView::refreshTouchBar);

    loadingCard_ = new LoadingCard(this, LoadingCard::Place::Centre);
    // Deferred: Retry rebuilds the scene, and never from inside the click.
    connect(loadingCard_, &LoadingCard::retryRequested, this,
            [this] { QTimer::singleShot(0, this, &MapView::retryFailedPictures); });
    connect(loadingCard_, &QObject::destroyed, this, [this] { loadingCard_ = nullptr; });
}

MapView::~MapView() {
    // Teardown order matters. QGraphicsView's base destructor will
    // destroy the scene, which deletes items individually. As each
    // selectable item is removed Qt fires selectionChanged on the
    // scene — our handler then walks scene->items() / setSelected on
    // survivors that may already be mid-destruction. Disconnect from
    // the scene signals BEFORE any of that runs.
    if (scene()) scene()->disconnect(this);
    // The card, a child, is deleted after this destructor: its destroyed
    // signal must not reach a MapView that's already gone.
    if (loadingCard_) loadingCard_->disconnect(this);
    // The undo stack's destructor clears it, which says indexChanged: the
    // handler would rebuild the scene of a view being destroyed and tell
    // the main window, already gone, that the selection changed. Let it go
    // quietly, before anything else here is destroyed.
    if (undoStack_) {
        undoStack_->disconnect();
        undoStack_->blockSignals(true);
        undoStack_.reset();
    }
}

void MapView::loadMap(std::unique_ptr<core::Map> map) {
    undoStack_->clear();
    // Any in-flight drag snapshots reference QGraphicsItems in the
    // previous scene — builder_->clear() / build() below will delete
    // those. Drop the snapshots first so no mouse event re-enters with
    // dangling pointers.
    dragStart_.clear();
    rulerDragStart_.clear();
    labelDragStart_.clear();
    flex_.reset();
    flexItems_.clear();
    liveSnapActive_ = false;
    // Any drag-from-browser preview ghost lives in the scene and would
    // dangle after the SceneBuilder rebuild; clear it before the load.
    if (dragPreviewItem_) {
        scene()->removeItem(dragPreviewItem_);
        delete dragPreviewItem_;
        dragPreviewItem_ = nullptr;
        dragPreviewKey_.clear();
    }
    clearRulerPreview();
    // Read the new layout's pictures first, with the loading card. Live
    // updates can arrive while it paints; a newer map loaded meanwhile wins.
    const int generation = ++loadGeneration_;
    if (map && !preloadPictures(*map)) return;
    if (generation != loadGeneration_) return;
    map_ = std::move(map);
    if (!map_) {
        builder_->clear();
        finishLoading();
        emit mapLoaded();
        return;
    }

    scene()->setBackgroundBrush(map_->backgroundColor.color);
    parts::placement::fixStaleAreas(*map_, parts_);
    // Freshly-loaded .bbm may have stale linkedToId values (the file
    // stores the last known state, which can disagree with current world
    // positions after e.g. external edits). Rebuild the connection graph
    // from positions before first render so snap + dot markers are
    // correct from the start.
    edit::rebuildConnectivity(*map_, parts_);
    builder_->build(*map_);
    applyViewFilter();
    // Give the view a much bigger scene rect than the current content so
    // the user can pan well outside the existing bricks to add new ones
    // or extend the layout. ~50 000 px = ~6 250 studs on each side, which
    // is well beyond any realistic train-club layout. Without this the
    // sceneRect defaulted to the items bounding box and pan stopped at
    // the last brick.
    const QRectF content = scene()->itemsBoundingRect();
    const double kPadPx = 50000.0;
    const QRectF bigRect = content.isEmpty()
        ? QRectF(-kPadPx, -kPadPx, kPadPx * 2, kPadPx * 2)
        : content.adjusted(-kPadPx, -kPadPx, kPadPx, kPadPx);
    scene()->setSceneRect(bigRect);
    if (!content.isEmpty()) {
        fitInView(content.adjusted(-50, -50, 50, 50), Qt::KeepAspectRatio);
    }
    viewport()->update();
    finishLoading();
    emit selectionChanged();
    emit mapLoaded();
}

void MapView::rebuildScene() {
    if (!map_) return;
    // SceneBuilder::build() deletes every QGraphicsItem* before
    // repopulating the scene. Any raw pointers we're holding in drag
    // snapshots (dragStart_ / rulerDragStart_ / labelDragStart_)
    // become dangling the moment we call build(). If a drag is in
    // progress when something triggers a rebuild (undo/redo, context
    // menu action, tool op) the next mouseMove will segfault on
    // s.item->scenePos(). Cancel any in-flight drag here so callers
    // don't have to remember. Mid-drag rebuild → drag aborts cleanly.
    dragStart_.clear();
    rulerDragStart_.clear();
    labelDragStart_.clear();
    // A flex move points into the bricks being rebuilt from.
    flex_.reset();
    flexItems_.clear();
    liveSnapActive_ = false;
    if (dragPreviewItem_) {
        scene()->removeItem(dragPreviewItem_);
        delete dragPreviewItem_;
        dragPreviewItem_ = nullptr;
        dragPreviewKey_.clear();
    }
    clearRulerPreview();

    // Snapshot the currently-selected (layer, guid, kind) triples before
    // the rebuild wipes the scene so we can reselect the same logical
    // items on the rebuilt pixmaps. Without this, moving a brick
    // deselected it the instant the move committed.
    struct SelKey { int layer; QString guid; QString kind; };
    QList<SelKey> preserve;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!it) continue;
        const QString kind = it->data(kBrickDataKind).toString();
        if (kind.isEmpty()) continue;  // e.g. overlay item itself
        preserve.append({ it->data(kBrickDataLayerIndex).toInt(),
                          it->data(kBrickDataGuid).toString(), kind });
    }
    builder_->build(*map_);
    applyViewFilter();
    // Reselect by (layer, guid, kind) — builds a quick index of the new
    // items once so each lookup is O(1).
    if (!preserve.isEmpty()) {
        QHash<QString, QGraphicsItem*> byKey;
        for (QGraphicsItem* it : scene()->items()) {
            const QString kind = it->data(kBrickDataKind).toString();
            if (kind.isEmpty()) continue;
            byKey.insert(QString::number(it->data(kBrickDataLayerIndex).toInt())
                             + QLatin1Char('|') + it->data(kBrickDataGuid).toString()
                             + QLatin1Char('|') + kind,
                         it);
        }
        for (const auto& k : preserve) {
            const QString key = QString::number(k.layer) + QLatin1Char('|')
                                + k.guid + QLatin1Char('|') + k.kind;
            if (auto* it = byKey.value(key)) it->setSelected(true);
        }
    }
    refreshSelectionOverlay();
    viewport()->update();
    emit selectionChanged();
}

void MapView::setViewFilter(std::optional<ViewFilter> filter) {
    viewFilter_ = std::move(filter);
    applyViewFilter();
}

void MapView::applyViewFilter() {
    if (!map_ || !builder_) return;
    const auto& layers = map_->layers();
    for (size_t i = 0; i < layers.size(); ++i) {
        const auto& l = layers[i];
        if (!l || l->kind() == core::LayerKind::Grid) continue;
        const bool on = viewFilter_ && viewFilter_->sheets ? viewFilter_->sheets->contains(l->guid) : l->visible;
        builder_->setLayerVisible(static_cast<int>(i), on);
    }
    builder_->setLabelsVisible(!viewFilter_ || viewFilter_->labels);
    viewport()->update();
}

std::optional<QRectF> MapView::screenRectStuds() const {
    if (!map_) return std::nullopt;
    const QRectF px = mapToScene(viewport()->rect()).boundingRect();
    const double k = rendering::SceneBuilder::kPixelsPerStud;
    if (px.isEmpty()) return std::nullopt;
    return QRectF(px.x() / k, px.y() / k, px.width() / k, px.height() / k);
}

void MapView::showRegionStuds(const QRectF& studs) {
    const double k = rendering::SceneBuilder::kPixelsPerStud;
    const QRectF px(studs.x() * k, studs.y() * k, studs.width() * k, studs.height() * k);
    if (px.isEmpty()) return;
    fitInView(px, Qt::KeepAspectRatio);
    viewport()->update();
}

bool MapView::eventFilter(QObject* obj, QEvent* ev) {
    // Intercept wheel events on the viewport so they go through our
    // zoom-only wheelEvent() and never fall through to the base class
    // (which would pan via scrollbars). Returning true marks the event
    // as handled.
    if (obj == viewport() && ev->type() == QEvent::Wheel) {
        wheelEvent(static_cast<QWheelEvent*>(ev));
        return true;
    }
    return QGraphicsView::eventFilter(obj, ev);
}

void MapView::wheelEvent(QWheelEvent* e) {
    // The wheel is ZOOM ONLY. We always accept the event so
    // QAbstractScrollArea's base class doesn't fall through to
    // scrollbar-driven pan — that made horizontal trackpad gestures
    // and "wheel with no vertical delta" events slide the map around,
    // which the user never wants here. Middle-click drag is the only
    // pan mechanism.
    //
    // Step size: proportional to angleDelta (120 units per notch on a
    // classic mouse wheel; smaller numbers for high-res trackpads). The
    // base factor 1.0015^delta_y yields ~1.20× for a full notch —
    // noticeably gentler than a fixed 1.15× step, and fractional
    // increments feel continuous instead of twitchy.
    const double rawDelta = e->angleDelta().y();
    e->accept();   // swallow every wheel event regardless — no pan fallthrough
    if (rawDelta == 0.0) return;
    constexpr double kBase = 1.0015;
    // High-res trackpads (especially on macOS) can emit angle deltas
    // >1000 in a single event. 1.0015^1000 ≈ 4.5e6 — one wheel tick
    // blowing through the whole zoom range and yanking the anchored
    // scene point far off-screen. Cap the delta per event so no
    // single wheel event can zoom more than ~2× either direction.
    // Users still accumulate fast zoom across multiple events; we
    // just don't fly across the map in one frame.
    constexpr double kMaxAbsDeltaPerEvent = 480.0;   // ~2× at 1.0015 base
    const double clampedDelta = std::clamp(rawDelta,
                                           -kMaxAbsDeltaPerEvent,
                                            kMaxAbsDeltaPerEvent);
    const double step = std::pow(kBase, clampedDelta);
    const double current = transform().m11();
    const double next = std::clamp(current * step, kMinZoom, kMaxZoom);
    const double actualStep = next / current;
    if (std::abs(actualStep - 1.0) < 1e-5) return;
    scale(actualStep, actualStep);
}


// MapView drag mechanics (selectedBrickSnapshots, captureDragStart, snap
// computations, commitDragIfMoved) live in MapViewDrag.cpp. Clipboard
// ops live in MapViewClipboard.cpp.

bool MapView::wouldDragGridOrigin(QPoint viewPos) const {
    if (!map_ || tool_ != Tool::Select || map_->selectedLayerIndex < 0
        || map_->selectedLayerIndex >= static_cast<int>(map_->layers().size()))
        return false;
    auto* L = map_->layers()[map_->selectedLayerIndex].get();
    QGraphicsItem* under = itemAt(viewPos);
    while (under && under->parentItem()) under = under->parentItem();
    const bool onItem = under && (isBrickItem(under) || isTextItem(under) || isRulerItem(under)
                                  || isLabelItem(under) || isVenueItem(under));
    return L && L->kind() == core::LayerKind::Grid && L->visible && !onItem
        && static_cast<core::LayerGrid&>(*L).displayCellIndex;
}

double MapView::handleRadiusScenePx(double screenPx) const {
    const double scale = std::max(1e-6, transform().m11());
    return std::max(selection::kHandleRadius, screenPx / scale);
}

bool MapView::rulerEndpointAt(QPointF clickScene, bool startDrag) {
    if (!map_ || tool_ != Tool::Select) return false;
    QSet<QString> selRulerGuids;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!it) continue;
        if (it->data(2).toString() == QStringLiteral("ruler"))
            selRulerGuids.insert(it->data(1).toString());
    }
    if (selRulerGuids.size() != 1) return false;
    const QString g = *selRulerGuids.constBegin();
    const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
    // A generous radius; in touch mode at least a fingertip on screen.
    double hitR = 0.8 * pxPerStud * 1.5;
    if (TouchMode::instance().active()) hitR = std::max(hitR, handleRadiusScenePx(TouchMode::kMinTarget));
    for (int li = 0; li < static_cast<int>(map_->layers().size()); ++li) {
        auto* L = map_->layers()[li].get();
        if (!L || L->kind() != core::LayerKind::Ruler) continue;
        const auto& RL = static_cast<const core::LayerRuler&>(*L);
        for (const auto& any : RL.rulers) {
            if (any.kind != core::RulerKind::Linear) continue;
            if (any.linear.guid != g) continue;
            const QPointF p1(any.linear.point1.x() * pxPerStud, any.linear.point1.y() * pxPerStud);
            const QPointF p2(any.linear.point2.x() * pxPerStud, any.linear.point2.y() * pxPerStud);
            auto near = [&](QPointF h) {
                const QPointF d = h - clickScene;
                return std::hypot(d.x(), d.y()) <= hitR;
            };
            int idx = -1;
            if (near(p1)) idx = 0;
            else if (near(p2)) idx = 1;
            if (idx < 0) continue;
            if (startDrag) {
                draggingRulerEndpoint_ = true;
                rulerEndpointIndex_ = idx;
                rulerEndpointLayer_ = li;
                rulerEndpointGuid_ = g;
                rulerEndpointDragLast_ = clickScene;
                rulerEndpointOriginalStuds_ = (idx == 0) ? any.linear.point1 : any.linear.point2;
            }
            return true;
        }
    }
    return false;
}

void MapView::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && scene()) {
        pressSelection_.clear();
        for (QGraphicsItem* it : scene()->selectedItems())
            if (isBrickItem(it)) pressSelection_.insert(it->data(kBrickDataGuid).toString());
    }
    lastMouseScenePos_ = mapToScene(e->pos());

    if (gridOriginDragging_ && e->button() == Qt::RightButton) {
        // Cancel, as BlueBrick does.
        gridOriginDragging_ = false;
        setGridOrigin(gridOriginBefore_);
        unsetCursor();
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton && wouldDragGridOrigin(e->pos())) {
        auto* L = map_->layers()[map_->selectedLayerIndex].get();
        gridOriginDragging_ = true;
        gridLayer_ = map_->selectedLayerIndex;
        gridOriginBefore_ = static_cast<core::LayerGrid&>(*L).cellIndexCorner;
        gridDragStartCell_ = gridDragLastCell_ = gridCellAt(lastMouseScenePos_);
        setCursor(Qt::SizeAllCursor);
        e->accept();
        return;
    }

    // Endpoint-handle hit-test: if exactly one linear ruler is selected
    // and the click lands on one of its handles, capture the drag and skip
    // Qt's default selection / rubber-band path. Mirrors BlueBrick's "drag
    // the handle to reshape the ruler" behaviour.
    if (e->button() == Qt::LeftButton && rulerEndpointAt(mapToScene(e->pos()), true)) {
        e->accept();
        return;
    }

    if (e->button() == Qt::MiddleButton) {
        panning_ = true;
        panAnchor_ = e->pos();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    // Ruler draw tools: record press position; create the ruler on release.
    if (e->button() == Qt::LeftButton && map_ &&
        (tool_ == Tool::DrawLinearRuler || tool_ == Tool::DrawCircularRuler)) {
        drawingRuler_ = true;
        rulerStart_ = mapToScene(e->pos());
        // Snap the start point so a drag from a near-grid spot anchors
        // exactly on the grid intersection. Without this the line jumps
        // visibly when the user moves the mouse and the endpoint snaps.
        if (snapStepStuds_ > 0.0) {
            const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
            const double sPx = snapStepStuds_ * pxPerStud;
            rulerStart_ = QPointF(std::round(rulerStart_.x() / sPx) * sPx,
                                   std::round(rulerStart_.y() / sPx) * sPx);
        }
        e->accept();
        return;
    }

    // Venue outline / obstacle drawing: each left-click adds a vertex.
    // Right-click (or Enter) finishes the polygon. Escape cancels.
    if (e->button() == Qt::LeftButton && map_ &&
        (tool_ == Tool::DrawVenueOutline || tool_ == Tool::DrawVenueObstacle)) {
        const double px = rendering::SceneBuilder::kPixelsPerStud;
        const QPointF scenePos = mapToScene(e->pos());
        venueDrawPoints_.append(QPointF(scenePos.x() / px, scenePos.y() / px));
        updateVenueDrawPreview();
        e->accept();
        return;
    }
    if (e->button() == Qt::RightButton && map_ &&
        (tool_ == Tool::DrawVenueOutline || tool_ == Tool::DrawVenueObstacle)) {
        finishVenueDraw();
        e->accept();
        return;
    }

    // Paint / erase: swallow the event so the graphics view doesn't start a
    // rubber band or drag, and stamp the cell under the cursor.
    if (e->button() == Qt::LeftButton && map_ &&
        (tool_ == Tool::PaintArea || tool_ == Tool::EraseArea)) {
        strokeCellsTouched_.clear();
        // Find top-most visible area layer; paint there. If none exists,
        // create one implicitly — same pattern the ruler tool uses, and
        // matches user expectation that "click paint, then click map →
        // it paints" without first needing to manually add a layer.
        int targetLayer = -1;
        core::LayerArea* target = nullptr;
        for (int i = static_cast<int>(map_->layers().size()) - 1; i >= 0; --i) {
            auto* L = map_->layers()[i].get();
            if (L && L->kind() == core::LayerKind::Area && L->visible) {
                targetLayer = i;
                target = static_cast<core::LayerArea*>(L);
                break;
            }
        }
        if (!target) {
            auto L = std::make_unique<core::LayerArea>();
            L->guid = core::newBbmId();
            L->name = tr("Area");
            target = L.get();
            map_->layers().push_back(std::move(L));
            targetLayer = static_cast<int>(map_->layers().size()) - 1;
            emit layersChanged();
        }
        if (target) {
            const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
            const QPointF sp = mapToScene(e->pos());
            const double cell = std::max(1, target->areaCellSizeInStud) * pxPerStud;
            const int cx = static_cast<int>(std::floor(sp.x() / cell));
            const int cy = static_cast<int>(std::floor(sp.y() / cell));
            strokeCellsTouched_.insert(QPoint(cx, cy));
            std::vector<edit::PaintAreaCellsCommand::Change> chg;
            chg.push_back({ cx, cy,
                tool_ == Tool::PaintArea ? std::optional<QColor>(paintColor_)
                                          : std::nullopt });
            undoStack_->push(new edit::PaintAreaCellsCommand(*map_, targetLayer, std::move(chg)));  // indexChanged handler rebuilds the scene
        }
        e->accept();
        return;
    }
    QGraphicsView::mousePressEvent(e);
    if (e->button() == Qt::LeftButton) {
        captureDragStart();
        // Master-brick snap anchor (BlueBrick-style): remember WHICH brick
        // was clicked and which of its connections was nearest. That one
        // connection is the sole snap lead for the whole drag.
        captureGrabAnchor(lastMouseScenePos_);
    }
}

void MapView::mouseMoveEvent(QMouseEvent* e) {
    lastMouseScenePos_ = mapToScene(e->pos());

    if (gridOriginDragging_) {
        const QPoint cell = gridCellAt(lastMouseScenePos_);
        if (cell != gridDragLastCell_) {
            gridDragLastCell_ = cell;
            setGridOrigin(gridOriginBefore_ + (cell - gridDragStartCell_));
        }
        e->accept();
        return;
    }

    if (flex_ && (e->buttons() & Qt::LeftButton)) {
        const double px = rendering::SceneBuilder::kPixelsPerStud;
        const auto snapped = flex_->moveTo(lastMouseScenePos_ / px, snapStepStuds_);
        flexMoved_ = true;
        updateFlexItems();
        liveSnapActive_ = snapped.has_value();
        if (snapped) liveSnapPointScene_ = *snapped * px;
        viewport()->update();
        e->accept();
        return;
    }

    // Live update of a ruler endpoint drag: mutate the model in-place so
    // the scene re-renders the line at the new position; commit a real
    // undoable command on release. We don't push a command per
    // mouse-move because the undo stack would balloon.
    if (draggingRulerEndpoint_ && (e->buttons() & Qt::LeftButton) && map_) {
        rulerEndpointDragLast_ = mapToScene(e->pos());
        const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
        QPointF target(rulerEndpointDragLast_.x() / pxPerStud,
                       rulerEndpointDragLast_.y() / pxPerStud);
        if (snapStepStuds_ > 0.0) {
            target.setX(std::round(target.x() / snapStepStuds_) * snapStepStuds_);
            target.setY(std::round(target.y() / snapStepStuds_) * snapStepStuds_);
        }
        if (auto* L = map_->layers()[rulerEndpointLayer_].get();
            L && L->kind() == core::LayerKind::Ruler) {
            auto& RL = static_cast<core::LayerRuler&>(*L);
            for (auto& any : RL.rulers) {
                if (any.kind != core::RulerKind::Linear) continue;
                if (any.linear.guid != rulerEndpointGuid_) continue;
                if (rulerEndpointIndex_ == 0) any.linear.point1 = target;
                else                          any.linear.point2 = target;
                const QPointF& p1 = any.linear.point1;
                const QPointF& p2 = any.linear.point2;
                const QPointF tl(std::min(p1.x(), p2.x()), std::min(p1.y(), p2.y()));
                const QPointF br(std::max(p1.x(), p2.x()), std::max(p1.y(), p2.y()));
                any.linear.displayArea = QRectF(tl, br);
                break;
            }
            rebuildScene();
        }
        e->accept();
        return;
    }
    // While dragging a selected brick, cursor hint "drop here to delete" when
    // the pointer leaves the viewport (over any dock — typically the Parts
    // panel on the left). Restore on re-entry so in-scene dragging keeps the
    // normal arrow/hand cursor.
    if (!dragStart_.empty() && (e->buttons() & Qt::LeftButton)) {
        if (!viewport()->rect().contains(e->pos())) setCursor(Qt::ForbiddenCursor);
        else                                         unsetCursor();
    }

    // Live ruler tooltip: while dragging in one of the ruler tools,
    // show the current length (linear) or radius (circular) in the
    // status bar so the user knows how long the ruler will be before
    // they release.
    if (drawingRuler_ && (e->buttons() & Qt::LeftButton)) {
        const QPointF endScene = mapToScene(e->pos());
        // Live preview: a line (or circle) plus a near-cursor label
        // showing the current length / radius. Status-bar message is
        // also kept as a backup for users who hide the floating label.
        updateRulerPreview(endScene);
        const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
        const QPointF dStuds((endScene.x() - rulerStart_.x()) / pxPerStud,
                             (endScene.y() - rulerStart_.y()) / pxPerStud);
        const double lenStuds = std::hypot(dStuds.x(), dStuds.y());
        const double lenMm = lenStuds * 8.0;
        QString unit;
        if (lenMm >= 1000) unit = QStringLiteral("%1 m").arg(lenMm / 1000.0, 0, 'f', 2);
        else               unit = QStringLiteral("%1 mm").arg(lenMm, 0, 'f', 0);
        if (auto* mw = window())
            if (auto* sb = mw->findChild<QStatusBar*>())
                sb->showMessage(tool_ == Tool::DrawLinearRuler
                    ? tr("Ruler length: %1 studs  (%2)").arg(lenStuds, 0, 'f', 1).arg(unit)
                    : tr("Ruler radius: %1 studs  (%2)").arg(lenStuds, 0, 'f', 1).arg(unit),
                    1200);
    }

    // Continue a paint/erase stroke as long as the left button is held down
    // and we're in a paint tool. Group each cell change as its own command so
    // Ctrl+Z rolls back one cell at a time (matches vanilla BlueBrick's
    // per-click behaviour).
    if (map_ && (tool_ == Tool::PaintArea || tool_ == Tool::EraseArea)
        && (e->buttons() & Qt::LeftButton)) {
        int targetLayer = -1;
        core::LayerArea* target = nullptr;
        for (int i = static_cast<int>(map_->layers().size()) - 1; i >= 0; --i) {
            auto* L = map_->layers()[i].get();
            if (L && L->kind() == core::LayerKind::Area && L->visible) {
                targetLayer = i; target = static_cast<core::LayerArea*>(L); break;
            }
        }
        if (target) {
            const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
            const QPointF sp = mapToScene(e->pos());
            const double cell = std::max(1, target->areaCellSizeInStud) * pxPerStud;
            const int cx = static_cast<int>(std::floor(sp.x() / cell));
            const int cy = static_cast<int>(std::floor(sp.y() / cell));
            const QPoint k(cx, cy);
            if (!strokeCellsTouched_.contains(k)) {
                strokeCellsTouched_.insert(k);
                std::vector<edit::PaintAreaCellsCommand::Change> chg;
                chg.push_back({ cx, cy,
                    tool_ == Tool::PaintArea ? std::optional<QColor>(paintColor_)
                                              : std::nullopt });
                undoStack_->push(new edit::PaintAreaCellsCommand(*map_, targetLayer, std::move(chg)));  // indexChanged handler rebuilds the scene
            }
        }
        e->accept();
        return;
    }
    if (panning_ && (e->buttons() & Qt::MiddleButton)) {
        const QPoint delta = e->pos() - panAnchor_;
        panAnchor_ = e->pos();
        // Scroll by the delta — negated because scrolling right *shows* the
        // left side, i.e. moves scene content the opposite way of the cursor.
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        e->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(e);
    // After Qt has translated every selected movable item by the mouse
    // delta, run connection snap as a single rigid group shift. This
    // guarantees connections win over the per-item grid snap and keeps
    // a multi-brick group perfectly aligned while snapping.
    if (!dragStart_.empty() && (e->buttons() & Qt::LeftButton)) {
        applyLiveConnectionSnap();
    }
}

void MapView::mouseReleaseEvent(QMouseEvent* e) {
    if (gridOriginDragging_ && e->button() == Qt::LeftButton) {
        gridOriginDragging_ = false;
        unsetCursor();
        const QPoint delta = gridDragLastCell_ - gridDragStartCell_;
        setGridOrigin(gridOriginBefore_);  // the command applies the move
        if (!delta.isNull())
            undoStack_->push(new edit::MoveGridOriginCommand(*map_, gridLayer_, delta.x(), delta.y()));
        e->accept();
        return;
    }
    if (flex_ && e->button() == Qt::LeftButton) {
        finishFlexMove();
        e->accept();
        return;
    }
    if (e->button() == Qt::MiddleButton && panning_) {
        panning_ = false;
        unsetCursor();
        e->accept();
        return;
    }
    // End an endpoint drag: restore the pre-drag value and push a real
    // undo command with the final position, so the entire drag becomes
    // ONE undo step.
    if (e->button() == Qt::LeftButton && draggingRulerEndpoint_ && map_) {
        draggingRulerEndpoint_ = false;
        const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
        QPointF finalStuds(rulerEndpointDragLast_.x() / pxPerStud,
                           rulerEndpointDragLast_.y() / pxPerStud);
        if (snapStepStuds_ > 0.0) {
            finalStuds.setX(std::round(finalStuds.x() / snapStepStuds_) * snapStepStuds_);
            finalStuds.setY(std::round(finalStuds.y() / snapStepStuds_) * snapStepStuds_);
        }
        // Restore pre-drag value so the command's redo() captures the
        // correct `before_` state. Without this the live in-place
        // mutation would fool the command into thinking nothing changed.
        if (auto* L = map_->layers()[rulerEndpointLayer_].get();
            L && L->kind() == core::LayerKind::Ruler) {
            auto& RL = static_cast<core::LayerRuler&>(*L);
            for (auto& any : RL.rulers) {
                if (any.kind != core::RulerKind::Linear) continue;
                if (any.linear.guid != rulerEndpointGuid_) continue;
                if (rulerEndpointIndex_ == 0)
                    any.linear.point1 = rulerEndpointOriginalStuds_;
                else
                    any.linear.point2 = rulerEndpointOriginalStuds_;
                const QPointF& p1 = any.linear.point1;
                const QPointF& p2 = any.linear.point2;
                const QPointF tl(std::min(p1.x(), p2.x()), std::min(p1.y(), p2.y()));
                const QPointF br(std::max(p1.x(), p2.x()), std::max(p1.y(), p2.y()));
                any.linear.displayArea = QRectF(tl, br);
                break;
            }
        }
        if (finalStuds != rulerEndpointOriginalStuds_) {
            undoStack_->push(new edit::MoveRulerEndpointCommand(
                *map_, rulerEndpointLayer_, rulerEndpointGuid_,
                rulerEndpointIndex_, finalStuds));  // the undo handler rebuilds
        } else {
            rebuildScene();  // put back the live-dragged ruler
        }
        // Reselect the ruler so subsequent endpoint drags work.
        for (QGraphicsItem* it : scene()->items()) {
            if (it->data(2).toString() == QStringLiteral("ruler")
                && it->data(1).toString() == rulerEndpointGuid_) {
                it->setSelected(true);
            }
        }
        e->accept();
        return;
    }
    // "Drag out to delete": if the user started a drag in Select mode and
    // released outside the map viewport (typically over the Parts panel or
    // any other dock), treat it as a delete rather than a move.
    if (e->button() == Qt::LeftButton && !dragStart_.empty()
        && !viewport()->rect().contains(e->pos())) {
        clearGrabAnchor();
        dragStart_.clear();
        QGraphicsView::mouseReleaseEvent(e);
        deleteSelected();
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton && drawingRuler_ && map_) {
        drawingRuler_ = false;
        clearRulerPreview();
        const QPointF endScene = mapToScene(e->pos());
        const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
        // Find or implicitly create a ruler layer.
        int targetLayer = -1;
        for (int i = 0; i < static_cast<int>(map_->layers().size()); ++i) {
            if (map_->layers()[i]->kind() == core::LayerKind::Ruler) { targetLayer = i; break; }
        }
        if (targetLayer < 0) {
            auto L = std::make_unique<core::LayerRuler>();
            L->guid = core::newBbmId();
            L->name = tr("Rulers");
            map_->layers().push_back(std::move(L));
            targetLayer = static_cast<int>(map_->layers().size()) - 1;
        }

        core::LayerRuler::AnyRuler any;
        QPointF p1(rulerStart_.x() / pxPerStud, rulerStart_.y() / pxPerStud);
        QPointF p2(endScene.x()    / pxPerStud, endScene.y()    / pxPerStud);
        // Snap both endpoints to the grid when snap is active. Vanilla
        // BlueBrick rounds the cursor to the nearest grid intersection
        // while drawing — we only have a release-time hook here, so we
        // apply the same rounding at commit. Future polish: also snap
        // during drag preview (would feed updateRulerPreview).
        if (snapStepStuds_ > 0.0) {
            auto snap = [s = snapStepStuds_](QPointF p){
                return QPointF(std::round(p.x() / s) * s,
                               std::round(p.y() / s) * s);
            };
            p1 = snap(p1);
            p2 = snap(p2);
        }

        if (tool_ == Tool::DrawLinearRuler) {
            any.kind = core::RulerKind::Linear;
            any.linear.point1 = p1;
            any.linear.point2 = p2;
            const QPointF tl(std::min(p1.x(), p2.x()), std::min(p1.y(), p2.y()));
            const QPointF br(std::max(p1.x(), p2.x()), std::max(p1.y(), p2.y()));
            any.linear.displayArea = QRectF(tl, br);
            any.linear.color = core::ColorSpec::fromKnown(QColor(Qt::black), QStringLiteral("Black"));
            any.linear.lineThickness = 1.0f;
            any.linear.displayDistance = true;
            any.linear.displayUnit = true;
        } else {
            any.kind = core::RulerKind::Circular;
            any.circular.center = p1;
            const QPointF d = p2 - p1;
            const double r = std::hypot(d.x(), d.y());
            any.circular.radius = static_cast<float>(r);
            any.circular.displayArea = QRectF(p1.x() - r, p1.y() - r, 2 * r, 2 * r);
            any.circular.color = core::ColorSpec::fromKnown(QColor(Qt::black), QStringLiteral("Black"));
            any.circular.lineThickness = 1.0f;
            any.circular.displayDistance = true;
            any.circular.displayUnit = true;
        }
        undoStack_->push(new edit::AddRulerItemCommand(*map_, targetLayer, std::move(any)));  // indexChanged handler rebuilds the scene
        e->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
    if (e->button() == Qt::LeftButton) {
        commitDragIfMoved();
        clearGrabAnchor();
        // Drag is done; clear the "live snap active" indicator so the
        // selection outline returns to its normal yellow colour.
        liveSnapActive_ = false;
        refreshSelectionOverlay();
    }
}

void MapView::clearRulerPreview() {
    if (rulerPreviewShape_) {
        scene()->removeItem(rulerPreviewShape_);
        delete rulerPreviewShape_;
        rulerPreviewShape_ = nullptr;
    }
    if (rulerPreviewLabel_) {
        scene()->removeItem(rulerPreviewLabel_);
        delete rulerPreviewLabel_;
        rulerPreviewLabel_ = nullptr;
    }
}

void MapView::updateRulerPreview(QPointF endScene) {
    clearRulerPreview();
    const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
    // Apply the same grid snap the release-time commit will use, so the
    // preview line matches what the user actually gets on release.
    if (snapStepStuds_ > 0.0) {
        const double sPx = snapStepStuds_ * pxPerStud;
        endScene = QPointF(std::round(endScene.x() / sPx) * sPx,
                           std::round(endScene.y() / sPx) * sPx);
    }
    const QPointF d = endScene - rulerStart_;
    const double lenScene = std::hypot(d.x(), d.y());
    const double lenStuds = lenScene / pxPerStud;
    const double lenMm    = lenStuds * 8.0;

    if (tool_ == Tool::DrawLinearRuler) {
        auto* line = new QGraphicsLineItem(QLineF(rulerStart_, endScene));
        QPen pen(QColor(20, 20, 20, 200));
        pen.setWidthF(1.5); pen.setCosmetic(true);
        line->setPen(pen);
        line->setZValue(1e8);
        scene()->addItem(line);
        rulerPreviewShape_ = line;
    } else if (tool_ == Tool::DrawCircularRuler) {
        auto* ell = new QGraphicsEllipseItem(QRectF(
            rulerStart_.x() - lenScene, rulerStart_.y() - lenScene,
            lenScene * 2.0, lenScene * 2.0));
        QPen pen(QColor(20, 20, 20, 200));
        pen.setWidthF(1.5); pen.setCosmetic(true);
        ell->setPen(pen);
        ell->setBrush(Qt::NoBrush);
        ell->setZValue(1e8);
        scene()->addItem(ell);
        rulerPreviewShape_ = ell;
    }

    QString unit;
    if (lenMm >= 1000) unit = QStringLiteral("%1 m").arg(lenMm / 1000.0, 0, 'f', 2);
    else               unit = QStringLiteral("%1 mm").arg(lenMm, 0, 'f', 0);
    const QString text = (tool_ == Tool::DrawLinearRuler)
        ? tr("%1 studs (%2)").arg(lenStuds, 0, 'f', 1).arg(unit)
        : tr("r=%1 studs (%2)").arg(lenStuds, 0, 'f', 1).arg(unit);

    rulerPreviewLabel_ = new QGraphicsSimpleTextItem(text);
    QFont f = rulerPreviewLabel_->font();
    f.setPointSizeF(std::max(8.0, f.pointSizeF()));
    rulerPreviewLabel_->setFont(f);
    rulerPreviewLabel_->setBrush(QColor(20, 20, 20));
    rulerPreviewLabel_->setZValue(1e8 + 1);
    rulerPreviewLabel_->setPos(endScene + QPointF(12.0, 12.0));
    scene()->addItem(rulerPreviewLabel_);
}

void MapView::updateVenueDrawPreview(QPointF /*hoverScenePos*/) {
    // Draw / refresh the in-progress polygon as a dashed outline so the
    // user sees what they're building. Persistent scene item; replaced on
    // every click.
    if (venueDrawPreview_) {
        scene()->removeItem(venueDrawPreview_);
        delete venueDrawPreview_;
        venueDrawPreview_ = nullptr;
    }
    if (venueDrawPoints_.isEmpty()) return;
    const double px = rendering::SceneBuilder::kPixelsPerStud;
    QPainterPath path;
    path.moveTo(venueDrawPoints_.first() * px);
    for (int i = 1; i < venueDrawPoints_.size(); ++i) path.lineTo(venueDrawPoints_[i] * px);
    venueDrawPreview_ = new QGraphicsPathItem(path);
    QPen pen(QColor(230, 40, 40));
    pen.setWidthF(2.0); pen.setCosmetic(true); pen.setStyle(Qt::DashLine);
    venueDrawPreview_->setPen(pen);
    venueDrawPreview_->setZValue(1e8);
    scene()->addItem(venueDrawPreview_);
}

void MapView::finishVenueDraw() {
    if (!map_) { venueDrawPoints_.clear(); return; }
    if (venueDrawPoints_.size() < 3) {
        venueDrawPoints_.clear();
        updateVenueDrawPreview();
        if (auto* mw = window())
            if (auto* sb = mw->findChild<QStatusBar*>())
                sb->showMessage(tr("Venue polygon needs at least 3 points"), 2500);
        return;
    }

    core::Venue v = map_->sidecar.venue.value_or(core::Venue{});
    v.enabled = true;

    if (tool_ == Tool::DrawVenueOutline) {
        // Replace outline: one VenueEdge per polygon side (closed loop).
        v.edges.clear();
        for (int i = 0; i < venueDrawPoints_.size(); ++i) {
            const QPointF a = venueDrawPoints_[i];
            const QPointF b = venueDrawPoints_[(i + 1) % venueDrawPoints_.size()];
            core::VenueEdge e;
            e.polyline = { a, b };
            e.kind = core::EdgeKind::Wall;
            v.edges.push_back(e);
        }
    } else {   // DrawVenueObstacle
        core::VenueObstacle ob;
        ob.polygon = venueDrawPoints_;
        v.obstacles.push_back(ob);
    }

    undoStack_->push(new edit::SetVenueCommand(*map_, std::make_optional(v)));
    venueDrawPoints_.clear();
    updateVenueDrawPreview();

    // Drop back to Select so normal editing resumes after a polygon lands.
    tool_ = Tool::Select;

    if (auto* mw = window())
        if (auto* sb = mw->findChild<QStatusBar*>())
            sb->showMessage(tr("Venue updated"), 2000);
}

void MapView::keyPressEvent(QKeyEvent* e) {
    if (!map_) { QGraphicsView::keyPressEvent(e); return; }
    // Venue-draw commit/cancel keys.
    if (tool_ == Tool::DrawVenueOutline || tool_ == Tool::DrawVenueObstacle) {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            finishVenueDraw();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Escape) {
            venueDrawPoints_.clear();
            updateVenueDrawPreview();
            tool_ = Tool::Select;
            e->accept();
            return;
        }
    }
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        deleteSelected();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_R) {
        // By the toolbar's turn step (90° unless changed), as the web does.
        const auto step = static_cast<float>(rotationStepDegrees());
        rotateSelected(e->modifiers().testFlag(Qt::ShiftModifier) ? -step : step);
        e->accept();
        return;
    }
    // Arrow keys: nudge selection by the current snap step (1 stud default).
    if (e->key() == Qt::Key_Left  || e->key() == Qt::Key_Right ||
        e->key() == Qt::Key_Up    || e->key() == Qt::Key_Down) {
        const double step = snapStepStuds_ > 0.0 ? snapStepStuds_ : 1.0;
        double dx = 0.0, dy = 0.0;
        if (e->key() == Qt::Key_Left)  dx = -step;
        if (e->key() == Qt::Key_Right) dx =  step;
        if (e->key() == Qt::Key_Up)    dy = -step;
        if (e->key() == Qt::Key_Down)  dy =  step;
        nudgeSelected(dx, dy);
        e->accept();
        return;
    }
    QGraphicsView::keyPressEvent(e);
}

void MapView::nudgeSelected(double dxStuds, double dyStuds) {
    if (!map_ || (dxStuds == 0.0 && dyStuds == 0.0)) return;
    const QPointF delta(dxStuds, dyStuds);
    std::vector<edit::MoveBricksCommand::Entry> brickEntries;

    // Nudge also moves selected rulers + labels. Collect the ruler guids
    // we've already handled so nudging a multi-piece ruler (seg1+seg2+
    // label) only fires one command.
    QSet<QString> rulerSeen;
    QSet<QString> labelSeen;
    struct RulerHit { int li; QString guid; };
    std::vector<RulerHit> rulerHits;
    QStringList labelIds;

    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (isBrickItem(it)) {
            const int li = it->data(kBrickDataLayerIndex).toInt();
            const QString guid = it->data(kBrickDataGuid).toString();
            if (li < 0 || li >= static_cast<int>(map_->layers().size())) continue;
            auto* L = map_->layers()[li].get();
            if (!L || L->kind() != core::LayerKind::Brick) continue;
            for (const auto& b : static_cast<core::LayerBrick&>(*L).bricks) {
                if (b.guid != guid) continue;
                edit::MoveBricksCommand::Entry e;
                e.ref.layerIndex = li;
                e.ref.guid = guid;
                e.beforeTopLeft = b.displayArea.topLeft();
                e.afterTopLeft  = b.displayArea.topLeft() + delta;
                brickEntries.push_back(e);
                break;
            }
        } else if (isRulerItem(it)) {
            const QString guid = it->data(kBrickDataGuid).toString();
            if (guid.isEmpty() || rulerSeen.contains(guid)) continue;
            rulerSeen.insert(guid);
            rulerHits.push_back({ it->data(kBrickDataLayerIndex).toInt(), guid });
        } else if (isLabelItem(it)) {
            const QString guid = it->data(kBrickDataGuid).toString();
            if (guid.isEmpty() || labelSeen.contains(guid)) continue;
            labelSeen.insert(guid);
            labelIds.append(guid);
        }
    }
    if (brickEntries.empty() && rulerHits.empty() && labelIds.isEmpty()) return;

    undoStack_->beginMacro(tr("Move selection"));
    if (!brickEntries.empty()) {
        undoStack_->push(new edit::MoveBricksCommand(*map_, std::move(brickEntries)));
    }
    for (const auto& h : rulerHits) {
        undoStack_->push(new edit::MoveRulerItemCommand(*map_, h.li, h.guid, delta));
    }
    for (const QString& id : labelIds) {
        undoStack_->push(new edit::MoveAnchoredLabelCommand(*map_, id, delta));
    }
    undoStack_->endMacro();
}

void MapView::rotateSelected(float degrees) {
    if (!map_) return;
    // Every selected brick turns around the selection's pivot: for a single
    // brick that's its own sprite centre (BlueBrick's pivot), so it turns
    // in place. The displayArea follows the rotated hull.
    struct Hit { int li; const core::Brick* brick; QPointF centre; };
    std::vector<Hit> hits;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!isBrickItem(it)) continue;
        const int li = it->data(kBrickDataLayerIndex).toInt();
        const QString guid = it->data(kBrickDataGuid).toString();
        if (li < 0 || li >= static_cast<int>(map_->layers().size())) continue;
        auto* L = map_->layers()[li].get();
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<core::LayerBrick&>(*L).bricks) {
            if (b.guid != guid) continue;
            hits.push_back({ li, &b, parts::placement::imageCentre(b, parts_) });
            break;
        }
    }
    if (hits.empty()) return;

    QPointF pivot(0, 0);
    for (const auto& h : hits) pivot += h.centre;
    pivot /= hits.size();

    std::vector<edit::RotateBricksCommand::Entry> rotates;
    for (const auto& h : hits) {
        core::Brick turned = *h.brick;
        turned.orientation = h.brick->orientation + degrees;
        parts::placement::placeByImageCentre(
            turned, pivot + parts::placement::rotated(h.centre - pivot, degrees), parts_);
        edit::RotateBricksCommand::Entry r;
        r.ref.layerIndex = h.li;
        r.ref.guid = h.brick->guid;
        r.beforeOrientation = h.brick->orientation;
        r.afterOrientation  = turned.orientation;
        r.beforeArea = h.brick->displayArea;
        r.afterArea  = turned.displayArea;
        rotates.push_back(r);
    }
    undoStack_->push(new edit::RotateBricksCommand(*map_, std::move(rotates)));
    // Selection is preserved automatically by the undoStack indexChanged
    // handler, which rebuilds the scene + reselects every item by guid.
}

void MapView::addPartAtViewCenter(const QString& partKey) {
    if (!map_) return;
    addPartAtScenePos(partKey, mapToScene(viewport()->rect().center()));
}

void MapView::resolvePartPlacement(const QString& partKey, QPointF cursorScenePx,
                                   QPointF* outCentreStuds, float* outOrientation,
                                   bool* outSnapped,
                                   QPointF* outSnapPointScenePx) const {
    const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
    QPointF centreStuds(cursorScenePx.x() / pxPerStud, cursorScenePx.y() / pxPerStud);
    float   orientation = 0.0f;
    bool    snapped = false;
    QPointF snapPoint = cursorScenePx;

    if (!map_) {
        if (outCentreStuds) *outCentreStuds = centreStuds;
        if (outOrientation) *outOrientation = orientation;
        if (outSnapped)     *outSnapped = false;
        if (outSnapPointScenePx) *outSnapPointScenePx = snapPoint;
        return;
    }

    QPixmap pm = parts_.pixmap(partKey);
    auto newMeta = parts_.metadata(partKey);
    // Some imports author at higher DPI than the scene's 8 px/stud.
    // Use the part's authored pxPerStud (recorded in <PixelsPerStud>)
    // to convert pixmap pixels back to studs — otherwise a 16 px/stud
    // sprite renders at twice the right size on the map. Guard against
    // a missing/zero value so we never divide by zero.
    const double partPxPerStud = (newMeta && newMeta->pxPerStud > 0)
        ? newMeta->pxPerStud : 8.0;
    const double widthStuds  = pm.isNull() ? 2.0 : pm.width()  / partPxPerStud;
    const double heightStuds = pm.isNull() ? 2.0 : pm.height() / partPxPerStud;

    // Selected-brick anchor: if exactly one brick is selected with a free
    // compatible connection, lock the new piece to that connection.
    bool anchoredToSelection = false;
    if (newMeta && newMeta->kind != parts::PartKind::Group) {
        const core::Brick* anchor = nullptr;
        int selectedBricks = 0;
        for (QGraphicsItem* it : scene()->selectedItems()) {
            if (!isBrickItem(it)) continue;
            ++selectedBricks;
            if (selectedBricks > 1) { anchor = nullptr; break; }
            const int li = it->data(kBrickDataLayerIndex).toInt();
            const QString g = it->data(kBrickDataGuid).toString();
            if (li < 0 || li >= static_cast<int>(map_->layers().size())) continue;
            auto* L = map_->layers()[li].get();
            if (!L || L->kind() != core::LayerKind::Brick) continue;
            const auto& BL = static_cast<const core::LayerBrick&>(*L);
            for (const auto& bb : BL.bricks) {
                if (bb.guid == g) { anchor = &bb; break; }
            }
        }
        if (anchor) {
            auto anchorMeta = parts_.metadata(anchor->partNumber);
            if (anchorMeta) {
                const QPointF anchorCenter = parts::placement::imageCentre(*anchor, parts_);
                const double rA = anchor->orientation * M_PI / 180.0;
                const double caA = std::cos(rA), saA = std::sin(rA);
                for (int i = 0; i < anchorMeta->connections.size(); ++i) {
                    const auto& ac = anchorMeta->connections[i];
                    if (ac.type.isEmpty()) continue;
                    if (i < static_cast<int>(anchor->connections.size()) &&
                        !anchor->connections[i].linkedToId.isEmpty()) continue;
                    int newCi = -1;
                    for (int j = 0; j < newMeta->connections.size(); ++j) {
                        if (newMeta->connections[j].type == ac.type) { newCi = j; break; }
                    }
                    if (newCi < 0) continue;
                    const auto& nc = newMeta->connections[newCi];
                    const QPointF acWorld(
                        anchorCenter.x() + ac.position.x() * caA - ac.position.y() * saA,
                        anchorCenter.y() + ac.position.x() * saA + ac.position.y() * caA);
                    const double targetAngle = ac.angleDegrees + anchor->orientation;
                    double newOrient = targetAngle + 180.0 - nc.angleDegrees;
                    newOrient = std::remainder(newOrient, 360.0);  // (-180, 180], no loop on absurd angles
                    if (newOrient <= -180.0) newOrient += 360.0;
                    const double rN = newOrient * M_PI / 180.0;
                    const double caN = std::cos(rN), saN = std::sin(rN);
                    centreStuds.setX(acWorld.x() - (nc.position.x() * caN - nc.position.y() * saN));
                    centreStuds.setY(acWorld.y() - (nc.position.x() * saN + nc.position.y() * caN));
                    orientation = static_cast<float>(newOrient);
                    anchoredToSelection = true;
                    snapped = true;
                    snapPoint = QPointF(acWorld.x() * pxPerStud, acWorld.y() * pxPerStud);
                    break;
                }
            }
        }
    }

    // Cursor-proximity connection snap: only when not already anchored.
    const double threshold = connectionSnapThresholdStuds();
    if (!anchoredToSelection) {
        auto snap = newPartPlacementSnap(*map_, parts_, partKey,
                                         centreStuds, orientation, threshold);
        if (snap.applied) {
            QPointF newCenter = centreStuds;
            if (snap.newOrientation) {
                newCenter += snap.rotationAlignedTranslationStuds;
                orientation = *snap.newOrientation;
            } else {
                newCenter += snap.translationStuds;
            }
            // Snap point = the new part's active connection world position
            // after the snap shift. Locate which new-part connection picked
            // up the snap by re-running the alignment formula.
            if (newMeta) {
                const double rN = orientation * M_PI / 180.0;
                const double caN = std::cos(rN), saN = std::sin(rN);
                // Find the new-part connection whose post-snap world pos is
                // closest to where the snap landed (= the saved targetWorld).
                // Easier: the active conn ends up where target was — and the
                // shift was target - activeWorld, so newCenter + rotated(c.pos)
                // == targetWorld. Use the first compatible connection which
                // is the one newPartPlacementSnap iterates.
                for (int i = 0; i < newMeta->connections.size(); ++i) {
                    const auto& c = newMeta->connections[i];
                    if (c.type.isEmpty()) continue;
                    const QPointF connWorld(
                        newCenter.x() + c.position.x() * caN - c.position.y() * saN,
                        newCenter.y() + c.position.x() * saN + c.position.y() * caN);
                    snapPoint = QPointF(connWorld.x() * pxPerStud,
                                        connWorld.y() * pxPerStud);
                    break;
                }
            }
            centreStuds = newCenter;
            snapped = true;
        } else if (snapStepStuds_ > 0.0) {
            // Snap the displayArea's corner, as for placed bricks.
            core::Brick probe;
            probe.partNumber = partKey;
            probe.orientation = orientation;
            probe.displayArea = QRectF(0, 0, widthStuds, heightStuds);
            parts::placement::placeByImageCentre(probe, centreStuds, parts_);
            QPointF topLeft = probe.displayArea.topLeft();
            topLeft.setX(std::round(topLeft.x() / snapStepStuds_) * snapStepStuds_);
            topLeft.setY(std::round(topLeft.y() / snapStepStuds_) * snapStepStuds_);
            probe.displayArea.moveTopLeft(topLeft);
            centreStuds = parts::placement::imageCentre(probe, parts_);
        }
    }

    if (outCentreStuds) *outCentreStuds = centreStuds;
    if (outOrientation) *outOrientation = orientation;
    if (outSnapped)     *outSnapped = snapped;
    if (outSnapPointScenePx) *outSnapPointScenePx = snapPoint;
}

void MapView::addPartAtScenePos(const QString& partKey, QPointF sceneCenterPx) {
    if (!map_) return;
    if (!budgetAllows(partKey)) return;

    // Use the active (selected) layer if it's a brick layer — matches
    // BlueBrick's selectedLayerIndex-driven placement. Fall back to the
    // first brick layer if the active one is a different type.
    int targetLayer = -1;
    if (map_->selectedLayerIndex >= 0 &&
        map_->selectedLayerIndex < static_cast<int>(map_->layers().size()) &&
        map_->layers()[map_->selectedLayerIndex]->kind() == core::LayerKind::Brick) {
        targetLayer = map_->selectedLayerIndex;
    } else {
        for (int i = 0; i < static_cast<int>(map_->layers().size()); ++i) {
            if (map_->layers()[i]->kind() == core::LayerKind::Brick) {
                targetLayer = i;
                break;
            }
        }
    }
    if (targetLayer < 0) return;

    // Sets (PartKind::Group): the XML enumerates SubPartList children.
    // Expand the set into one brick per subpart placed at
    //   setCentre + rotatePoint(subpart.localPos, 0) in stud coords
    // and automatically wrap the new bricks in a sidecar Module so the
    // user can move the whole thing as a unit. All under a single undo
    // macro.
    {
        const auto meta = parts_.metadata(partKey);
        if (meta && meta->kind == parts::PartKind::Group && !meta->subparts.isEmpty()) {
            const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
            const QPointF centreStuds(sceneCenterPx.x() / pxPerStud,
                                        sceneCenterPx.y() / pxPerStud);
            undoStack_->beginMacro(tr("Place set %1").arg(partKey));
            std::vector<edit::CreateModuleCommand::Member> members;
            for (const auto& sp : meta->subparts) {
                // As BlueBrick's Group constructor: sp.position is the
                // subpart's displayArea centre (Brick.Center) in set-local
                // studs; parts with an XML hull draw their sprite off it.
                double orientDeg = sp.angleDegrees;
                orientDeg = std::fmod(orientDeg, 360.0);
                if (orientDeg >  180.0) orientDeg -= 360.0;
                if (orientDeg <= -180.0) orientDeg += 360.0;
                core::Brick b;
                b.guid = core::newBbmId();
                b.partNumber = sp.subKey;
                b.orientation = static_cast<float>(orientDeg);
                parts::placement::placeByAreaCentre(b, centreStuds + sp.position, parts_);
                members.push_back({ targetLayer, b.guid });
                undoStack_->push(new edit::AddBrickCommand(*map_, targetLayer, std::move(b)));
            }
            // Name the module after the set's description, falling back
            // to the set key. Creates the module as a single undoable
            // step so Ctrl+Z unwinds the whole placement.
            QString setName = partKey;
            for (const auto& d : meta->descriptions) {
                if (d.language == QStringLiteral("en")) { setName = d.text; break; }
            }
            if (!members.empty()) {
                undoStack_->push(new edit::CreateModuleCommand(
                    *map_, setName, std::move(members)));
            }
            undoStack_->endMacro();
            // Set files declare positions + angles but NOT connectivity.
            // Without this, every subpart has empty linkedToId, so the
            // tracks look "not connected" even though they're positioned
            // to abut one another. Rebuild from world positions so the
            // connection dots light up and snapping recognises the
            // already-joined track network.
            edit::rebuildConnectivity(*map_, parts_);
            rebuildScene();
            if (auto* mw = window())
                if (auto* sb = mw->findChild<QStatusBar*>())
                    sb->showMessage(tr("Placed set: %1 (%2 parts)")
                        .arg(setName).arg(meta->subparts.size()), 3000);
            return;
        }
    }

    QPointF centreStuds;
    float   orientation = 0.0f;
    bool    connectionSnapped = false;
    resolvePartPlacement(partKey, sceneCenterPx,
                         &centreStuds, &orientation, &connectionSnapped, nullptr);

    core::Brick b;
    b.guid = core::newBbmId();
    b.partNumber = partKey;
    b.orientation = orientation;
    parts::placement::placeByImageCentre(b, centreStuds, parts_);

    const QString newGuid = b.guid;
    undoStack_->push(new edit::AddBrickCommand(*map_, targetLayer, std::move(b)));  // indexChanged handler rebuilds the scene

    // Select the newly-placed brick so the user can immediately chain
    // another connected placement: the next click-place uses it as the
    // snap source. Clear prior selection so only the new piece is the
    // "current" one.
    scene()->clearSelection();
    for (QGraphicsItem* it : scene()->items()) {
        if (!isBrickItem(it)) continue;
        if (it->data(kBrickDataLayerIndex).toInt() != targetLayer) continue;
        if (it->data(kBrickDataGuid).toString() != newGuid) continue;
        it->setSelected(true);
        break;
    }

    if (auto* mw = window())
        if (auto* sb = mw->findChild<QStatusBar*>())
            sb->showMessage(
                connectionSnapped
                    ? tr("Connection snap: %1").arg(partKey)
                    : tr("Placed: %1").arg(partKey), 2000);
}


void MapView::setSnapStepStuds(double studs) {
    snapStepStuds_ = studs;
    // Propagate to the rendering side so item-level ItemPositionChange snaps
    // bricks live while dragging (in addition to commit-time snap on release).
    rendering::SceneBuilder::setLiveSnapStepStuds(studs);
}

void MapView::selectAll() {
    for (QGraphicsItem* it : scene()->items()) {
        if (isBrickItem(it)) it->setSelected(true);
    }
}

void MapView::deselectAll() {
    scene()->clearSelection();
}

void MapView::bringSelectionToFront() {
    if (!map_) return;
    std::vector<edit::ReorderBricksCommand::Target> targets;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!isBrickItem(it)) continue;
        targets.push_back({ it->data(kBrickDataLayerIndex).toInt(),
                            it->data(kBrickDataGuid).toString() });
    }
    if (targets.empty()) return;
    undoStack_->push(new edit::ReorderBricksCommand(
        *map_, std::move(targets), edit::ReorderBricksCommand::ToFront));  // indexChanged handler rebuilds the scene
}

void MapView::sendSelectionToBack() {
    if (!map_) return;
    std::vector<edit::ReorderBricksCommand::Target> targets;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!isBrickItem(it)) continue;
        targets.push_back({ it->data(kBrickDataLayerIndex).toInt(),
                            it->data(kBrickDataGuid).toString() });
    }
    if (targets.empty()) return;
    undoStack_->push(new edit::ReorderBricksCommand(
        *map_, std::move(targets), edit::ReorderBricksCommand::ToBack));  // indexChanged handler rebuilds the scene
}

void MapView::groupSelection() {
    if (!map_) return;
    std::vector<edit::BrickRef> targets;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!isBrickItem(it)) continue;
        targets.push_back({ it->data(kBrickDataLayerIndex).toInt(),
                            it->data(kBrickDataGuid).toString() });
    }
    if (targets.size() < 2) return;   // nothing to group
    undoStack_->push(new edit::GroupBricksCommand(*map_, std::move(targets)));
}

void MapView::ungroupSelection() {
    if (!map_) return;
    std::vector<edit::BrickRef> targets;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!isBrickItem(it)) continue;
        targets.push_back({ it->data(kBrickDataLayerIndex).toInt(),
                            it->data(kBrickDataGuid).toString() });
    }
    if (targets.empty()) return;
    undoStack_->push(new edit::UngroupBricksCommand(*map_, std::move(targets)));
}

void MapView::selectPath() {
    if (!map_) return;
    // Build a guid -> (layerIndex, QGraphicsItem*, Brick*) index, plus the
    // starting frontier from the current selection.
    QHash<QString, QGraphicsItem*> itemByGuid;
    QHash<QString, core::Brick*>   brickByGuid;
    QSet<QString> toVisit;
    for (int li = 0; li < static_cast<int>(map_->layers().size()); ++li) {
        auto* L = map_->layers()[li].get();
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        for (auto& b : static_cast<core::LayerBrick&>(*L).bricks) {
            brickByGuid.insert(b.guid, &b);
        }
    }
    for (QGraphicsItem* it : scene()->items()) {
        if (!isBrickItem(it)) continue;
        const QString guid = it->data(kBrickDataGuid).toString();
        itemByGuid.insert(guid, it);
    }
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (!isBrickItem(it)) continue;
        toVisit.insert(it->data(kBrickDataGuid).toString());
    }
    if (toVisit.isEmpty()) return;

    // Transitive BFS over each brick's Connexion.linkedToId. The on-disk
    // .bbm format stores LinkedTo as the partner's *connection-point* guid,
    // but rebuildConnectivity (which runs on every undo-stack change and
    // on load) overwrites it with the partner's *brick* guid. Handle both
    // so the BFS finds neighbours regardless of which form is currently
    // in memory:
    //   * If linkedToId matches a brick guid directly, that's the partner.
    //   * Otherwise, look it up in the connection-guid reverse index.
    QHash<QString, QString> brickGuidForConnGuid;
    for (auto it = brickByGuid.constBegin(); it != brickByGuid.constEnd(); ++it) {
        for (const auto& c : it.value()->connections) {
            if (!c.guid.isEmpty()) brickGuidForConnGuid.insert(c.guid, it.key());
        }
    }

    QSet<QString> visited = toVisit;
    while (!toVisit.isEmpty()) {
        const QString cur = *toVisit.constBegin();
        toVisit.remove(cur);
        auto bit = brickByGuid.constFind(cur);
        if (bit == brickByGuid.constEnd()) continue;
        for (const auto& c : bit.value()->connections) {
            if (c.linkedToId.isEmpty()) continue;
            QString neighbourBrick;
            if (brickByGuid.contains(c.linkedToId)) {
                neighbourBrick = c.linkedToId;
            } else {
                neighbourBrick = brickGuidForConnGuid.value(c.linkedToId);
            }
            if (neighbourBrick.isEmpty() || visited.contains(neighbourBrick)) continue;
            visited.insert(neighbourBrick);
            toVisit.insert(neighbourBrick);
        }
    }
    for (const QString& guid : visited) {
        if (auto* it = itemByGuid.value(guid)) it->setSelected(true);
    }
}

void MapView::editSelectedTextContent() {
    if (!map_) return;
    QGraphicsItem* sel = nullptr;
    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (isTextItem(it)) { sel = it; break; }
    }
    if (!sel) return;
    const int li = sel->data(kBrickDataLayerIndex).toInt();
    const QString guid = sel->data(kBrickDataGuid).toString();
    if (li < 0 || li >= static_cast<int>(map_->layers().size())) return;
    auto* L = map_->layers()[li].get();
    if (!L || L->kind() != core::LayerKind::Text) return;
    auto& TL = static_cast<core::LayerText&>(*L);
    QString current;
    for (const auto& c : TL.textCells) if (c.guid == guid) { current = c.text; break; }
    bool ok = false;
    const QString next = QInputDialog::getMultiLineText(
        this, tr("Edit text"), tr("Label text:"), current, &ok);
    if (!ok || next == current) return;
    undoStack_->push(new edit::EditTextCellTextCommand(*map_, li, guid, next));  // indexChanged handler rebuilds the scene
}

void MapView::mouseDoubleClickEvent(QMouseEvent* e) {
    QGraphicsItem* under = itemAt(e->pos());
    // Connection dots and hull outlines are children of their brick.
    while (under && under->parentItem() && !isBrickItem(under)) under = under->parentItem();
    if (under) {
        if (e->button() == Qt::LeftButton && isBrickItem(under)
            && startFlexMove(under, mapToScene(e->pos()))) {
            e->accept();  // properties open on release if the mouse didn't move
            return;
        }
        scene()->clearSelection();
        under->setSelected(true);
        const int li = under->data(kBrickDataLayerIndex).toInt();
        const QString guid = under->data(kBrickDataGuid).toString();
        bool handled = false;
        if (isBrickItem(under)) {
            handled = editBrickDialog(this, *map_, li, guid, parts_, *undoStack_);
        } else if (isRulerItem(under)) {
            handled = editRulerDialog(this, *map_, li, guid, *undoStack_);
        } else if (isTextItem(under)) {
            handled = editTextDialog(this, *map_, li, guid, *undoStack_);
        } else if (isLabelItem(under)) {
            // Anchored labels: quick text edit via input dialog.
            QString current;
            for (const auto& lbl : map_->sidecar.anchoredLabels) {
                if (lbl.id == guid) { current = lbl.text; break; }
            }
            bool ok = false;
            const QString next = QInputDialog::getText(
                this, tr("Edit label"), tr("Label text:"),
                QLineEdit::Normal, current, &ok);
            if (ok && next != current) {
                undoStack_->push(new edit::EditAnchoredLabelTextCommand(
                    *map_, guid, next));
                handled = true;
            }
        } else if (isVenueItem(under)) {
            // Open the venue-properties dialog; commit any changes
            // through SetVenueCommand.
            VenueDialog dlg(map_->sidecar.venue, this);
            if (dlg.exec() == QDialog::Accepted) {
                undoStack_->push(new edit::SetVenueCommand(*map_,
                    dlg.cleared() ? std::nullopt : dlg.result()));
                handled = true;
            }
        }
        if (handled || isBrickItem(under) || isRulerItem(under)
            || isTextItem(under) || isLabelItem(under) || isVenueItem(under)) {
            e->accept();
            return;
        }
    }
    QGraphicsView::mouseDoubleClickEvent(e);
}

QPoint MapView::gridCellAt(QPointF scenePos) const {
    // LayerGrid.computeGridCoordFromStudCoord: truncate, one less below zero.
    const auto& grid = static_cast<const core::LayerGrid&>(*map_->layers()[gridLayer_]);
    const double size = std::max(1, grid.gridSizeInStud);
    const QPointF studs = scenePos / rendering::SceneBuilder::kPixelsPerStud;
    QPoint cell(static_cast<int>(studs.x() / size), static_cast<int>(studs.y() / size));
    if (studs.x() < 0) cell.rx() -= 1;
    if (studs.y() < 0) cell.ry() -= 1;
    return cell;
}

void MapView::setGridOrigin(QPoint corner) {
    if (!map_ || gridLayer_ < 0 || gridLayer_ >= static_cast<int>(map_->layers().size())) return;
    auto* L = map_->layers()[gridLayer_].get();
    if (!L || L->kind() != core::LayerKind::Grid) return;
    auto& grid = static_cast<core::LayerGrid&>(*L);
    if (grid.cellIndexCorner == corner) return;
    grid.cellIndexCorner = corner;
    viewport()->update();  // indices are painted with the background
}

bool MapView::budgetAllows(const QString& part, int quantity) {
    if (!budget_ || !map_ || budget_->canAdd(*map_, part, quantity)) return true;
    reportBudgetRefusal();
    return false;
}

void MapView::reportBudgetRefusal() {
    if (auto* mw = window())
        if (auto* sb = mw->findChild<QStatusBar*>())
            sb->showMessage(tr("Budget reached: part not added"), 3000);
    const QString key = QStringLiteral("general/warnBudgetLimitation");
    if (!QSettings().value(key, true).toBool()) return;
    QMessageBox box(QMessageBox::Critical, tr("Budget reached"),
                    tr("Cannot add this part because the budget is reached. If you want to add "
                       "this part, increase the budget for this part, disable the Budget "
                       "Limitation or close the budget file."),
                    QMessageBox::Ok, this);
    auto* dontShow = new QCheckBox(tr("Don't show this message again"), &box);
    box.setCheckBox(dontShow);
    box.exec();
    QSettings().setValue(key, !dontShow->isChecked());
}

bool MapView::startFlexMove(QGraphicsItem* under, QPointF scenePos) {
    if (!map_) return false;
    const int li = under->data(kBrickDataLayerIndex).toInt();
    if (li < 0 || li >= static_cast<int>(map_->layers().size())) return false;
    auto* L = map_->layers()[li].get();
    if (!L || L->kind() != core::LayerKind::Brick) return false;
    const QString grabbed = under->data(kBrickDataGuid).toString();
    // The chain is the current selection (made by the double-click's first
    // click, or before it), as in BlueBrick.
    QSet<QString> selection{ grabbed };
    for (QGraphicsItem* it : scene()->selectedItems())
        if (isBrickItem(it) && it->data(kBrickDataLayerIndex).toInt() == li)
            selection.insert(it->data(kBrickDataGuid).toString());
    if (pressSelection_.contains(grabbed)) selection |= pressSelection_;  // other layers' guids never match
    const double px = rendering::SceneBuilder::kPixelsPerStud;
    auto flex = edit::FlexMove::start(static_cast<core::LayerBrick&>(*L), selection, grabbed,
                                      scenePos / px, parts_);
    if (!flex) return false;

    flex_ = std::move(flex);
    flexLayer_ = li;
    flexGrabbed_ = grabbed;
    flexMoved_ = false;
    flexItems_.clear();
    QSet<QString> chain;
    for (const auto& s : flex_->initialState()) chain.insert(s.guid);
    for (QGraphicsItem* it : scene()->items()) {
        if (!isBrickItem(it) || it->data(kBrickDataLayerIndex).toInt() != li) continue;
        const QString guid = it->data(kBrickDataGuid).toString();
        if (chain.contains(guid)) flexItems_.insert(guid, it);
    }
    // Show which pieces bend.
    scene()->clearSelection();
    for (QGraphicsItem* it : std::as_const(flexItems_)) it->setSelected(true);
    return true;
}

void MapView::updateFlexItems() {
    if (!flex_) return;
    const double px = rendering::SceneBuilder::kPixelsPerStud;
    rendering::SceneBuilder::setSuppressItemSnap(true);
    for (const auto& s : flex_->currentState()) {
        QGraphicsItem* it = flexItems_.value(s.guid);
        if (!it) continue;
        core::Brick probe;
        probe.guid = s.guid;
        for (const auto& b : static_cast<core::LayerBrick&>(*map_->layers()[flexLayer_]).bricks)
            if (b.guid == s.guid) { probe = b; break; }
        it->setRotation(s.orientation);
        const QPointF offset = parts_.imageOffset(probe.partNumber, s.orientation) * px;
        it->setTransform(QTransform::fromTranslate(offset.x(), offset.y()));
        it->setPos(s.area.center() * px);
    }
    rendering::SceneBuilder::setSuppressItemSnap(false);
}

void MapView::finishFlexMove() {
    auto flex = std::move(flex_);
    flexItems_.clear();
    if (liveSnapActive_) { liveSnapActive_ = false; viewport()->update(); }
    if (!flexMoved_) {
        // A plain double-click: edit the brick, as BlueBrick does.
        flex->restore();
        editBrickDialog(this, *map_, flexLayer_, flexGrabbed_, parts_, *undoStack_);
        return;
    }
    const auto after = flex->currentState();
    flex->restore();
    std::vector<edit::RotateBricksCommand::Entry> entries;
    const auto& before = flex->initialState();
    for (size_t i = 0; i < before.size() && i < after.size(); ++i) {
        edit::RotateBricksCommand::Entry e;
        e.ref.layerIndex = flexLayer_;
        e.ref.guid = before[i].guid;
        e.beforeOrientation = before[i].orientation;
        e.beforeArea = before[i].area;
        e.afterOrientation = after[i].orientation;
        e.afterArea = after[i].area;
        entries.push_back(e);
    }
    auto* cmd = new edit::RotateBricksCommand(*map_, std::move(entries));
    cmd->setText(tr("Flex move"));
    undoStack_->push(cmd);  // indexChanged handler relinks and rebuilds the scene
}

void MapView::addTextAtViewCenter(const QString& text) {
    if (!map_) return;
    addTextAtScenePos(text, mapToScene(viewport()->rect().center()));
}

void MapView::addTextAtScenePos(const QString& text, QPointF sceneCenterPx) {
    if (!map_ || text.isEmpty()) return;
    // Pick the first text layer; create one if none exists so calling this on
    // a layout that doesn't have a text layer still works.
    int targetLayer = -1;
    for (int i = 0; i < static_cast<int>(map_->layers().size()); ++i) {
        if (map_->layers()[i]->kind() == core::LayerKind::Text) { targetLayer = i; break; }
    }
    if (targetLayer < 0) {
        auto L = std::make_unique<core::LayerText>();
        L->guid = core::newBbmId();
        L->name = tr("Labels");
        map_->layers().push_back(std::move(L));
        targetLayer = static_cast<int>(map_->layers().size()) - 1;
    }

    const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
    // Default label box: sized so the fit-to-box renderer shows the text at
    // a readable height. Width scales with character count, height is a
    // fixed line height in studs.
    const double heightStuds = 10.0;
    const double widthStuds  = std::max(heightStuds * 0.6 * text.size(), heightStuds * 2.0);

    core::TextCell c;
    c.guid = core::newBbmId();
    c.text = text;
    c.orientation = 0.0f;
    c.fontColor = core::ColorSpec::fromKnown(QColor(Qt::black), QStringLiteral("Black"));
    c.font.familyName = QStringLiteral("Arial");
    c.font.sizePt = 12.0f;
    c.font.styleString = QStringLiteral("Regular");
    c.alignment = core::TextAlignment::Center;
    c.displayArea = QRectF(sceneCenterPx.x() / pxPerStud - widthStuds / 2.0,
                            sceneCenterPx.y() / pxPerStud - heightStuds / 2.0,
                            widthStuds, heightStuds);
    undoStack_->push(new edit::AddTextCellCommand(*map_, targetLayer, std::move(c)));  // indexChanged handler rebuilds the scene
}

void MapView::showDropTargetHint() {
    if (!map_) return;
    auto* sb = window() ? window()->findChild<QStatusBar*>() : nullptr;
    if (!sb) return;

    // Mirror addPartAtScenePos: active layer if brick, else first brick layer.
    int target = -1;
    const auto& layers = map_->layers();
    if (map_->selectedLayerIndex >= 0 &&
        map_->selectedLayerIndex < static_cast<int>(layers.size()) &&
        layers[map_->selectedLayerIndex]->kind() == core::LayerKind::Brick) {
        target = map_->selectedLayerIndex;
    } else {
        for (int i = 0; i < static_cast<int>(layers.size()); ++i) {
            if (layers[i]->kind() == core::LayerKind::Brick) { target = i; break; }
        }
    }

    if (target < 0) {
        sb->showMessage(tr("No brick layer — add one before dropping parts"), 0);
        return;
    }

    const QString name = layers[target]->name;
    const bool isActive = (target == map_->selectedLayerIndex);
    const QString msg = isActive
        ? tr("Drop onto: %1 (active layer)").arg(name)
        : tr("Drop onto: %1 (active layer is not a brick layer)").arg(name);
    sb->showMessage(msg, 0);
}

void MapView::clearDropTargetHint() {
    if (auto* sb = window() ? window()->findChild<QStatusBar*>() : nullptr)
        sb->clearMessage();
}

void MapView::dragEnterEvent(QDragEnterEvent* e) {
    const QString partMime   = QString::fromLatin1(PartsBrowser::kPartMimeType);
    const QString moduleMime = QString::fromLatin1(kModuleDragMimeType);
    const QPointF scenePos = mapToScene(e->position().toPoint());
    if (e->mimeData()->hasFormat(partMime)) {
        const QString key = QString::fromUtf8(e->mimeData()->data(partMime));
        updateDragPreview(key, scenePos);
        showDropTargetHint();
        e->acceptProposedAction();
        return;
    }
    if (e->mimeData()->hasFormat(moduleMime)) {
        const QString path = QString::fromUtf8(e->mimeData()->data(moduleMime));
        updateModuleDragPreview(path, scenePos);
        showDropTargetHint();
        e->acceptProposedAction();
        return;
    }
    QGraphicsView::dragEnterEvent(e);
}

void MapView::dragMoveEvent(QDragMoveEvent* e) {
    const QString partMime   = QString::fromLatin1(PartsBrowser::kPartMimeType);
    const QString moduleMime = QString::fromLatin1(kModuleDragMimeType);
    const QPointF scenePos = mapToScene(e->position().toPoint());
    if (e->mimeData()->hasFormat(partMime)) {
        const QString key = QString::fromUtf8(e->mimeData()->data(partMime));
        updateDragPreview(key, scenePos);
        e->acceptProposedAction();
        return;
    }
    if (e->mimeData()->hasFormat(moduleMime)) {
        const QString path = QString::fromUtf8(e->mimeData()->data(moduleMime));
        updateModuleDragPreview(path, scenePos);
        e->acceptProposedAction();
        return;
    }
    QGraphicsView::dragMoveEvent(e);
}

void MapView::dragLeaveEvent(QDragLeaveEvent* e) {
    clearDragPreview();
    clearDropTargetHint();
    QGraphicsView::dragLeaveEvent(e);
}

void MapView::clearDragPreview() {
    if (dragPreviewItem_) {
        scene()->removeItem(dragPreviewItem_);
        delete dragPreviewItem_;
        dragPreviewItem_ = nullptr;
    }
    dragPreviewKey_.clear();
    if (liveSnapActive_) {
        liveSnapActive_ = false;
        viewport()->update();
    }
}

void MapView::updateDragPreview(const QString& partKey, QPointF cursorScenePx) {
    if (!map_ || partKey.isEmpty()) { clearDragPreview(); return; }

    // Lazy-build the ghost pixmap item the first time we see this key.
    // Re-use the same item when the user drags continuously over the
    // viewport — recreating it every frame would flicker.
    const QString cacheKey = QStringLiteral("part:") + partKey;
    if (!dragPreviewItem_ || dragPreviewKey_ != cacheKey) {
        clearDragPreview();
        QPixmap pm = parts_.pixmap(partKey);
        if (pm.isNull()) return;
        dragPreviewItem_ = new QGraphicsPixmapItem(pm);
        // Rotate around the pixmap centre so part orientations match
        // SceneBuilder's brick rendering.
        dragPreviewItem_->setOffset(-pm.width() / 2.0, -pm.height() / 2.0);
        dragPreviewItem_->setTransformOriginPoint(0.0, 0.0);
        dragPreviewItem_->setOpacity(0.55);
        // Sit above all bricks but below the SelectionOverlay (which uses
        // an even higher z so the snap ring stays on top of the ghost).
        dragPreviewItem_->setZValue(1e8);
        dragPreviewItem_->setTransformationMode(Qt::SmoothTransformation);
        scene()->addItem(dragPreviewItem_);
        dragPreviewKey_ = cacheKey;
    }

    QPointF centreStuds;
    float   orientation = 0.0f;
    bool    snapped = false;
    QPointF snapPointScene;
    resolvePartPlacement(partKey, cursorScenePx,
                         &centreStuds, &orientation, &snapped, &snapPointScene);

    const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
    dragPreviewItem_->setRotation(orientation);
    dragPreviewItem_->setPos(centreStuds.x() * pxPerStud,
                              centreStuds.y() * pxPerStud);

    // Pipe through the existing live-snap overlay so the user gets the
    // green ring at the connection point exactly like during a brick drag.
    if (snapped) {
        liveSnapActive_ = true;
        liveSnapPointScene_ = snapPointScene;
    } else if (liveSnapActive_) {
        liveSnapActive_ = false;
    }
    viewport()->update();
}

void MapView::updateModuleDragPreview(const QString& bbmPath, QPointF cursorScenePx) {
    if (!map_ || bbmPath.isEmpty()) { clearDragPreview(); return; }

    const QString cacheKey = QStringLiteral("module:") + bbmPath;
    if (!dragPreviewItem_ || dragPreviewKey_ != cacheKey) {
        // First time we see this module in the current drag — parse the
        // .bbm and rasterize it into an offscreen scene so we can show a
        // ghost without re-doing the work every move event. Modules can
        // be expensive (lots of bricks); cache aggressively.
        clearDragPreview();
        auto res = saveload::readBbm(bbmPath);
        if (!res.ok() || !res.map) return;
        parts::placement::fixStaleAreas(*res.map, parts_);

        const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;

        // Walk the bricks once to compute the centroid AND the bounding-
        // box top-left, both in studs. Top-left drives grid-snap so the
        // module's edge (not its centroid) lands on a stud-aligned grid
        // line — matches how a single-brick drop snaps its top-left.
        QPointF centroidStuds; int count = 0;
        QRectF bboxPx;
        QRectF bboxStuds;
        for (const auto& L : res.map->layers()) {
            if (!L || L->kind() != core::LayerKind::Brick) continue;
            for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks) {
                centroidStuds += b.displayArea.center();
                ++count;
                const QRectF bp(b.displayArea.x() * pxPerStud,
                                 b.displayArea.y() * pxPerStud,
                                 b.displayArea.width()  * pxPerStud,
                                 b.displayArea.height() * pxPerStud);
                bboxPx = bboxPx.isNull() ? bp : bboxPx.united(bp);
                bboxStuds = bboxStuds.isNull() ? b.displayArea
                                                : bboxStuds.united(b.displayArea);
            }
        }
        if (count == 0 || bboxPx.isEmpty()) return;
        centroidStuds /= count;
        // Vector from centroid to bbox top-left (negative numbers).
        dragPreviewModuleTopLeftOffsetStuds_ = bboxStuds.topLeft() - centroidStuds;

        // Render the module via a temporary scene + SceneBuilder so we
        // pick up the same pixmaps + transforms users already see. Then
        // rasterize to a QImage we can wrap in a QGraphicsPixmapItem.
        QGraphicsScene tmpScene;
        tmpScene.setBackgroundBrush(Qt::transparent);
        rendering::SceneBuilder builder(tmpScene, parts_);
        builder.build(*res.map);
        const QRectF source = bboxPx.adjusted(-2, -2, 2, 2);
        QImage img(static_cast<int>(std::ceil(source.width())),
                   static_cast<int>(std::ceil(source.height())),
                   QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            tmpScene.render(&p, QRectF(0, 0, img.width(), img.height()), source,
                            Qt::KeepAspectRatio);
        }

        QPixmap pm = QPixmap::fromImage(std::move(img));
        if (pm.isNull()) return;
        dragPreviewItem_ = new QGraphicsPixmapItem(pm);
        // Centre the ghost on the centroid so dragMove can simply set
        // pos = cursor (centroid stays under the cursor as the user
        // drags). The offset is centroid minus the source rect's top-left.
        const QPointF centroidPx(centroidStuds.x() * pxPerStud,
                                 centroidStuds.y() * pxPerStud);
        const QPointF offsetFromCentroid = source.topLeft() - centroidPx;
        dragPreviewItem_->setOffset(offsetFromCentroid);
        dragPreviewItem_->setOpacity(0.55);
        dragPreviewItem_->setZValue(1e8);
        dragPreviewItem_->setTransformationMode(Qt::SmoothTransformation);
        scene()->addItem(dragPreviewItem_);
        dragPreviewKey_ = cacheKey;
        dragPreviewModuleCentroidScenePx_ = centroidPx;
    }

    // Modules drop centroid-at-cursor with no rotation. When grid snap
    // is active, snap the bbox TOP-LEFT to the grid (matches how single-
    // brick drops snap their top-left) and back-derive the centroid.
    // Snapping the centroid directly would land non-square modules off
    // the stud grid even though "snap is on".
    QPointF placedScene = cursorScenePx;
    if (snapStepStuds_ > 0.0) {
        const double pxPerStud = rendering::SceneBuilder::kPixelsPerStud;
        const QPointF cursorStuds(cursorScenePx.x() / pxPerStud,
                                   cursorScenePx.y() / pxPerStud);
        const QPointF wantTopLeft = cursorStuds + dragPreviewModuleTopLeftOffsetStuds_;
        QPointF snappedTopLeft(
            std::round(wantTopLeft.x() / snapStepStuds_) * snapStepStuds_,
            std::round(wantTopLeft.y() / snapStepStuds_) * snapStepStuds_);
        const QPointF snappedCentroid = snappedTopLeft - dragPreviewModuleTopLeftOffsetStuds_;
        placedScene = QPointF(snappedCentroid.x() * pxPerStud,
                               snappedCentroid.y() * pxPerStud);
    }
    dragPreviewItem_->setRotation(0.0);
    dragPreviewItem_->setPos(placedScene);

    if (liveSnapActive_) {
        liveSnapActive_ = false;
        viewport()->update();
    }
}

bool MapView::dropModuleAt(const QString& bbmPath, QPointF scenePos) {
    clearDragPreview();
    if (bbmPath.isEmpty()) return false;
    auto res = saveload::readBbm(bbmPath);
    if (!res.ok()) return false;
    parts::placement::fixStaleAreas(*res.map, parts_);
    const double px = rendering::SceneBuilder::kPixelsPerStud;

    // Build per-layer batches (preserves the module's z-order /
    // layering so tracks don't land on top of scenery) and translate
    // every batch's bricks so the module's centroid lands at the
    // drop position.
    std::vector<edit::ImportBbmAsModuleCommand::LayerBatch> batches;
    QPointF srcCentre; int count = 0;
    QRectF  srcBbox;
    for (const auto& L : res.map->layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        edit::ImportBbmAsModuleCommand::LayerBatch batch;
        batch.layerName = L->name.isEmpty() ? QStringLiteral("Module") : L->name;
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks) {
            srcCentre += b.displayArea.center(); ++count;
            srcBbox = srcBbox.isNull() ? b.displayArea
                                        : srcBbox.united(b.displayArea);
            core::Brick copy = b;
            copy.guid.clear();
            batch.bricks.push_back(std::move(copy));
        }
        if (!batch.bricks.empty()) batches.push_back(std::move(batch));
    }
    if (batches.empty() || count == 0) return false;
    srcCentre /= count;
    QPointF targetCentre(scenePos.x() / px, scenePos.y() / px);
    // Grid-snap the bbox TOP-LEFT (not the centroid) so the module's
    // visible edges line up with the stud grid, matching the preview
    // ghost. Snapping the centroid would leave non-square modules
    // off-grid even with snap enabled.
    if (snapStepStuds_ > 0.0) {
        const QPointF offset = srcBbox.topLeft() - srcCentre;
        const QPointF wantTL = targetCentre + offset;
        const QPointF snappedTL(
            std::round(wantTL.x() / snapStepStuds_) * snapStepStuds_,
            std::round(wantTL.y() / snapStepStuds_) * snapStepStuds_);
        targetCentre = snappedTL - offset;
    }
    const QPointF translation = targetCentre - srcCentre;
    for (auto& batch : batches)
        for (auto& b : batch.bricks) b.displayArea.translate(translation);

    // Connection-snap pass: if any brick in the placed module has a
    // free connection that lands within `connSnapThreshold` of a
    // free compatible target in the host map, apply an additional
    // group translation so they coincide. Picks the smallest-shift
    // candidate so the module barely moves but locks against an
    // existing free track end. No rotation — modules drop at fixed
    // orientation; the user can rotate post-drop with R.
    {
        auto rotPt = [](QPointF p, double deg) {
            const double r = deg * M_PI / 180.0;
            const double c = std::cos(r), s = std::sin(r);
            return QPointF(p.x() * c - p.y() * s, p.x() * s + p.y() * c);
        };
        const double connThresh = connectionSnapThresholdStuds();
        const double connThreshSq = connThresh * connThresh;

        // Collect every free connection of the placed module bricks
        // in world (stud) coords with its type.
        struct ModConn { QString type; QPointF worldStuds; };
        std::vector<ModConn> modConns;
        for (const auto& batch : batches) {
            for (const auto& b : batch.bricks) {
                auto meta = parts_.metadata(b.partNumber);
                if (!meta) continue;
                const QPointF cen = parts::placement::imageCentre(b, parts_);
                const int n = meta->connections.size();
                for (int i = 0; i < n; ++i) {
                    const auto& c = meta->connections[i];
                    if (c.type.isEmpty()) continue;
                    // Internal connections (linked to another module
                    // brick) shouldn't seek host targets — they'll be
                    // re-linked by rebuildConnectivity after insert.
                    // We treat ALL module connections as candidates;
                    // matching against an internal partner can't hit
                    // because the partner moved with us so it's not
                    // in the host map yet.
                    modConns.push_back({ c.type,
                                          cen + rotPt(c.position, b.orientation) });
                }
            }
        }

        // Scan host map for free targets and find the smallest-shift
        // pairing.
        QPointF bestShift;
        double  bestShiftSq = std::numeric_limits<double>::max();
        bool    bestFound = false;
        for (const auto& layerPtr : map_->layers()) {
            if (!layerPtr || layerPtr->kind() != core::LayerKind::Brick) continue;
            const auto& BL = static_cast<const core::LayerBrick&>(*layerPtr);
            for (const auto& tb : BL.bricks) {
                auto tmeta = parts_.metadata(tb.partNumber);
                if (!tmeta) continue;
                const QPointF tCen = parts::placement::imageCentre(tb, parts_);
                const int nT = tmeta->connections.size();
                for (int tci = 0; tci < nT; ++tci) {
                    const auto& tc = tmeta->connections[tci];
                    if (tc.type.isEmpty()) continue;
                    if (tci < static_cast<int>(tb.connections.size()) &&
                        !tb.connections[tci].linkedToId.isEmpty()) continue;
                    const QPointF tWorld = tCen + rotPt(tc.position, tb.orientation);
                    for (const auto& mc : modConns) {
                        if (mc.type != tc.type) continue;
                        const QPointF d = tWorld - mc.worldStuds;
                        const double sq = d.x() * d.x() + d.y() * d.y();
                        if (sq > connThreshSq) continue;
                        if (sq < bestShiftSq) {
                            bestShiftSq = sq;
                            bestShift = d;
                            bestFound = true;
                        }
                    }
                }
            }
        }

        if (bestFound) {
            for (auto& batch : batches)
                for (auto& b : batch.bricks) b.displayArea.translate(bestShift);
        }
    }

    const QString name = QFileInfo(bbmPath).completeBaseName();
    auto* cmd = new edit::ImportBbmAsModuleCommand(
        *map_, bbmPath, name, std::move(batches));
    undoStack_->push(cmd);
    const auto placed = cmd->placedBricks();
    // Select every just-placed brick so R / Shift+R rotate the
    // freshly-dropped module, and arrow keys nudge it. Without this
    // the user has to rubber-band-select after every drop to do
    // anything else with the module.
    if (!placed.isEmpty()) {
        scene()->clearSelection();
        QSet<QString> wantedGuids;
        QSet<int> wantedLayers;
        for (const auto& p : placed) {
            wantedGuids.insert(p.guid);
            wantedLayers.insert(p.layerIndex);
        }
        for (QGraphicsItem* it : scene()->items()) {
            if (!isBrickItem(it)) continue;
            if (!wantedLayers.contains(it->data(kBrickDataLayerIndex).toInt())) continue;
            if (wantedGuids.contains(it->data(kBrickDataGuid).toString()))
                it->setSelected(true);
        }
    }
    return true;
}

void MapView::dropEvent(QDropEvent* e) {
    clearDropTargetHint();
    const QString partMime   = QString::fromLatin1(PartsBrowser::kPartMimeType);
    const QString moduleMime = QString::fromLatin1(kModuleDragMimeType);
    const QPointF scenePos = mapToScene(e->position().toPoint());

    if (e->mimeData()->hasFormat(moduleMime)) {
        if (dropModuleAt(QString::fromUtf8(e->mimeData()->data(moduleMime)), scenePos)) e->acceptProposedAction();
        else e->ignore();
        return;
    }

    if (!e->mimeData()->hasFormat(partMime)) {
        clearDragPreview();
        QGraphicsView::dropEvent(e);
        return;
    }
    clearDragPreview();
    const QString key = QString::fromUtf8(e->mimeData()->data(partMime));
    if (key.isEmpty()) { e->ignore(); return; }
    addPartAtScenePos(key, scenePos);
    e->acceptProposedAction();
}

void MapView::deleteSelected() {
    if (!map_) return;
    std::vector<edit::DeleteBricksCommand::Entry> brickEntries;
    // Keyed hits for other types — bundled into one undo macro.
    struct TextHit  { int li; QString guid; };
    struct RulerHit { int li; QString guid; };
    std::vector<TextHit>  textHits;
    std::vector<RulerHit> rulerHits;
    QStringList labelIds;
    bool venueSelected = false;

    for (QGraphicsItem* it : scene()->selectedItems()) {
        if (isBrickItem(it)) {
            const int li = it->data(kBrickDataLayerIndex).toInt();
            const QString guid = it->data(kBrickDataGuid).toString();
            if (li < 0 || li >= static_cast<int>(map_->layers().size())) continue;
            auto* L = map_->layers()[li].get();
            if (!L || L->kind() != core::LayerKind::Brick) continue;
            auto& BL = static_cast<core::LayerBrick&>(*L);
            for (int i = 0; i < static_cast<int>(BL.bricks.size()); ++i) {
                if (BL.bricks[i].guid == guid) {
                    edit::DeleteBricksCommand::Entry e;
                    e.layerIndex = li;
                    e.indexInLayer = i;
                    e.brick = BL.bricks[i];
                    brickEntries.push_back(std::move(e));
                    break;
                }
            }
        } else if (isTextItem(it)) {
            textHits.push_back({ it->data(kBrickDataLayerIndex).toInt(),
                                  it->data(kBrickDataGuid).toString() });
        } else if (isRulerItem(it)) {
            rulerHits.push_back({ it->data(kBrickDataLayerIndex).toInt(),
                                    it->data(kBrickDataGuid).toString() });
        } else if (isLabelItem(it)) {
            labelIds.append(it->data(kBrickDataGuid).toString());
        } else if (isVenueItem(it)) {
            venueSelected = true;
        }
    }
    if (brickEntries.empty() && textHits.empty() &&
        rulerHits.empty() && labelIds.isEmpty() && !venueSelected) return;

    // Before mutating, find a connected neighbor of the bricks being
    // deleted so we can move the selection to it after the rebuild.
    // Lets the user fan-delete down a track chain without re-clicking.
    struct NeighborRef { int layerIndex; QString guid; };
    std::optional<NeighborRef> survivingNeighbor;
    if (!brickEntries.empty()) {
        QSet<QString> deletedGuids;
        for (const auto& e : brickEntries) deletedGuids.insert(e.brick.guid);
        // Build connectionGuid -> (layerIndex, brickGuid) so we can resolve
        // either flavour of linkedToId: rebuildConnectivity stores the
        // partner's brick guid, while freshly-loaded .bbm files store the
        // partner's connection guid.
        QHash<QString, NeighborRef> brickByConnGuid;
        QHash<QString, NeighborRef> brickByBrickGuid;
        for (int li = 0; li < static_cast<int>(map_->layers().size()); ++li) {
            auto* L = map_->layers()[li].get();
            if (!L || L->kind() != core::LayerKind::Brick) continue;
            const auto& BL = static_cast<const core::LayerBrick&>(*L);
            for (const auto& bb : BL.bricks) {
                brickByBrickGuid.insert(bb.guid, { li, bb.guid });
                for (const auto& c : bb.connections) {
                    if (!c.guid.isEmpty()) brickByConnGuid.insert(c.guid, { li, bb.guid });
                }
            }
        }
        for (const auto& e : brickEntries) {
            for (const auto& c : e.brick.connections) {
                if (c.linkedToId.isEmpty()) continue;
                NeighborRef ref{-1, {}};
                if (auto it = brickByBrickGuid.constFind(c.linkedToId);
                    it != brickByBrickGuid.constEnd()) {
                    ref = it.value();
                } else if (auto it2 = brickByConnGuid.constFind(c.linkedToId);
                           it2 != brickByConnGuid.constEnd()) {
                    ref = it2.value();
                }
                if (ref.layerIndex < 0) continue;
                if (deletedGuids.contains(ref.guid)) continue;
                survivingNeighbor = ref;
                break;
            }
            if (survivingNeighbor) break;
        }
    }

    undoStack_->beginMacro(tr("Delete selection"));
    if (!brickEntries.empty()) {
        undoStack_->push(new edit::DeleteBricksCommand(*map_, std::move(brickEntries)));
    }
    for (const auto& h : textHits) {
        undoStack_->push(new edit::DeleteTextCellCommand(*map_, h.li, h.guid));
    }
    for (const auto& h : rulerHits) {
        undoStack_->push(new edit::DeleteRulerItemCommand(*map_, h.li, h.guid));
    }
    for (const QString& id : labelIds) {
        undoStack_->push(new edit::DeleteAnchoredLabelCommand(*map_, id));
    }
    if (venueSelected) {
        const auto btn = QMessageBox::question(this, tr("Delete venue"),
            tr("Remove the entire venue (outline + obstacles)?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (btn == QMessageBox::Yes) {
            undoStack_->push(new edit::SetVenueCommand(*map_, std::nullopt));
        }
    }
    undoStack_->endMacro();
    // The undo-stack indexChanged handler rebuilds + preserves selection.
    // If a brick we deleted was connected to something that survived,
    // move the selection onto that neighbor so the user can keep
    // deleting / chaining without re-clicking.
    if (survivingNeighbor) {
        scene()->clearSelection();
        for (QGraphicsItem* it : scene()->items()) {
            if (!isBrickItem(it)) continue;
            if (it->data(kBrickDataLayerIndex).toInt() != survivingNeighbor->layerIndex) continue;
            if (it->data(kBrickDataGuid).toString() != survivingNeighbor->guid) continue;
            it->setSelected(true);
            break;
        }
    }
}

}
