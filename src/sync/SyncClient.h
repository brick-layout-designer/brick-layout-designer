#pragma once

// Keeps a SyncDoc in step with a collaborative server over its
// y-websocket endpoint (wss://host/ws/layout/<id>), authenticated with a
// desktop API token. Sync phase P3c.
//
// On connect both sides send sync step 1 and answer the other's, which
// also delivers edits made while offline (they are in the local doc, so
// nothing is queued). Afterwards remote updates are applied as they come
// and local ones (SyncDoc::writeMap's result) sent with sendLocal. Lost
// connections are retried with backoff; a revoked sign-in (1008) or a
// deleted / no longer shared layout (4404) ends the session.
//
// An update that can't be read (damaged, or refused by UpdateGuard before
// yrs sees it) is logged and dropped, never half applied, and the client
// asks the server once for the whole layout again. If that can't be read
// either, the session ends with kUnreadableUpdateCode.

#include <QByteArray>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

#include <chrono>

namespace bld::sync {

class SyncDoc;

class SyncClient : public QObject {
    Q_OBJECT
public:
    enum class Status {
        Offline,     // not connected (before open, after close or a final error)
        Connecting,  // opening, or waiting to retry
        Syncing,     // connected, exchanging what each side is missing
        Synced,      // up to date; changes flow both ways
    };
    Q_ENUM(Status)

    explicit SyncClient(SyncDoc& doc, QObject* parent = nullptr);
    ~SyncClient() override;

    void open(const QUrl& url, const QString& bearerToken);
    void close();

    // Send a local change (from SyncDoc::writeMap). While not connected it
    // needs no queueing: the next handshake carries it.
    void sendLocal(const QByteArray& update);
    void sendAwareness(const QByteArray& awarenessUpdate);

    Status status() const { return status_; }

    // Retry backoff: `initial`, doubling up to `max`.
    void setReconnectDelays(std::chrono::milliseconds initial, std::chrono::milliseconds max);
    // For tests: the largest message accepted (UpdateGuard's limit otherwise).
    void setMaxMessageSize(quint64 bytes) { socket_.setMaxAllowedIncomingMessageSize(bytes); }

signals:
    void statusChanged(bld::sync::SyncClient::Status status);
    // The document changed from the network; re-read it.
    void remoteChange();
    void awarenessReceived(const QByteArray& awarenessUpdate);
    // The server ended the session for good (1008, 4404), or the layout's
    // updates can't be read (kUnreadableUpdateCode): no more retries.
    void closedForGood(int code, const QString& reason);

private:
    void setStatus(Status s);
    void connectSocket();
    void onConnected();
    void onDisconnected();
    void onMessage(const QByteArray& message);
    void unreadableUpdate(bool syncStep2, const QString& why);
    void send(const QByteArray& message);

    SyncDoc& doc_;
    QWebSocket socket_;
    QTimer retry_;
    QUrl url_;
    QString token_;
    Status status_ = Status::Offline;
    bool wantOpen_ = false;
    // A full resync asked for after an unreadable update, not answered yet.
    bool resyncAsked_ = false;
    // The connection closed for a message over the size limit; once more ends the session.
    bool closedForSize_ = false;
    std::chrono::milliseconds initialDelay_{ 1000 };
    std::chrono::milliseconds maxDelay_{ 30000 };
    std::chrono::milliseconds delay_{ 1000 };
};

}  // namespace bld::sync
