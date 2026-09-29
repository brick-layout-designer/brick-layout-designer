// ServerApi (sync phase P4) against a small scripted HTTP server: address
// rules, the version check, device sign-in with its polling rules
// (authorization_pending, slow_down, access_denied, expiry), and the
// layout list with the token in the Authorization header.

#include "ServerApi.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimeZone>

#include <deque>
#include <functional>

using namespace bld::sync;

namespace {

bool waitFor(const std::function<bool()>& done, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

struct Request {
    QByteArray method, path, authorization, body;
    qint64 atMs = 0;  // when it arrived
};

// Answers each request from a per-path queue of (status, JSON) replies.
class FakeHttp : public QObject {
public:
    FakeHttp() {
        clock_.start();
        server_.listen(QHostAddress::LocalHost, 0);
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = server_.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, this, [this, s] { onData(s); });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }
    QUrl base() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort())); }
    void reply(const QByteArray& path, int status, const QJsonObject& body) { replies_[path].push_back({ status, body }); }

    std::vector<Request> requests;

private:
    void onData(QTcpSocket* s) {
        QByteArray& buf = pending_[s];
        buf += s->readAll();
        const qsizetype headerEnd = buf.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        const QList<QByteArray> lines = buf.left(headerEnd).split('\n');
        Request req;
        const QList<QByteArray> first = lines[0].trimmed().split(' ');
        req.method = first.value(0);
        req.path = first.value(1);
        qsizetype length = 0;
        for (const QByteArray& l : lines.mid(1)) {
            const qsizetype colon = l.indexOf(':');
            const QByteArray name = l.left(colon).trimmed().toLower(), value = l.mid(colon + 1).trimmed();
            if (name == "content-length") length = value.toLongLong();
            if (name == "authorization") req.authorization = value;
        }
        if (buf.size() < headerEnd + 4 + length) return;
        req.body = buf.mid(headerEnd + 4, length);
        req.atMs = clock_.elapsed();
        pending_.remove(s);
        requests.push_back(req);
        auto& q = replies_[req.path];
        const auto [status, json] = q.empty() ? std::pair{ 404, QJsonObject{ { QStringLiteral("error"), QStringLiteral("not_found") } } } : q.front();
        if (q.size() > 1) q.pop_front();  // the last reply repeats
        const QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);
        s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: application/json\r\nContent-Length: " +
                 QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        s->disconnectFromHost();
    }

    QTcpServer server_;
    QElapsedTimer clock_;
    QHash<QTcpSocket*, QByteArray> pending_;
    QHash<QByteArray, std::deque<std::pair<int, QJsonObject>>> replies_;
};

}  // namespace

TEST(ServerApi, AddressesNeedHttpsExceptOnThisMachine) {
    QString err;
    EXPECT_EQ(ServerApi::normalizeBase(QStringLiteral("layouts.example.org"), &err), QUrl(QStringLiteral("https://layouts.example.org")));
    EXPECT_EQ(ServerApi::normalizeBase(QStringLiteral(" https://x.org:8443/editor/1?a=b "), &err), QUrl(QStringLiteral("https://x.org:8443")));
    EXPECT_TRUE(ServerApi::normalizeBase(QStringLiteral("http://localhost:3000"), &err));
    EXPECT_TRUE(ServerApi::normalizeBase(QStringLiteral("http://127.0.0.1:3000"), &err));
    EXPECT_TRUE(ServerApi::normalizeBase(QStringLiteral("http://[::1]:3000"), &err));
    EXPECT_FALSE(ServerApi::normalizeBase(QStringLiteral("http://x.org"), &err));
    EXPECT_TRUE(err.contains(QStringLiteral("https://")));
    EXPECT_FALSE(ServerApi::normalizeBase(QStringLiteral("ftp://x.org"), &err));
    EXPECT_FALSE(ServerApi::normalizeBase(QString(), &err));

    ServerApi api;
    api.setBase(QUrl(QStringLiteral("https://x.org:8443")));
    EXPECT_EQ(api.layoutSocketUrl(QStringLiteral("abc-1")), QUrl(QStringLiteral("wss://x.org:8443/ws/layout/abc-1")));
    api.setBase(QUrl(QStringLiteral("http://localhost:3000")));
    EXPECT_EQ(api.layoutSocketUrl(QStringLiteral("abc-1")), QUrl(QStringLiteral("ws://localhost:3000/ws/layout/abc-1")));
}

