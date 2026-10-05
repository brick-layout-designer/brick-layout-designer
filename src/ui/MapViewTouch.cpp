// Touch and trackpad gestures on the map.
//
//   * One finger on a part, a ruler handle, or with a drawing tool: the
//     mouse's left button, sent through mousePress / Move / ReleaseEvent, so
//     selecting, moving, grid snap and connection snap are exactly the
//     mouse's.
//   * One finger on empty map: pans; a tap there clears the selection (or
//     places the part picked in the parts panel).
//   * Two fingers: pinch to zoom around them, and pan as they move.
//   * A finger resting still: the right-click menu, with a filling ring.
//   * Trackpads (macOS, Windows precision touchpads, libinput): pinch zooms,
//     a pan gesture pans. Wheel and two-finger scroll stay as they were.

#include "MapView.h"
#include "ModuleEditBar.h"

#include "../core/LayerRuler.h"
#include "../core/Map.h"
#include "../parts/PartsLibrary.h"
#include "MapViewInternal.h"
#include "TouchActionBar.h"
#include "TouchMode.h"

#include <QContextMenuEvent>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPointingDevice>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QTouchEvent>
#include <QUndoStack>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

namespace bld::ui {

using detail::isBrickItem;
using detail::isLabelItem;
using detail::isRulerItem;
using detail::isTextItem;
using detail::isVenueItem;
using detail::kMaxZoom;
using detail::kMinZoom;

namespace {
// How far a finger may wander and still count as a tap or a rest.
constexpr double kTouchSlop = 10.0;
}  // namespace

bool MapView::viewportEvent(QEvent* e) {
    switch (e->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
        if (handleTouch(static_cast<QTouchEvent*>(e))) return true;
        break;
    case QEvent::NativeGesture:
        if (handleNativeGesture(static_cast<QNativeGestureEvent*>(e))) return true;
        break;
    default:
        break;
    }
    return QGraphicsView::viewportEvent(e);
}

void MapView::resizeEvent(QResizeEvent* e) {
    QGraphicsView::resizeEvent(e);
    if (touchBar_ && touchBar_->isVisible()) touchBar_->place(viewport()->geometry());
    if (editBar_ && editBar_->isVisible()) editBar_->place(viewport()->geometry());
}

void MapView::panBy(QPointF delta) {
    // Content follows the finger: moving right shows what was to the left.
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() - qRound(delta.x()));
    verticalScrollBar()->setValue(verticalScrollBar()->value() - qRound(delta.y()));
}

void MapView::zoomAt(QPointF from, QPointF to, double factor) {
    const QPointF anchor = viewportTransform().inverted().map(from);
    const double current = transform().m11();
    const double next = std::clamp(current * factor, kMinZoom, kMaxZoom);
    const double step = next / current;
    if (std::abs(step - 1.0) > 1e-6) {
        const auto old = transformationAnchor();
        setTransformationAnchor(QGraphicsView::NoAnchor);
        scale(step, step);
        setTransformationAnchor(old);
    }
    // Put the anchor back under the fingers (and follow them as they move).
    panBy(to - viewportTransform().map(anchor));
}

bool MapView::handleNativeGesture(QNativeGestureEvent* e) {
    const QPointF at = e->position();
    switch (e->gestureType()) {
    case Qt::BeginNativeGesture:
    case Qt::EndNativeGesture:
        e->accept();
        return true;
    case Qt::ZoomNativeGesture:
        // value(): how much bigger since the last event (0.1 = 10 %).
        zoomAt(at, at, 1.0 + e->value());
        e->accept();
        return true;
    case Qt::SmartZoomNativeGesture:
        zoomAt(at, at, e->value() > 0 ? 2.0 : 0.5);
        e->accept();
        return true;
    case Qt::PanNativeGesture:
        panBy(e->delta());
        e->accept();
        return true;
    default:
        return false;
    }
}

void MapView::sendTouchMouse(QEvent::Type type, QPointF viewPos) {
    const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    const Qt::MouseButtons buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
    // Handed straight to the handlers, never through the application. Not
    // on the touchscreen's device: a mouse event there would take over the
    // finger's touch point, and its moves would stop coming here.
    QMouseEvent ev(type, viewPos, viewport()->mapToGlobal(viewPos), button, buttons,
                   Qt::NoModifier, QPointingDevice::primaryPointingDevice());
    switch (type) {
    case QEvent::MouseButtonPress: mousePressEvent(&ev); break;
    case QEvent::MouseMove: mouseMoveEvent(&ev); break;
    case QEvent::MouseButtonRelease: mouseReleaseEvent(&ev); break;
    default: break;
    }
}

