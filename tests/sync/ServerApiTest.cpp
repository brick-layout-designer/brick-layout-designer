// ServerApi (sync phase P4) against a small scripted HTTP server: address
// rules, the version check, device sign-in with its polling rules
// (authorization_pending, slow_down, access_denied, expiry), and the
// layout list with the token in the Authorization header.

#include "ServerApi.h"
#include "FakeHttp.h"
#include "saveload/VenueIO.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimeZone>

#include <deque>
#include <functional>

using namespace bld::sync;
using bld::synctest::FakeHttp;

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

TEST(ServerApi, PullsVenuesAsFilesTheVenueLibraryReads) {
    FakeHttp http;
    ServerApi api;
    api.setBase(http.base());
    api.setToken(QStringLiteral("bld_pat_abc"));
    QList<VenueEntry> venues;
    QString gotId, gotName;
    QByteArray file;
    QString failedWhat;
    bool unauthorized = false;
    QObject::connect(&api, &ServerApi::venuesReady, [&](const QList<VenueEntry>& v) { venues = v; });
    QObject::connect(&api, &ServerApi::venueReady, [&](const QString& id, const QString& name, const QByteArray& f) {
        gotId = id;
        gotName = name;
        file = f;
    });
    QObject::connect(&api, &ServerApi::requestFailed, [&](const QString& w, const QString&, bool u) {
        failedWhat = w;
        unauthorized = u;
    });

    http.reply("/api/venues", 200, { { QStringLiteral("venues"), QJsonArray{
        QJsonObject{ { QStringLiteral("id"), QStringLiteral("v1") }, { QStringLiteral("name"), QStringLiteral("Grand Lobby") },
                     { QStringLiteral("ownerOrgId"), QStringLiteral("org1") } },
        QJsonObject{ { QStringLiteral("id"), QStringLiteral("v2") }, { QStringLiteral("name"), QStringLiteral("Garage") },
                     { QStringLiteral("ownerOrgId"), QJsonValue::Null } } } } });
    api.fetchVenues();
    ASSERT_TRUE(waitFor([&] { return venues.size() == 2; }));
    EXPECT_EQ(venues[0].name, QStringLiteral("Grand Lobby"));
    EXPECT_EQ(venues[0].ownerOrgId, QStringLiteral("org1"));
    EXPECT_TRUE(venues[1].ownerOrgId.isEmpty());
    EXPECT_EQ(http.requests.back().authorization, QByteArray("Bearer bld_pat_abc"));

    // The web stores the venue without the file's schema tag.
    const QJsonObject data{
        { QStringLiteral("name"), QStringLiteral("Grand Lobby") }, { QStringLiteral("enabled"), true },
        { QStringLiteral("minWalkwayStuds"), 30 },
        { QStringLiteral("bounds"), QJsonObject{ { QStringLiteral("x"), 0 }, { QStringLiteral("y"), 0 }, { QStringLiteral("w"), 0 }, { QStringLiteral("h"), 0 } } },
        { QStringLiteral("edges"), QJsonArray{ QJsonObject{
            { QStringLiteral("kind"), 2 }, { QStringLiteral("doorWidthStuds"), 0 }, { QStringLiteral("label"), QStringLiteral("to the Lobby") },
            { QStringLiteral("poly"), QJsonArray{ QJsonObject{ { QStringLiteral("x"), 0 }, { QStringLiteral("y"), 0 } },
                                                  QJsonObject{ { QStringLiteral("x"), 100 }, { QStringLiteral("y"), 0 } } } } } } },
        { QStringLiteral("obstacles"), QJsonArray{ QJsonObject{
            { QStringLiteral("label"), QStringLiteral("stairs") },
            { QStringLiteral("poly"), QJsonArray{ QJsonObject{ { QStringLiteral("x"), 1 }, { QStringLiteral("y"), 1 } },
                                                  QJsonObject{ { QStringLiteral("x"), 2 }, { QStringLiteral("y"), 2 } } } } } } } };
    http.reply("/api/venues/v1", 200, { { QStringLiteral("id"), QStringLiteral("v1") }, { QStringLiteral("name"), QStringLiteral("Grand Lobby") },
                                        { QStringLiteral("data"), data } });
    api.fetchVenue(QStringLiteral("v1"));
    ASSERT_TRUE(waitFor([&] { return !file.isEmpty(); }));
    EXPECT_EQ(gotId, QStringLiteral("v1"));
    EXPECT_EQ(gotName, QStringLiteral("Grand Lobby"));
    EXPECT_EQ(QJsonDocument::fromJson(file).object().value(QLatin1String("schema")).toString(), QStringLiteral("bld-venue/1"));
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("Grand Lobby.bld-venue"));
    {
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(file);
    }
    const auto v = bld::saveload::readVenueFile(path);
    ASSERT_TRUE(v);
    EXPECT_EQ(v->name, QStringLiteral("Grand Lobby"));
    EXPECT_DOUBLE_EQ(v->minWalkwayStuds, 30.0);
    ASSERT_EQ(v->edges.size(), 1);
    EXPECT_EQ(v->edges[0].kind, bld::core::EdgeKind::Open);
    EXPECT_EQ(v->edges[0].polyline.size(), 2);
    ASSERT_EQ(v->obstacles.size(), 1);
    EXPECT_EQ(v->obstacles[0].label, QStringLiteral("stairs"));

    // A token without venues:read gets 403: sign in again with that scope.
    http.reply("/api/venues/v2", 403, { { QStringLiteral("error"), QStringLiteral("insufficient_scope") } });
    api.fetchVenue(QStringLiteral("v2"));
    ASSERT_TRUE(waitFor([&] { return !failedWhat.isEmpty(); }));
    EXPECT_EQ(failedWhat, QStringLiteral("venue"));
    EXPECT_TRUE(unauthorized);
}

TEST(ServerApi, AsksWhoTheTokenBelongsTo) {
    FakeHttp http;
    ServerApi api;
    api.setBase(http.base());
    api.setToken(QStringLiteral("bld_pat_abc"));
    QString id, name;
    QObject::connect(&api, &ServerApi::currentUserReady, [&](const QString& i, const QString& n) {
        id = i;
        name = n;
    });
    http.reply("/api/tokens/current", 200,
               { { QStringLiteral("user"),
                   QJsonObject{ { QStringLiteral("id"), QStringLiteral("u1") },
                                { QStringLiteral("displayName"), QStringLiteral("Aaron") } } } });
    api.fetchCurrentUser();
    ASSERT_TRUE(waitFor([&] { return !id.isEmpty(); }));
    EXPECT_EQ(name, QStringLiteral("Aaron"));
    EXPECT_EQ(http.requests.back().authorization, QByteArray("Bearer bld_pat_abc"));
}
