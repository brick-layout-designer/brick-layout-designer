#pragma once

// The same header on every dock: the panel's name in the heading font, an
// optional small "?" that explains the panel in one line (hidden when
// Settings > Show help buttons is off), and a close button. Installed as
// the dock's title bar, so dragging and double-click-to-float still work.

#include <QWidget>

class QDockWidget;
class QLabel;
class QToolButton;

namespace bld::ui::theme {

class PrefsStore;

class PanelHeader : public QWidget {
    Q_OBJECT
public:
    // Follows the dock's window title and `store`'s help-button setting.
    PanelHeader(QDockWidget* dock, const QString& helpText, PrefsStore& store);

    // Puts a header on `dock`.
    static PanelHeader* install(QDockWidget* dock, const QString& helpText, PrefsStore& store);

    QLabel* title() const { return title_; }
    QToolButton* helpButton() const { return help_; }

protected:
    void changeEvent(QEvent* e) override;

private:
    void refreshFont();
    QLabel* title_ = nullptr;
    QToolButton* help_ = nullptr;
    QString helpText_;
};

}  // namespace bld::ui::theme