void MapView::cancelTouchPress() {
    if (touchState_ != TouchState::Press) return;
    // Let go without a release, so nothing is clicked or committed.
    if (scene())
        if (QGraphicsItem* grabber = scene()->mouseGrabberItem()) grabber->ungrabMouse();
    bool moved = false;
    for (const auto& s : dragStart_)
        if (s.item && s.item->scenePos() != s.scenePosAtPress) moved = true;
    for (const auto& s : rulerDragStart_)
        if (s.anyPiece && s.anyPiece->scenePos() != s.scenePosAtPress) moved = true;
    for (const auto& s : labelDragStart_)
        if (s.item && s.item->scenePos() != s.scenePosAtPress) moved = true;
    dragStart_.clear();
    rulerDragStart_.clear();
    labelDragStart_.clear();
    clearGrabAnchor();
    liveSnapActive_ = false; liveSnapMovingScene_.reset();
    if (draggingRulerEndpoint_) {
        // Its live edit goes back with a release at the press point.
        draggingRulerEndpoint_ = false;
        moved = true;
        if (map_ && rulerEndpointLayer_ >= 0 && rulerEndpointLayer_ < static_cast<int>(map_->layers().size())) {
            auto* L = map_->layers()[rulerEndpointLayer_].get();
            if (L && L->kind() == core::LayerKind::Ruler) {
                for (auto& any : static_cast<core::LayerRuler&>(*L).rulers) {
                    if (any.kind != core::RulerKind::Linear || any.linear.guid != rulerEndpointGuid_) continue;
                    (rulerEndpointIndex_ == 0 ? any.linear.point1 : any.linear.point2) = rulerEndpointOriginalStuds_;
                    const QPointF& p1 = any.linear.point1;
                    const QPointF& p2 = any.linear.point2;
                    any.linear.displayArea = QRectF(QPointF(std::min(p1.x(), p2.x()), std::min(p1.y(), p2.y())),
                                                    QPointF(std::max(p1.x(), p2.x()), std::max(p1.y(), p2.y())));
                }
            }
        }
    }
    if (moved) rebuildScene();  // the model never changed: parts go back
    refreshSelectionOverlay();
}

bool MapView::longPressRingShown() const {
    return longPressAnim_ && longPressAnim_->state() == QAbstractAnimation::Running;
}

void MapView::stopLongPressRing() {
    if (!longPressAnim_) return;
    longPressAnim_->stop();
    viewport()->update();
}

void MapView::paintLongPressRing(QPainter* painter) {
    if (!longPressRingShown()) return;
    painter->save();
    painter->resetTransform();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(touchStart_ - QPointF(26, 26), QSizeF(52, 52));
    QColor fill = palette().color(QPalette::Highlight);
    fill.setAlphaF(0.18f);
    painter->setPen(Qt::NoPen);
    painter->setBrush(fill);
    painter->drawEllipse(r);
    QPen pen(palette().color(QPalette::Highlight), 4);
    pen.setCapStyle(Qt::RoundCap);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawArc(r, 90 * 16, -qRound(longPressAnim_->currentValue().toDouble() * 360 * 16));
    painter->restore();
}

void MapView::longPressFired() {
    const QPointF at = touchLast_;
    if (touchState_ == TouchState::Press) sendTouchMouse(QEvent::MouseButtonRelease, at);
    else if (touchState_ != TouchState::PanOrTap) return;
    touchState_ = TouchState::LongPressed;
    // The menu runs its own loop: open it once this timer slot is done.
    QTimer::singleShot(0, this, [this, at] {
        stopLongPressRing();
        QContextMenuEvent ce(QContextMenuEvent::Other, at.toPoint(), viewport()->mapToGlobal(at.toPoint()));
        contextMenuEvent(&ce);
        refreshTouchBar();
    });
}

