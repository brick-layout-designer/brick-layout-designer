#pragma once

// One open live layout (sync phase P4): the shared document, its
// connection, and the rules the editor follows while connected.
//
// - The editor reloads its map from currentMap() on mapChanged: after the
//   first sync, when someone else edits, and after undo / redo.
// - After each of its own edits the editor calls localEdit(map); only the
//   difference is sent.
// - Remote changes wait while held (mid-drag) and arrive on release.
// - Undo / redo revert only this desktop's own edits.
// - A read-only session (view-only access) never sends edits.

#include "Awareness.h"
#include "SyncClient.h"
#include "SyncDoc.h"

#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>

namespace bld::core { class Map; }

namespace bld::sync {

class SyncSession : public QObject {
    Q_OBJECT
public:
    explicit SyncSession(QObject* parent = nullptr);
    ~SyncSession() override;

    void open(const QUrl& socketUrl, const QString& token, bool readOnly);
    void close();

    SyncClient::Status status() const { return client_.status(); }
    bool readOnly() const { return readOnly_; }
    // The first sync has delivered the layout.
    bool loaded() const { return loaded_; }
    // Edits made since the connection was last in step (sent on reconnect).
    int unsyncedEdits() const { return unsynced_; }

    // The shared layout as a desktop map; nullptr (and *error) when the
    // document can't be read.
    std::unique_ptr<core::Map> currentMap(QString* error = nullptr) const;

    // The editor's layout after one of its own edits.
    void localEdit(const core::Map& map);

    bool undo();
    bool redo();
    bool canUndo() const { return !readOnly_ && doc_.canUndo(); }
    bool canRedo() const { return !readOnly_ && doc_.canRedo(); }

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
    // The server ended the session for good (access revoked, layout gone).
    void ended(int code, const QString& reason);

private:
    void remoteArrived();

    void sendPresence();

    quint32 clientId_;
    quint32 presenceClock_ = 0;
    std::optional<QJsonObject> presence_;
    awareness::Peers peers_;
    SyncDoc doc_;
    SyncClient client_;
    bool readOnly_ = false;
    bool loaded_ = false;
    bool held_ = false;
    bool pending_ = false;
    int unsynced_ = 0;
};

}  // namespace bld::sync
