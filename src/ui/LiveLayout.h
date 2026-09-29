#pragma once

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
//   still happens (a menu command) is put back.

#include "SyncSession.h"

#include <QObject>
#include <QString>
#include <QUrl>

namespace bld::ui {

class MapView;

class LiveLayout : public QObject {
    Q_OBJECT
public:
    explicit LiveLayout(MapView& view, QObject* parent = nullptr);
    ~LiveLayout() override;

    void open(const QUrl& socketUrl, const QString& token, bool readOnly, const QString& title);
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

signals:
    // The map was replaced: panels bound to the old one must re-bind.
    void mapReloaded();
    void statusTextChanged(const QString& text);
    void undoStateChanged();
    // The server ended the session (access revoked, layout deleted).
    void ended(const QString& reason);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void reload();
    void onLocalStep();

    MapView& view_;
    sync::SyncSession session_;
    QString title_;
    bool active_ = false;
    bool reloading_ = false;
    bool clearing_ = false;
    bool reloadedOnce_ = false; // the first load fits the view; later ones keep it
};

} // namespace bld::ui
