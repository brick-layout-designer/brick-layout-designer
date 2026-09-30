#pragma once

// The small round "?" next to anything that isn't obvious (redesign "Help"
// board), the desktop twin of the web's HelpButton.tsx. Three states:
//   at rest      a quiet circled ?;
//   hover/focus  a one-sentence tooltip (the entry's short text);
//   click        a popover with the longer text, "Show me" (pulses the
//                outline of the control it explains) and "Learn more"
//                (the matching in-app help page; hidden when there is
//                none).
// Esc closes the popover and puts focus back on the button. The button
// hides everywhere while Settings › Show help buttons (AppPrefs::helpIcons)
// is off. The words come from HelpTexts, by key.

#include <QAbstractButton>

class QAction;
class QLabel;
class QMessageBox;
class QToolBar;

namespace bld::ui::theme {
class PrefsStore;
}

namespace bld::ui::help {

class HelpPopover;

class HelpButton : public QAbstractButton {
    Q_OBJECT
public:
    // `key` is a HelpTexts key; `target` is what "Show me" highlights (the
    // parent widget when there is none). Follows `store`'s helpIcons.
    HelpButton(const QString& key, theme::PrefsStore& store, QWidget* parent = nullptr, QWidget* target = nullptr);
    // The app's settings store.
    explicit HelpButton(const QString& key, QWidget* parent = nullptr, QWidget* target = nullptr);
    ~HelpButton() override;

    // Adds a "?" to `toolbar` (its action is what hides it there).
    static HelpButton* addTo(QToolBar* toolbar, const QString& key, QWidget* target = nullptr);

    QString key() const { return key_; }
    QWidget* target() const;

    // A toolbar hides its widgets through their actions: when set, the
    // help setting shows and hides `action` instead of the button.
    void setVisibilityAction(QAction* action);

    // The one-sentence tooltip, or null while it isn't showing.
    QLabel* tooltip() const;
    // The popover, or null while it isn't open.
    QWidget* popover() const;
    bool isPopoverOpen() const;

    void showPopover();
    void closePopover(bool refocus);
    // What "Show me" does.
    void showMe();

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void focusInEvent(QFocusEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    void changeEvent(QEvent* e) override;

private:
    void applyPrefs();
    void showTip();
    void hideTip();
    void placeFloating(QWidget* box) const;

    QString key_;
    theme::PrefsStore& store_;
    // Plain pointers throughout: CI's analyzer misreads QPointer's weak
    // reference as a use after free. The target and the toolbar action are
    // cleared when they are destroyed; the tooltip is a parentless window
    // this button owns; the popover is cleared when it is destroyed.
    QWidget* target_ = nullptr;
    QAction* visibilityAction_ = nullptr;
    QLabel* tip_ = nullptr;
    HelpPopover* popover_ = nullptr;
    bool hovered_ = false;
    // The tooltip came up for keyboard focus (so leaving with the mouse keeps it).
    bool focusTip_ = false;
    // Focus came from the keyboard (or back from the popover): draw the ring.
    bool focusRing_ = false;
    // Focus handed back after Esc shouldn't bring the tooltip straight back.
    bool quietFocus_ = false;
};

// `control` with its "?" just after it, in a new row widget that takes
// `control`'s place (add the row where `control` would have gone). The
// "?" explains `key` and "Show me" highlights `control`.
QWidget* withHelp(QWidget* control, const QString& key, QWidget* parent = nullptr);

// A dialog's heading (`text`, bold) with its "?" after it; "Show me"
// highlights `dialog`.
QWidget* headingWithHelp(const QString& text, const QString& key, QWidget* dialog);

// Puts a "?" at the top right of a message box's text (call it once the
// box's text, buttons and check box are set).
HelpButton* addToMessageBox(QMessageBox* box, const QString& key);

// Pulses the outline of `target` for a moment, so people can see which
// control a help text is about (tours will reuse it). Returns the overlay
// that draws the pulse; it deletes itself when done.
QWidget* pulseWidget(QWidget* target);

}  // namespace bld::ui::help
