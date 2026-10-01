#include "TouchMode.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QInputEvent>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QScroller>
#include <QScrollerProperties>

namespace bld::ui {

TouchMode& TouchMode::instance() {
    static TouchMode* mode = new TouchMode();  // lives as long as the app
    return *mode;
}

void TouchMode::install() {
    if (installed_ || !qApp) return;
    installed_ = true;
    qApp->installEventFilter(this);
}

void TouchMode::setActive(bool on) {
    if (on == active_) return;
    active_ = on;
    emit changed(on);
}

bool TouchMode::fromTouchScreen(const QInputEvent* e) {
    const QInputDevice* d = e ? e->device() : nullptr;
    return d && d->type() == QInputDevice::DeviceType::TouchScreen;
}

bool TouchMode::fromMouse(const QInputEvent* e) {
    if (!e || fromTouchScreen(e)) return false;
    if (const auto* m = dynamic_cast<const QMouseEvent*>(e))
        if (m->source() != Qt::MouseEventNotSynthesized) return false;
    const QInputDevice* d = e->device();
    return !d || d->type() == QInputDevice::DeviceType::Mouse;
}

void TouchMode::enableFlick(QAbstractScrollArea* area) {
    if (!area || area->property("bldNoTouchScroll").toBool()) return;
    QWidget* vp = area->viewport();
    if (!vp || QScroller::hasScroller(vp)) return;
    QScroller::grabGesture(vp, QScroller::TouchGesture);
    QScrollerProperties props = QScroller::scroller(vp)->scrollerProperties();
    props.setScrollMetric(QScrollerProperties::OvershootDragResistanceFactor, 0.3);
    props.setScrollMetric(QScrollerProperties::OvershootScrollDistanceFactor, 0.1);
    QScroller::scroller(vp)->setScrollerProperties(props);
    if (auto* view = qobject_cast<QAbstractItemView*>(area)) {
        view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    }
}

bool TouchMode::eventFilter(QObject* obj, QEvent* ev) {
    switch (ev->type()) {
    case QEvent::TouchBegin:
        if (fromTouchScreen(static_cast<QInputEvent*>(ev))) setActive(true);
        break;
    case QEvent::MouseButtonPress:
    case QEvent::Wheel:
        if (fromMouse(static_cast<QInputEvent*>(ev))) setActive(false);
        break;
    case QEvent::MouseMove:
        // Only a move with the pointer actually travelling: some systems
        // send a still "move" after a touch.
        if (active_ && fromMouse(static_cast<QInputEvent*>(ev))) {
            static QPointF last;
            const QPointF p = static_cast<QMouseEvent*>(ev)->globalPosition();
            if ((p - last).manhattanLength() > 4 && !last.isNull()) setActive(false);
            last = p;
        }
        break;
    case QEvent::Polish:
        if (auto* area = qobject_cast<QAbstractScrollArea*>(obj)) enableFlick(area);
        break;
    default:
        break;
    }
    return QObject::eventFilter(obj, ev);
}

}  // namespace bld::ui
