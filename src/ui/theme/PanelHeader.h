#pragma once

// The same header on every dock: the panel's name in the heading font, an
// optional "?" (help::HelpButton: a one-line tooltip, and more on click;
// hidden when Settings > Show help buttons is off), and a close button. Installed as
// the dock's title bar, so dragging and double-click-to-float still work.

#include <QWidget>

class QDockWidget;
class QLabel;
class QToolButton;

namespace bld::ui::help {
class HelpButton;
}

namespace bld::ui::theme {

class PrefsStore;

class PanelHeader : public QWidget {
    Q_OBJECT
public:
    // Follows the dock's window title. `helpKey` is a HelpTexts key (no
    // "?" when it is empty); the "?" follows `store`'s help-button setting.
    PanelHeader(QDockWidget* dock, const QString& helpKey, PrefsStore& store);

    // Puts a header on `dock`.
    static PanelHeader* install(QDockWidget* dock, const QString& helpKey, PrefsStore& store);

    QLabel* title() const { return title_; }
    // Null when the panel has no help.
    help::HelpButton* helpButton() const { return help_; }

protected:
    void changeEvent(QEvent* e) override;

private:
    void refreshFont();
    QLabel* title_ = nullptr;
    help::HelpButton* help_ = nullptr;
};

}  // namespace bld::ui::theme