TEST(ServerApi, ChecksTheServerSpeaksOurProtocolAndSchema) {
    FakeHttp http;
    ServerApi api;
    api.setBase(http.base());
    std::optional<ServerInfo> info;
    QObject::connect(&api, &ServerApi::versionReady, [&](const ServerInfo& i) { info = i; });
    http.reply("/api/version", 200, { { QStringLiteral("version"), QStringLiteral("1.4.0") }, { QStringLiteral("schemaVersion"), 1 },
                                      { QStringLiteral("protocols"), QJsonArray{ QStringLiteral("y-websocket/1") } } });
    api.fetchVersion();
    ASSERT_TRUE(waitFor([&] { return info.has_value(); }));
    EXPECT_EQ(info->version, QStringLiteral("1.4.0"));
    EXPECT_TRUE(info->compatible());
    // A newer document schema, or no shared protocol: not compatible.
    EXPECT_FALSE((ServerInfo{ {}, 2, { QStringLiteral("y-websocket/1") } }).compatible());
    EXPECT_FALSE((ServerInfo{ {}, 1, { QStringLiteral("y-websocket/2") } }).compatible());
}

TEST(ServerApi, DeviceSignInPollsUntilApproved) {
    FakeHttp http;
    ServerApi api;
    api.setBase(http.base());
    api.setPollIntervalScale(5);  // 1 "second" = 5 ms
    std::optional<DeviceCode> code;
    QString token;
    QObject::connect(&api, &ServerApi::signInCode, [&](const DeviceCode& c) { code = c; });
    QObject::connect(&api, &ServerApi::signedIn, [&](const QString& t) { token = t; });
    http.reply("/api/auth/device/code", 200, {
        { QStringLiteral("device_code"), QStringLiteral("dev-123") }, { QStringLiteral("user_code"), QStringLiteral("BCDF-GHJK") },
        { QStringLiteral("verification_uri"), QStringLiteral("https://x.org/device") },
        { QStringLiteral("verification_uri_complete"), QStringLiteral("https://x.org/device?user_code=BCDF-GHJK") },
        { QStringLiteral("expires_in"), 600 }, { QStringLiteral("interval"), 1 } });
    http.reply("/api/auth/device/token", 400, { { QStringLiteral("error"), QStringLiteral("authorization_pending") } });
    http.reply("/api/auth/device/token", 400, { { QStringLiteral("error"), QStringLiteral("slow_down") } });
    http.reply("/api/auth/device/token", 400, { { QStringLiteral("error"), QStringLiteral("slow_down") }, { QStringLiteral("interval"), 20 } });
    http.reply("/api/auth/device/token", 200, { { QStringLiteral("access_token"), QStringLiteral("bld_pat_abc") }, { QStringLiteral("token_type"), QStringLiteral("Bearer") } });

    api.startSignIn(QStringLiteral("Brick Layout Designer (test)"));
    ASSERT_TRUE(waitFor([&] { return !token.isEmpty(); }));
    EXPECT_EQ(token, QStringLiteral("bld_pat_abc"));
    ASSERT_TRUE(code);
    EXPECT_EQ(code->userCode, QStringLiteral("BCDF-GHJK"));
    EXPECT_EQ(code->verificationUriComplete, QUrl(QStringLiteral("https://x.org/device?user_code=BCDF-GHJK")));
    // The code request names the app; each poll sends the device code with the RFC grant type.
    const QJsonObject first = QJsonDocument::fromJson(http.requests.front().body).object();
    EXPECT_EQ(first.value(QLatin1String("client_name")).toString(), QStringLiteral("Brick Layout Designer (test)"));
    int polls = 0;
    std::vector<qint64> at;
    for (const auto& r : http.requests) {
        if (r.path != "/api/auth/device/token") continue;
        ++polls;
        at.push_back(r.atMs);
        const QJsonObject b = QJsonDocument::fromJson(r.body).object();
        EXPECT_EQ(b.value(QLatin1String("device_code")).toString(), QStringLiteral("dev-123"));
        EXPECT_EQ(b.value(QLatin1String("grant_type")).toString(), QStringLiteral("urn:ietf:params:oauth:grant-type:device_code"));
    }
    EXPECT_EQ(polls, 4);
    // slow_down adds 5 s to the wait (1 + 5 = 6 "seconds" = 30 ms here), or
    // takes the server's interval when that is longer (20 = 100 ms).
    ASSERT_EQ(at.size(), 4u);
    EXPECT_GE(at[2] - at[1], 25);
    EXPECT_GE(at[3] - at[2], 95);
}

