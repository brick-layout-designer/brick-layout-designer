#include "RefreshOnFocus.h"

#include <QEvent>
#include <QWidget>

#include <utility>

namespace bld::sync {

RefreshOnFocus::RefreshOnFocus(QWidget* watched, std::function<void()> refresh, int minGapMs)
    : QObject(watched), refresh_(std::move(refresh)), minGapMs_(minGapMs) {
    setObjectName(QStringLiteral("refreshOnFocus"));
    since_.start();
    watched->installEventFilter(this);
}

bool RefreshOnFocus::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::WindowDeactivate) {
        away_ = true;
    } else if (event->type() == QEvent::WindowActivate && away_) {
        away_ = false;
        if (since_.elapsed() >= minGapMs_) {
            since_.restart();
            ++count_;
            if (refresh_) refresh_();
        }
    }
    return QObject::eventFilter(watched, event);
}

} // namespace bld::sync
