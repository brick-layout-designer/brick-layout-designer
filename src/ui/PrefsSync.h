#pragma once

// Keeps the app's settings (PrefsStore) in step with the account on the
// server the desktop is connected to: pulls them on connecting, pushes
// changes made here (debounced), and the newest `updatedAt` wins.
//
// What each server last confirmed is kept per server host in QSettings
// (group "servers/<host>/prefs"), so more than one server can be added
// later without a rewrite.

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QUrl>

namespace bld::sync { class ServerApi; }

namespace bld::ui {

namespace theme { class PrefsStore; }

class PrefsSync : public QObject {
    Q_OBJECT
public:
    explicit PrefsSync(theme::PrefsStore& store, QObject* parent = nullptr);
    ~PrefsSync() override;

    // Connected to `server` with `token`: pull now, push local changes.
    void start(const QUrl& server, const QString& token);
    // Disconnected: stop pushing (a pending push is dropped).
    void stop();

    bool active() const { return !server_.isEmpty(); }
    // The connected server's host (with its port, if any), for "Synced
    // with <host>"; empty when not connected.
    QString host() const;

    // Milliseconds a change waits before it is pushed (more changes in
    // that time go together).
    void setPushDelay(int ms) { push_.setInterval(ms); }

    // The QSettings group for a server's record: "servers/<host[_port]>".
    static QString hostKey(const QUrl& server);
    // What the server last confirmed: its updatedAt and settings.
    static QDateTime syncedAt(const QUrl& server);
    static QJsonObject syncedPrefs(const QUrl& server);

signals:
    // Pulled (and taken in, or pushed back) or pushed.
    void synced();
    // `unauthorized`: the token can't read or change settings (an older
    // sign-in without the account:prefs scope; signing in again fixes it).
    void failed(const QString& message, bool unauthorized);

private:
    void onPulled(const QJsonObject& prefs, const QDateTime& updatedAt);
    void pushNow();
    void remember(const QJsonObject& prefs, const QDateTime& updatedAt);
    // What we send: our settings, less the keys this server doesn't know
    // yet (it refuses a save with an unknown key).
    QJsonObject outgoing() const;

    theme::PrefsStore& store_;
    sync::ServerApi* api_ = nullptr;
    QUrl server_;
    QTimer push_;
    bool pulling_ = false;
    // The server answered with partsIconSize, so it takes it (newer servers).
    bool knowsIconSize_ = false;
};

}  // namespace bld::ui
