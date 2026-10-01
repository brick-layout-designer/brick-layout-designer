#pragma once

// The same header on every dock: the panel's name in the heading font, an
// optional "?" (help::HelpButton: a one-line tooltip, and more on click;
// hidden when Settings > Show help buttons is off), a "⋯" menu that moves
// the panel (the web's PanelHost / FloatingPanel menu, same words) and a
// close button. Installed as the dock's title bar, so dragging and
// double-click-to-float still work.

#include <QList>
#include <QString>
#include <QWidget>

class QDockWidget;
class QLabel;
class QMainWindow;
class QMenu;
class QToolButton;

namespace bld::ui::help {
class HelpButton;
}

namespace bld::ui::theme {

class PrefsStore;

// Where a panel is, like the web's DockZone. Top and bottom docks (an old
// saved layout can still hold them) count as Elsewhere.
enum class PanelPlace { Left, Right, Float, Hidden, Elsewhere };

class PanelHeader : public QWidget {
    Q_OBJECT
public:
    // Follows the dock's window title. `helpKey` is a HelpTexts key (no
    // "?" when it is empty); the "?" follows `store`'s help-button setting.
    PanelHeader(QDockWidget* dock, const QString& helpKey, PrefsStore& store);

    // Puts a header on `dock` and keeps the dock to the left and right
    // sides, the two the web has.
    static PanelHeader* install(QDockWidget* dock, const QString& helpKey, PrefsStore& store);

    QLabel* title() const { return title_; }
    // Null when the panel has no help.
    help::HelpButton* helpButton() const { return help_; }
    // The "⋯" button and its menu (filled each time it opens).
    QToolButton* menuButton() const { return menuButton_; }
    QMenu* menu() const { return menu_; }
    // The header's buttons in touch mode, square, in px.
    static constexpr int kTouchButton = 40;

    struct Target {
        PanelPlace place;
        QString label;
    };
    // What the "⋯" menu offers a panel at `current`: a docked panel the
    // places it is not in (Move to left / Move to right / Float panel /
    // Hide panel), a floating one Dock to left / Dock to right / Hide panel.
    static QList<Target> targets(PanelPlace current);

    static PanelPlace placeOf(const QDockWidget* dock);
    // Moves `dock` there; a move onto an empty side gives it its width.
    static void moveTo(QDockWidget* dock, PanelPlace place);
    // The Panels menu's tick: a hidden panel comes back on the right side
    // (like the web), unticking hides it.
    static void setShown(QDockWidget* dock, bool shown);

protected:
    void changeEvent(QEvent* e) override;

private:
    void refreshFont();
    void fillMenu();
    QDockWidget* dock_ = nullptr;
    QLabel* title_ = nullptr;
    help::HelpButton* help_ = nullptr;
    QToolButton* menuButton_ = nullptr;
    QMenu* menu_ = nullptr;
};

}  // namespace bld::ui::theme