bool MapView::handleTouch(QTouchEvent* e) {
    if (!TouchMode::fromTouchScreen(e)) return false;  // touchpads: Qt's own
    TouchMode::instance().setActive(true);
    if (!longPressTimer_) {
        longPressTimer_ = new QTimer(this);
        longPressTimer_->setSingleShot(true);
        longPressTimer_->setInterval(kLongPressMs);
        connect(longPressTimer_, &QTimer::timeout, this, &MapView::longPressFired);
        longPressAnim_ = new QVariantAnimation(this);
        longPressAnim_->setStartValue(0.0);
        longPressAnim_->setEndValue(1.0);
        longPressAnim_->setDuration(kLongPressMs);
        connect(longPressAnim_, &QVariantAnimation::valueChanged, viewport(), qOverload<>(&QWidget::update));
    }
    QList<QPointF> down;
    for (const auto& p : e->points())
        if (p.state() != QEventPoint::Released) down.append(p.position());
    e->accept();

    auto stopRest = [this] {
        longPressTimer_->stop();
        stopLongPressRing();
    };

    if (e->type() == QEvent::TouchBegin) {
        // Starts over, whatever an earlier touch left (a menu may have
        // eaten its end).
        touchState_ = TouchState::None;
        touchMoved_ = false;
        if (down.isEmpty()) return true;
        touchStart_ = touchLast_ = down.first();
        if (down.size() >= 2) {  // both fingers at once
            touchState_ = TouchState::Pinch;
            pinchCentre_ = (down[0] + down[1]) / 2.0;
            pinchDist_ = std::max(1.0, QLineF(down[0], down[1]).length());
            return true;
        }
        const QPoint vp = touchStart_.toPoint();
        QGraphicsItem* under = itemAt(vp);
        while (under && under->parentItem()) under = under->parentItem();
        const bool onItem = under && (isBrickItem(under) || isTextItem(under) || isRulerItem(under)
                                      || isLabelItem(under) || isVenueItem(under));
        const bool selectTool = tool_ == Tool::Select;
        const bool asMouse = !selectTool
            || (armedPart_.isEmpty() && (onItem || rulerEndpointAt(mapToScene(vp), false) || wouldDragGridOrigin(vp)));
        if (asMouse) {
            touchState_ = TouchState::Press;
            sendTouchMouse(QEvent::MouseButtonPress, touchStart_);
        } else {
            touchState_ = TouchState::PanOrTap;
        }
        if (selectTool) {
            longPressTimer_->start();
            longPressAnim_->start();
        }
        return true;
    }

    if (e->type() == QEvent::TouchCancel) {
        stopRest();
        cancelTouchPress();
        touchState_ = TouchState::None;
        return true;
    }

    if (e->type() == QEvent::TouchUpdate) {
        if (down.size() >= 2) {
            const QPointF c = (down[0] + down[1]) / 2.0;
            const double d = std::max(1.0, QLineF(down[0], down[1]).length());
            if (touchState_ != TouchState::Pinch) {
                stopRest();
                cancelTouchPress();
                touchState_ = TouchState::Pinch;
            } else {
                zoomAt(pinchCentre_, c, d / pinchDist_);
            }
            pinchCentre_ = c;
            pinchDist_ = d;
            return true;
        }
        if (down.isEmpty()) return true;
        const QPointF p = down.first();
        if (touchState_ == TouchState::Pinch) {  // one finger lifted: keep panning
            touchState_ = TouchState::Pan;
            touchLast_ = p;
            return true;
        }
        const bool beyondSlop = QLineF(p, touchStart_).length() > kTouchSlop;
        if (beyondSlop) stopRest();
        switch (touchState_) {
        case TouchState::Press:
            // Small wobbles under the slop don't nudge a tapped part.
            if (beyondSlop) touchMoved_ = true;
            if (touchMoved_) {
                touchLast_ = p;
                sendTouchMouse(QEvent::MouseMove, p);
            }
            break;
        case TouchState::PanOrTap:
            if (!beyondSlop) break;
            touchState_ = TouchState::Pan;
            [[fallthrough]];
        case TouchState::Pan:
            panBy(p - touchLast_);
            touchLast_ = p;
            break;
        default:
            break;
        }
        return true;
    }

    // TouchEnd.
    stopRest();
    switch (touchState_) {
    case TouchState::Press:
        sendTouchMouse(QEvent::MouseButtonRelease, touchLast_);
        break;
    case TouchState::PanOrTap:
        if (!armedPart_.isEmpty()) {
            const QString key = armedPart_;
            armedPart_.clear();
            addPartAtScenePos(key, mapToScene(touchStart_.toPoint()));
        } else if (scene()) {
            scene()->clearSelection();
        }
        break;
    default:
        break;
    }
    touchState_ = TouchState::None;
    refreshTouchBar();
    return true;
}

