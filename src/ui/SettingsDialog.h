#pragma once

// Edit > Settings...: light or dark, the colour, bigger text and the help
// buttons, as the web's Settings page has them. (The synced settings also
// carry expertMode, kept so it round-trips with the server; the desktop
// shows every feature, so it has no switch for it.) Every change
// applies at once through the PrefsStore (the theme follows, PrefsSync
// pushes it). The older, detailed options stay in Preferences, one click
// away.

#include <QDialog>

#include <functional>

class QLabel;

namespace bld::ui {

namespace theme { class PrefsStore; }

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    // `syncedHost`: the host of the server these settings sync with right
    // now, or empty when not connected. `openMoreOptions` opens the
    // Preferences dialog (the button is left out when it is empty).
    SettingsDialog(theme::PrefsStore& store, const QString& syncedHost,
                   const std::function<void()>& openMoreOptions = {}, QWidget* parent = nullptr);

    // The note under the options: synced with the host, or kept here.
    QLabel* syncNote() const { return syncNote_; }

private:
    void load();
    theme::PrefsStore& store_;
    QLabel* syncNote_ = nullptr;
    bool loading_ = false;
};

}  // namespace bld::ui
