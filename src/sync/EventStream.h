#pragma once

// The server's live hints (GET /api/events, Server-Sent Events), for the
// panels that stay open: the Module library and your notices. Each hint
// only says that something changed ({kind, id, action}: "module",
// "catalog", "club", "warning", …); the panel asks the server again. The
// stream reconnects by itself (after the server's "retry", then longer
// while the server stays away) until stop().

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QUrl>

class QNetworkReply;

namespace bld::sync {

// Splits an event stream into its messages' data, as JSON objects. Comments
// (": ping"), other fields and data that isn't a JSON object are skipped.
class EventStreamParser {
public:
    QList<QJsonObject> feed(const QByteArray& bytes);
    // The last "retry:" the server asked for, in ms (0: none).
    int retryMs() const { return retryMs_; }

private:
    QByteArray buffer_;
    QByteArray data_;
    int retryMs_ = 0;
};

class EventStream : public QObject {
    Q_OBJECT
public:
    explicit EventStream(QObject* parent = nullptr);
    ~EventStream() override;

    void start(const QUrl& base, const QString& token);
    void stop();
    bool running() const { return running_; }
    bool connected() const { return connected_; }
    // The wait before the next reconnect, in ms (tests shorten it).
    void setReconnectDelay(int firstMs, int maxMs);

signals:
    void hint(const QJsonObject& hint);
    // The stream opened again after being away: hints may have been missed.
    void reconnected();
    // A 401 or 403: the sign-in no longer works here.
    void refused(int status);

private:
    void open();
    void onClosed();

    QNetworkAccessManager net_;
    QNetworkReply* reply_ = nullptr;
    QTimer retry_;
    QUrl base_;
    QString token_;
    EventStreamParser parser_;
    bool running_ = false;
    bool connected_ = false;
    bool everConnected_ = false;
    int firstDelayMs_ = 5000;
    int maxDelayMs_ = 60000;
    int delayMs_ = 5000;
};

}  // namespace bld::sync
