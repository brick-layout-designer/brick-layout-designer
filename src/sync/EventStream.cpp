#include "EventStream.h"
#include "ServerApi.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <algorithm>

namespace bld::sync {

QList<QJsonObject> EventStreamParser::feed(const QByteArray& bytes) {
    QList<QJsonObject> out;
    buffer_ += bytes;
    for (;;) {
        const qsizetype nl = buffer_.indexOf('\n');
        if (nl < 0) break;
        QByteArray line = buffer_.left(nl);
        buffer_.remove(0, nl + 1);
        if (line.endsWith('\r')) line.chop(1);
        if (line.isEmpty()) {
            // A blank line ends the message.
            if (!data_.isEmpty()) {
                const QJsonDocument doc = QJsonDocument::fromJson(data_);
                if (doc.isObject()) out << doc.object();
            }
            data_.clear();
            continue;
        }
        if (line.startsWith(':')) continue;  // a comment: the heartbeat
        const qsizetype colon = line.indexOf(':');
        const QByteArray field = colon < 0 ? line : line.left(colon);
        QByteArray value = colon < 0 ? QByteArray() : line.mid(colon + 1);
        if (value.startsWith(' ')) value.remove(0, 1);
        if (field == "data") {
            if (!data_.isEmpty()) data_ += '\n';
            data_ += value;
        } else if (field == "retry") {
            bool ok = false;
            const int ms = value.toInt(&ok);
            if (ok && ms > 0) retryMs_ = ms;
        }
    }
    return out;
}

EventStream::EventStream(QObject* parent) : QObject(parent) {
    retry_.setSingleShot(true);
    connect(&retry_, &QTimer::timeout, this, &EventStream::open);
}

EventStream::~EventStream() { stop(); }

void EventStream::setReconnectDelay(int firstMs, int maxMs) {
    firstDelayMs_ = firstMs;
    maxDelayMs_ = std::max(firstMs, maxMs);
    delayMs_ = firstMs;
}

void EventStream::start(const QUrl& base, const QString& token) {
    if (running_ && base == base_ && token == token_) return;
    stop();
    base_ = base;
    token_ = token;
    running_ = true;
    everConnected_ = false;
    delayMs_ = firstDelayMs_;
    open();
}

void EventStream::stop() {
    running_ = false;
    connected_ = false;
    retry_.stop();
    if (reply_) {
        QNetworkReply* r = reply_;
        reply_ = nullptr;
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
}

void EventStream::open() {
    if (!running_ || reply_) return;
    QUrl url = base_;
    url.setPath(QStringLiteral("/api/events"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    req.setRawHeader("Accept", "text/event-stream");
    req.setRawHeader("Cache-Control", "no-cache");
    if (!token_.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token_.toUtf8());
    // A stream never "finishes": no transfer timeout.
    req.setTransferTimeout(0);
    parser_ = EventStreamParser();
    reply_ = net_.get(req);
    connect(reply_, &QNetworkReply::readyRead, this, [this] {
        if (!reply_) return;
        const int status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) return;
        if (!connected_) {
            connected_ = true;
            delayMs_ = firstDelayMs_;
            if (everConnected_) emit reconnected();
            everConnected_ = true;
        }
        for (const QJsonObject& h : parser_.feed(reply_->readAll())) emit hint(h);
    });
    connect(reply_, &QNetworkReply::finished, this, &EventStream::onClosed);
}

void EventStream::onClosed() {
    if (!reply_) return;
    QNetworkReply* r = reply_;
    reply_ = nullptr;
    r->deleteLater();
    connected_ = false;
    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 401 || status == 403) {
        running_ = false;
        emit refused(status);
        return;
    }
    if (!running_) return;
    // The server's own "retry" first, then longer while it stays away.
    const int wait = std::max(delayMs_, parser_.retryMs() > 0 ? std::min(parser_.retryMs(), maxDelayMs_) : 0);
    delayMs_ = std::min(delayMs_ * 2, maxDelayMs_);
    retry_.start(wait);
}

}  // namespace bld::sync
