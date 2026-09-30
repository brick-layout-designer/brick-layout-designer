#include "SyncClient.h"

#include "ServerApi.h"

#include "SyncDoc.h"
#include "SyncProtocol.h"

#include <QNetworkRequest>

namespace bld::sync {

using protocol::Kind;

SyncClient::SyncClient(SyncDoc& doc, QObject* parent) : QObject(parent), doc_(doc) {
    retry_.setSingleShot(true);
    connect(&retry_, &QTimer::timeout, this, &SyncClient::connectSocket);
    connect(&socket_, &QWebSocket::connected, this, &SyncClient::onConnected);
    connect(&socket_, &QWebSocket::disconnected, this, &SyncClient::onDisconnected);
    connect(&socket_, &QWebSocket::binaryMessageReceived, this, &SyncClient::onMessage);
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
    // Revoked sign-in / unauthorized, or the layout is gone or no longer shared.
    if (code == 1008 || code == 4404) {
        wantOpen_ = false;
        setStatus(Status::Offline);
        emit closedForGood(code, reason);
        return;
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
        send(protocol::encode(Kind::SyncStep2, doc_.diffSince(msg->payload)));
        break;
    case Kind::SyncStep2:
    case Kind::Update:
        if (!doc_.applyUpdate(msg->payload)) return;
        emit remoteChange();
        // The answer to our step 1: we now have everything the server had.
        if (msg->kind == Kind::SyncStep2) setStatus(Status::Synced);
        break;
    case Kind::Awareness:
        emit awarenessReceived(msg->payload);
        break;
    }
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