void MapView::armPartPlacement(const QString& partKey) {
    armedPart_ = partKey;
    refreshTouchBar();
}

void MapView::cancelPartPlacement() {
    armedPart_.clear();
    refreshTouchBar();
}

void MapView::touchPartDragTo(const QString& partKey, QPoint globalPos) {
    const QPoint vp = viewport()->mapFromGlobal(globalPos);
    if (!viewport()->rect().contains(vp)) {
        touchPartDragCancel();
        return;
    }
    updateDragPreview(partKey, mapToScene(vp));
}

bool MapView::touchPartDropAt(const QString& partKey, QPoint globalPos) {
    clearDragPreview();
    const QPoint vp = viewport()->mapFromGlobal(globalPos);
    if (!map_ || partKey.isEmpty() || !viewport()->rect().contains(vp)) return false;
    addPartAtScenePos(partKey, mapToScene(vp), &placeSnap_);
    refreshTouchBar();
    return true;
}

void MapView::touchPartDragCancel() {
    placeSnap_.reset();
    moduleSnap_.reset();
    clearDragPreview();
}

void MapView::touchModuleDragTo(const QString& bbmPath, QPoint globalPos) {
    const QPoint vp = viewport()->mapFromGlobal(globalPos);
    if (!viewport()->rect().contains(vp)) {
        touchPartDragCancel();
        return;
    }
    updateModuleDragPreview(bbmPath, mapToScene(vp));
}

bool MapView::touchModuleDropAt(const QString& bbmPath, QPoint globalPos) {
    clearDragPreview();
    const QPoint vp = viewport()->mapFromGlobal(globalPos);
    if (!map_ || !viewport()->rect().contains(vp)) return false;
    const bool placed = dropModuleAt(bbmPath, mapToScene(vp), &moduleSnap_);
    refreshTouchBar();
    return placed;
}

void MapView::refreshTouchBar() {
    const bool touch = TouchMode::instance().active();
    if (!touchBar_) {
        if (!touch) return;
        touchBar_ = new TouchActionBar(this);
        connect(touchBar_, &TouchActionBar::rotateLeft, this, [this] { rotateSelected(static_cast<float>(-rotationStepDegrees_)); });
        connect(touchBar_, &TouchActionBar::rotateRight, this, [this] { rotateSelected(static_cast<float>(rotationStepDegrees_)); });
        connect(touchBar_, &TouchActionBar::duplicate, this, &MapView::duplicateSelection);
        connect(touchBar_, &TouchActionBar::remove, this, &MapView::deleteSelected);
        connect(touchBar_, &TouchActionBar::done, this, &MapView::deselectAll);
        connect(touchBar_, &TouchActionBar::addPart, this, &MapView::addPartRequested);
        connect(touchBar_, &TouchActionBar::undo, this, [this] { undoStack_->undo(); });
        connect(touchBar_, &TouchActionBar::redo, this, [this] { undoStack_->redo(); });
        connect(touchBar_, &TouchActionBar::cancelPlacing, this, &MapView::cancelPartPlacement);
    }
    if (!touch || !map_) {
        touchBar_->hide();
        return;
    }
    if (!armedPart_.isEmpty()) {
        QString name = armedPart_;
        if (auto meta = parts_.metadata(armedPart_); meta && !meta->descriptions.isEmpty()) {
            name = meta->descriptions.front().text;
            for (const auto& d : meta->descriptions)
                if (d.language == QStringLiteral("en")) { name = d.text; break; }
        }
        touchBar_->setMode(TouchActionBar::Mode::Placing, name);
    } else {
        bool any = false;
        for (QGraphicsItem* it : scene()->selectedItems())
            if (!it->data(detail::kBrickDataKind).toString().isEmpty()) { any = true; break; }
        touchBar_->setMode(any ? TouchActionBar::Mode::Selection : TouchActionBar::Mode::Idle);
        if (auto* u = touchBar_->button(QStringLiteral("touchUndo"))) u->setEnabled(undoStack_->canUndo());
        if (auto* r = touchBar_->button(QStringLiteral("touchRedo"))) r->setEnabled(undoStack_->canRedo());
    }
    touchBar_->place(viewport()->geometry());
    touchBar_->show();
    touchBar_->raise();
}

}  // namespace bld::ui