TEST(ServerApi, DeviceSignInStopsWhenDeniedOrExpired) {
    for (const bool denied : { true, false }) {
        FakeHttp http;
        ServerApi api;
        api.setBase(http.base());
        api.setPollIntervalScale(5);
        QString failure;
        QObject::connect(&api, &ServerApi::signInFailed, [&](const QString& r) { failure = r; });
        http.reply("/api/auth/device/code", 200, { { QStringLiteral("device_code"), QStringLiteral("d") }, { QStringLiteral("user_code"), QStringLiteral("U") },
                                                   { QStringLiteral("expires_in"), denied ? 600 : 3 }, { QStringLiteral("interval"), 1 } });
        http.reply("/api/auth/device/token", 400, { { QStringLiteral("error"), denied ? QStringLiteral("access_denied") : QStringLiteral("authorization_pending") } });
        api.startSignIn(QStringLiteral("app"));
        ASSERT_TRUE(waitFor([&] { return !failure.isEmpty(); }));
        EXPECT_EQ(failure, denied ? QStringLiteral("access_denied") : QStringLiteral("expired_token"));
    }
}

TEST(ServerApi, ListsLayoutsWithTheTokenAndReportsAnExpiredOne) {
    FakeHttp http;
    ServerApi api;
    api.setBase(http.base());
    api.setToken(QStringLiteral("bld_pat_abc"));
    QList<LayoutEntry> layouts;
    bool got = false;
    QObject::connect(&api, &ServerApi::layoutsReady, [&](const QList<LayoutEntry>& l) { layouts = l; got = true; });
    http.reply("/api/layouts", 200, { { QStringLiteral("layouts"), QJsonArray{
        QJsonObject{ { QStringLiteral("id"), QStringLiteral("L1") }, { QStringLiteral("title"), QStringLiteral("Show 2026") },
                     { QStringLiteral("ownerOrgName"), QStringLiteral("Club") }, { QStringLiteral("role"), QStringLiteral("editor") },
                     { QStringLiteral("updatedAt"), QStringLiteral("2026-09-29T10:00:00.000Z") } } } } });
    api.fetchLayouts();
    ASSERT_TRUE(waitFor([&] { return got; }));
    ASSERT_EQ(layouts.size(), 1);
    EXPECT_EQ(layouts[0].title, QStringLiteral("Show 2026"));
    EXPECT_EQ(layouts[0].ownerOrgName, QStringLiteral("Club"));
    EXPECT_EQ(layouts[0].role, QStringLiteral("editor"));
    EXPECT_EQ(layouts[0].updatedAt, QDateTime(QDate(2026, 9, 29), QTime(10, 0), QTimeZone::UTC));
    EXPECT_EQ(http.requests.back().authorization, QByteArray("Bearer bld_pat_abc"));

    bool unauthorized = false;
    QObject::connect(&api, &ServerApi::requestFailed, [&](const QString&, const QString&, bool u) { unauthorized = u; got = true; });
    got = false;
    FakeHttp other;  // a server that no longer accepts the token
    other.reply("/api/layouts", 401, { { QStringLiteral("error"), QStringLiteral("invalid_token") } });
    api.setBase(other.base());
    api.fetchLayouts();
    ASSERT_TRUE(waitFor([&] { return got; }));
    EXPECT_TRUE(unauthorized);
}
