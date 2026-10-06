#include "SyncClient.h"

#include "Compat.h"
#include "ServerApi.h"
#include "SyncDoc.h"
#include "SyncProtocol.h"
#include "UpdateGuard.h"

#include <QDebug>
#include <QNetworkRequest>
#include <QWebSocketProtocol>

namespace bld::sync {

using protocol::Kind;

SyncClient::SyncClient(SyncDoc& doc, QObject* parent) : QObject(parent), doc_(doc) {
    retry_.setSingleShot(true);
    connect(&retry_, &QTimer::timeout, this, &SyncClient::connectSocket);
    connect(&socket_, &QWebSocket::connected, this, &SyncClient::onConnected);
    connect(&socket_, &QWebSocket::disconnected, this, &SyncClient::onDisconnected);
    connect(&socket_, &QWebSocket::binaryMessageReceived, this, &SyncClient::onMessage);
    // Qt drops a bigger message (closing with 1009) before buffering it all.
    // A few bytes over the limit for the message's own header.
    socket_.setMaxAllowedIncomingMessageSize(static_cast<quint64>(guard::kMaxUpdateBytes) + 64);
}

SyncClient::~SyncClient() {
    wantOpen_ = false;
    socket_.disconnect(this);
    socket_.abort();
}

void SyncClient::setReconnectDelays(std::chrono::milliseconds initial, std::chrono::milliseconds max) {
    initialDelay_ = delay_ = initial;
    maxDelay_ = max;
}

void SyncClient::open(const QUrl& url, const QString& bearerToken) {
    url_ = url;
    token_ = bearerToken;
    wantOpen_ = true;
    resyncAsked_ = false;
    closedForSize_ = false;
    delay_ = initialDelay_;
    connectSocket();
}

void SyncClient::close() {
    wantOpen_ = false;
    retry_.stop();
    socket_.close();
    setStatus(Status::Offline);
}

void SyncClient::connectSocket() {
    if (!wantOpen_) return;
    setStatus(Status::Connecting);
    QNetworkRequest req(url_);
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    // Tokens only ever travel in the header (the server ignores query tokens).
    if (!token_.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token_.toUtf8());
    socket_.open(req);
}

void SyncClient::onConnected() {
    delay_ = initialDelay_;
    // Our step 1 below asks for everything we're missing anyway.
    resyncAsked_ = false;
    setStatus(Status::Syncing);
    send(protocol::encode(Kind::SyncStep1, doc_.stateVector()));
}

void SyncClient::onDisconnected() {
    const int code = socket_.closeCode();
    const QString reason = socket_.closeReason();
    if (!wantOpen_) {
        setStatus(Status::Offline);
        return;
    }
    // Revoked sign-in / unauthorized, the layout is gone or no longer shared,
    // or the server's limit on people in one layout (retrying would only
    // be refused again; the person reopens it later).
    // 4426: this app is older than the server allows (update_required).
    if (code == 1008 || code == 4404 || code == 4426 || (code == 4429 && reason == QLatin1String("limit_reached"))) {
        wantOpen_ = false;
        setStatus(Status::Offline);
        emit closedForGood(code, reason);
        return;
    }
    // A message over the size limit (Qt closes with 1009 before reading it
    // all): reconnecting is the resync; the same again ends the session.
    if (code == QWebSocketProtocol::CloseCodeTooMuchData) {
        qWarning().noquote() << "Live layout: a message from the server is over the"
                             << guard::kMaxUpdateBytes / guard::kMiB << "MiB limit";
        if (closedForSize_) {
            wantOpen_ = false;
            setStatus(Status::Offline);
            emit closedForGood(kUnreadableUpdateCode, QStringLiteral("unreadable_update"));
            return;
        }
        closedForSize_ = true;
    }
    setStatus(Status::Connecting);
    retry_.start(delay_);
    delay_ = std::min(delay_ * 2, maxDelay_);
}

void SyncClient::onMessage(const QByteArray& message) {
    const auto msg = protocol::decode(message);
    if (!msg) return;  // unknown or malformed: ignored, like y-websocket
    switch (msg->kind) {
    case Kind::SyncStep1:
        // An unreadable state vector gets the whole document (SyncDoc::diffSince).
        if (const QString refused = guard::checkStateVector(msg->payload); !refused.isEmpty())
            qWarning().noquote() << "Live layout: the server's state vector was refused:" << refused;
        send(protocol::encode(Kind::SyncStep2, doc_.diffSince(msg->payload)));
        break;
    case Kind::SyncStep2:
    case Kind::Update: {
        QString error;
        if (!doc_.applyUpdate(msg->payload, &error)) {
            unreadableUpdate(msg->kind == Kind::SyncStep2, error);
            return;
        }
        emit remoteChange();
        // The answer to our step 1: we now have everything the server had.
        if (msg->kind == Kind::SyncStep2) {
            resyncAsked_ = false;
            closedForSize_ = false;
            setStatus(Status::Synced);
        }
        break;
    }
    case Kind::Awareness:
        emit awarenessReceived(msg->payload);
        break;
    }
}

void SyncClient::unreadableUpdate(bool syncStep2, const QString& why) {
    qWarning().noquote() << "Live layout: an update from the server couldn't be read:" << why;
    if (!resyncAsked_) {
        // Ask once for the whole layout again (a state vector of no clients):
        // a damaged relay of one change is fixed by the server's own copy.
        resyncAsked_ = true;
        send(protocol::encode(Kind::SyncStep1, QByteArray(1, '\0')));
        return;
    }
    // A change arriving while the resync is on its way is dropped: the
    // server's whole layout, the answer, decides.
    if (!syncStep2) return;
    wantOpen_ = false;
    retry_.stop();
    socket_.close();
    setStatus(Status::Offline);
    emit closedForGood(kUnreadableUpdateCode, QStringLiteral("unreadable_update"));
}

void SyncClient::sendLocal(const QByteArray& update) {
    if (update.isEmpty()) return;
    send(protocol::encode(Kind::Update, update));
}

void SyncClient::sendAwareness(const QByteArray& awarenessUpdate) {
    send(protocol::encode(Kind::Awareness, awarenessUpdate));
}

void SyncClient::send(const QByteArray& message) {
    if (socket_.state() == QAbstractSocket::ConnectedState) socket_.sendBinaryMessage(message);
}

void SyncClient::setStatus(Status s) {
    if (s == status_) return;
    status_ = s;
    emit statusChanged(s);
}

}  // namespace bld::sync
