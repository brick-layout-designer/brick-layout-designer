#pragma once

#include <QSet>

#include <optional>

// A layout open live from a collaborative server, shown in the MapView
// (sync phase P4). While it is open:
//
// - the map is reloaded from the shared document whenever it changes
//   (first sync, other people's edits, undo / redo), keeping the view's
//   zoom and scroll position;
// - each of the editor's own edits (an undo-stack step) goes to the server,
//   and the undo stack is emptied: Undo / Redo go through undo() / redo(),
//   which revert only this desktop's own edits;
// - others' changes wait while a mouse button is down on the map;
// - with view-only access, clicks on the map are ignored and any edit that
//   still happens (a menu command) is put back;
// - out of step with the server, edits are offline edits (SyncSession):
//   the map shows them, and back in step offlineEditsReady() asks for the
//   compare window.

#include "Presence.h"
#include "SyncSession.h"

#include <QObject>
#include <QString>
#include <QUrl>

class QGraphicsItem;
class QTimer;

namespace bld::ui {

class MapView;

class LiveLayout : public QObject {
    Q_OBJECT
public:
    explicit LiveLayout(MapView& view, QObject* parent = nullptr);
    ~LiveLayout() override;

    // `cacheDir` keeps the layout and offline edits on disk; empty for none.
    void open(const QUrl& socketUrl, const QString& token, bool readOnly, const QString& title,
              const QString& cacheDir = {});
    void close();
    bool active() const { return active_; }
    QString title() const { return title_; }
    bool readOnly() const { return session_.readOnly(); }

    bool undo();
    bool redo();
    bool canUndo() const { return session_.canUndo(); }
    bool canRedo() const { return session_.canRedo(); }

    // "Connected", "Reconnecting…", "Offline, 2 unsynced edits", plus
    // "view only".
    QString statusText() const;

    sync::SyncSession& session() { return session_; }

    // Who we are on this layout, for our cursor's name and color (the
    // color is the web's, per user and layout).
    void setUser(const QString& userId, const QString& displayName, const QString& layoutId);
    // Other people's cursors and selections drawn on the map right now.
    int drawnPeers() const { return drawnPeers_; }

signals:
    // The map was replaced: panels bound to the old one must re-bind.
    void mapReloaded();
    void statusTextChanged(const QString& text);
    void undoStateChanged();
    // The server ended the session (access revoked, layout deleted).
    void ended(const QString& reason);
    // Back in step with offline edits to resolve (SyncSession::resolveOffline).
    void offlineEditsReady();
    // One of this desktop's own edits went to the shared layout.
    void localEdited();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void reload();
    void onLocalStep();
    void schedulePresence();
    void publishPresence();
    void drawPeers();

    MapView& view_;
    sync::SyncSession session_;
    QString title_;
    bool active_ = false;
    bool reloading_ = false;
    bool clearing_ = false;
    bool reloadedOnce_ = false;
    // The parts on the map before an undo or redo, until its reload picks what came back.
    std::optional<QSet<QString>> partsBeforeStep_;
    sync::presence::User user_{ {}, QStringLiteral("Desktop"), QStringLiteral("#888888") };
    std::optional<QPointF> cursorStuds_;
    QTimer* presenceTimer_ = nullptr;
    QList<QGraphicsItem*> peerItems_;
    int drawnPeers_ = 0; // the first load fits the view; later ones keep it
};

} // namespace bld::ui
