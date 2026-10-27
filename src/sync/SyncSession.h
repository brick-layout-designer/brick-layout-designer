#pragma once

// One open live layout (sync phase P4): the shared document, its
// connection, and the rules the editor follows while connected.
//
// - The editor reloads its map from currentMap() on mapChanged: after the
//   first sync, when someone else edits, and after undo / redo.
// - After each of its own edits the editor calls localEdit(map); only the
//   difference is sent.
// - Edits made while not in step with the server are offline edits: kept
//   apart from the shared document, with the copy they started from, and
//   never merged silently. Back in step, offlineEditsReady() asks the
//   editor to resolve them (the compare window), and resolveOffline() puts
//   the result in as an ordinary edit.
// - With a cache folder, the shared layout and any offline edits are kept
//   on disk: the layout opens without a connection, and offline edits
//   survive a crash.
// - Remote changes wait while held (mid-drag) and arrive on release.
// - Undo / redo revert only this desktop's own edits.
// - A read-only session (view-only access) never sends edits.
// - Access that changes while open (setReadOnly): up to editor, edits flow
//   from then on; down to view only, the server keeps the connection but
//   drops our edits, so the document starts again from the server's copy.
//   Anything of ours the server lacks then (edits it dropped, offline
//   edits) is kept for saving: editsNotTaken(). Offline edits stay on disk
//   until then, so closing first doesn't lose them.

#include "Awareness.h"
#include "LayoutMerge.h"
#include "SyncClient.h"
#include "SyncDoc.h"

#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>
#include <optional>

class QTimer;

namespace bld::core { class Map; }

namespace bld::sync {

class SyncSession : public QObject {
    Q_OBJECT
public:
    explicit SyncSession(QObject* parent = nullptr);
    ~SyncSession() override;

    void open(const QUrl& socketUrl, const QString& token, bool readOnly);
    // Where this layout is kept on disk (call before open); empty for none.
    void setCacheDir(const QString& dir) { cacheDir_ = dir; }
    void close();

    SyncClient::Status status() const { return client_.status(); }
    bool readOnly() const { return readOnly_; }
    // Our access changed while open (see above).
    void setReadOnly(bool readOnly);
    // What the editor showed when access went down to view only, if the
    // server turned out to lack some of it (editsNotTaken); else nullopt.
    const std::optional<merge::Snapshot>& notTaken() const { return notTaken_; }
    // The first sync has delivered the layout.
    bool loaded() const { return loaded_; }
    // Offline edits waiting to be resolved.
    int unsyncedEdits() const { return offline_ ? offline_->count : 0; }

    struct OfflineEdits {
        merge::Snapshot base;  // the shared layout when the first offline edit was made
        merge::Snapshot mine;  // the editor's layout after the last one
        int count = 0;
    };
    const std::optional<OfflineEdits>& offlineEdits() const { return offline_; }
    // Ends the offline edits: `result` (a merge, or mine to replace the
    // server's) goes in as an ordinary edit; nullptr throws them away.
    void resolveOffline(const core::Map* result);

    // The shared layout as a desktop map; nullptr (and *error) when the
    // document can't be read.
    std::unique_ptr<core::Map> currentMap(QString* error = nullptr) const;
    // What the editor shows: the offline edits while there are any, else
    // the shared layout.
    std::unique_ptr<core::Map> editorMap(QString* error = nullptr) const;

    // The editor's layout after one of its own edits.
    void localEdit(const core::Map& map);

    bool undo();
    bool redo();
    bool canUndo() const { return !readOnly_ && !offline_ && doc_.canUndo(); }
    bool canRedo() const { return !readOnly_ && !offline_ && doc_.canRedo(); }

    void holdRemoteChanges(bool hold);

    // Presence: our state in the web editor's shape (user, cursor,
    // selection, tool, lastActivityMs), sent to everyone else; nullopt says
    // we left. Sent again whenever the connection comes back.
    void setPresence(const std::optional<QJsonObject>& state);
    // Everyone else's presence, by client id.
    const QHash<quint32, QJsonObject>& peers() const { return peers_.states(); }
    quint32 clientId() const { return clientId_; }

    // For tests: retry delays of the connection.
    SyncClient& client() { return client_; }

signals:
    void statusChanged(bld::sync::SyncClient::Status status);
    void mapChanged();
    void peersChanged();
    // Back in step with the server, with offline edits to resolve.
    void offlineEditsReady();
    // The server ended the session for good (access revoked, layout gone),
    // or the layout is in a form this version can't read (kUnreadableDocCode).
    void ended(int code, const QString& reason);
    // Access went down to view only and the server's copy lacks some of our
    // edits: they're in notTaken(), for saving.
    void editsNotTaken();

private:
    void remoteArrived();
    void saveDoc();
    void saveOffline();
    void loadCache();

    void sendPresence();
    void dropStalePeers();
    // Back in step after starting again read only: whether the server kept everything of ours.
    void checkNothingDropped();

    quint32 clientId_;
    quint32 presenceClock_ = 0;
    std::optional<QJsonObject> presence_;
    QTimer* renew_ = nullptr;
    awareness::Peers peers_;
    SyncDoc doc_;
    SyncClient client_;
    bool readOnly_ = false;
    bool loaded_ = false;
    bool held_ = false;
    bool pending_ = false;
    std::optional<OfflineEdits> offline_;
    QUrl socketUrl_;
    QString token_;
    // Started again read only: what we showed and held, until the server's copy is in.
    struct Before {
        merge::Snapshot shown;
        QByteArray state;
        bool offline = false;
    };
    std::optional<Before> before_;
    std::optional<merge::Snapshot> notTaken_;
    QString cacheDir_;
    QTimer* saveDocTimer_ = nullptr;
};

}  // namespace bld::sync
