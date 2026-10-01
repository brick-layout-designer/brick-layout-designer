#pragma once

// Touch mode: on while the last input came from a touchscreen, off again on
// the first real mouse press, move or wheel. Things that are hard to hit
// with a finger (panel dividers, dock headers, the panel "⋯" menu, ruler
// endpoints) grow while it is on, and the map shows its touch action bar.

#include <QObject>

class QAbstractScrollArea;
class QInputEvent;

namespace bld::ui {

class TouchMode : public QObject {
    Q_OBJECT
public:
    static TouchMode& instance();

    // Watches every input event of the application (once; later calls do
    // nothing). Also gives scroll areas touch flicking as they appear.
    void install();

    bool active() const { return active_; }
    void setActive(bool on);

    // A finger on a touchscreen (not a touchpad, not a mouse).
    static bool fromTouchScreen(const QInputEvent* e);
    // A real mouse (not one Qt or the system made up from a touch).
    static bool fromMouse(const QInputEvent* e);

    // Flick-to-scroll by touch for `area` (mouse and wheel stay as they
    // are). Areas with the "bldNoTouchScroll" property are left alone.
    static void enableFlick(QAbstractScrollArea* area);

    // The smallest a target may be in touch mode, in pixels.
    static constexpr int kMinTarget = 24;

signals:
    void changed(bool active);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    TouchMode() = default;
    bool active_ = false;
    bool installed_ = false;
};

}  // namespace bld::ui
