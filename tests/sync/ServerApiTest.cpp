// ServerApi (sync phase P4) against a small scripted HTTP server: address
// rules, the version check, device sign-in with its polling rules
// (authorization_pending, slow_down, access_denied, expiry), and the
// layout list with the token in the Authorization header.

#include "LibraryApi.h"
#include "ServerApi.h"
#include "ServerRefusal.h"
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
#include <optional>
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
    const auto server = [](int schemaVersion, const QString& protocol) {
        ServerInfo s;
        s.schemaVersion = schemaVersion;
        s.protocols = { protocol };
        return s;
    };
    EXPECT_FALSE(server(2, QStringLiteral("y-websocket/1")).compatible());
    EXPECT_FALSE(server(1, QStringLiteral("y-websocket/2")).compatible());
}

TEST(ServerApi, ReadsTheDesktopVersionsAndFeaturesAServerWorksWith) {
    FakeHttp http;
    ServerApi api;
    api.setBase(http.base());
    std::optional<ServerInfo> info;
    QObject::connect(&api, &ServerApi::versionReady, [&](const ServerInfo& i) { info = i; });
    http.reply("/api/version", 200,
               { { QStringLiteral("version"), QStringLiteral("nightly-abc") },
                 { QStringLiteral("schemaVersion"), 1 },
                 { QStringLiteral("protocols"), QJsonArray{ QStringLiteral("y-websocket/1") } },
                 { QStringLiteral("desktop"),
                   QJsonObject{ { QStringLiteral("minimum"), QStringLiteral("1.2.0") },
                                { QStringLiteral("recommended"), QStringLiteral("1.3.0") },
                                { QStringLiteral("downloadUrl"), QStringLiteral("https://example.org/get") } } },
                 { QStringLiteral("doc"), QJsonObject{ { QStringLiteral("schemaVersion"), 1 }, { QStringLiteral("minReadable"), 1 } } },
                 { QStringLiteral("features"), QJsonArray{ QStringLiteral("liveSync"), QStringLiteral("venues") } } });
    api.fetchVersion();
    ASSERT_TRUE(waitFor([&] { return info.has_value(); }));
    EXPECT_EQ(info->desktopMinimum, QStringLiteral("1.2.0"));
    EXPECT_EQ(info->desktopRecommended, QStringLiteral("1.3.0"));
    EXPECT_EQ(info->downloadUrl, QStringLiteral("https://example.org/get"));
    EXPECT_EQ(info->standing(QStringLiteral("1.1.0")), Standing::UpdateRequired);
    EXPECT_EQ(info->standing(QStringLiteral("1.2.0")), Standing::UpdateSuggested);
    EXPECT_EQ(info->standing(QStringLiteral("1.3.0")), Standing::Ok);
    EXPECT_TRUE(info->has(QStringLiteral("venues")));
    EXPECT_FALSE(info->has(QStringLiteral("preferences")));
    EXPECT_TRUE(info->missing().contains(QStringLiteral("preferences")));
    EXPECT_FALSE(info->missing().contains(QStringLiteral("venues")));
    // No "limits" listed: this server doesn't refuse with limit_reached.
    EXPECT_FALSE(info->hasLimits());
    ServerInfo limited = *info;
    limited.features->append(QStringLiteral("limits"));
    EXPECT_TRUE(limited.hasLimits());
    EXPECT_FALSE(ServerInfo{}.hasLimits());
    // A server that reads only newer documents than this build writes.
    ServerInfo strict = *info;
    strict.docMinReadable = kDocSchemaVersion + 1;
    EXPECT_FALSE(strict.compatible());
    // A server from before these checks: nothing asked, everything assumed there.
    ServerInfo old;
    old.schemaVersion = 1;
    old.protocols = { QStringLiteral("y-websocket/1") };
    EXPECT_EQ(old.standing(QStringLiteral("0.1.0")), Standing::Ok);
    EXPECT_TRUE(old.has(QStringLiteral("venues")));
    EXPECT_TRUE(old.compatible());
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

TEST(LibraryApi, PullsVenuesWithTheirSizesAsFilesTheVenueLibraryReads) {
    FakeHttp http;
    LibraryApi api;
    api.setBase(http.base());
    api.setToken(QStringLiteral("bld_pat_abc"));
    QList<VenueEntry> venues;
    QString gotName;
    QByteArray file;
    std::optional<ServerRefusal> refused;

    http.reply("/api/venues", 200, { { QStringLiteral("venues"), QJsonArray{
        QJsonObject{ { QStringLiteral("id"), QStringLiteral("v1") }, { QStringLiteral("name"), QStringLiteral("Grand Lobby") },
                     { QStringLiteral("ownerOrgId"), QStringLiteral("org1") }, { QStringLiteral("canManage"), false },
                     { QStringLiteral("createdAt"), 1790000000000.0 }, { QStringLiteral("widthStuds"), 1250 },
                     { QStringLiteral("heightStuds"), 625 } },
        // A server before sizes and rights.
        QJsonObject{ { QStringLiteral("id"), QStringLiteral("v2") }, { QStringLiteral("name"), QStringLiteral("Garage") },
                     { QStringLiteral("ownerOrgId"), QJsonValue::Null } } } } });
    api.venues([&](const QList<VenueEntry>& v) { venues = v; }, {});
    ASSERT_TRUE(waitFor([&] { return venues.size() == 2; }));
    EXPECT_EQ(venues[0].name, QStringLiteral("Grand Lobby"));
    EXPECT_EQ(venues[0].ownerOrgId, QStringLiteral("org1"));
    EXPECT_FALSE(venues[0].canManage);
    EXPECT_EQ(venues[0].widthStuds, 1250);
    EXPECT_EQ(venues[0].heightStuds, 625);
    EXPECT_EQ(venues[0].createdAt.toMSecsSinceEpoch(), 1790000000000);
    EXPECT_TRUE(venues[1].ownerOrgId.isEmpty());
    EXPECT_TRUE(venues[1].canManage);
    EXPECT_EQ(venues[1].widthStuds, 0);
    EXPECT_FALSE(venues[1].createdAt.isValid());
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
    api.venueFile(QStringLiteral("v1"), [&](const QString& name, const QByteArray& f) {
        gotName = name;
        file = f;
    }, {});
    ASSERT_TRUE(waitFor([&] { return !file.isEmpty(); }));
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

    // A token without venues:read gets 403, and says why.
    http.reply("/api/venues/v2", 403, { { QStringLiteral("error"), QStringLiteral("insufficient_scope") } });
    api.venueFile(QStringLiteral("v2"), {}, [&](const ServerRefusal& r) { refused = r; });
    ASSERT_TRUE(waitFor([&] { return refused.has_value(); }));
    EXPECT_EQ(refused->status, 403);
    EXPECT_EQ(refused->code, QStringLiteral("insufficient_scope"));
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

TEST(ServerApi, NamesTheAppInEveryRequest) {
    EXPECT_TRUE(userAgent().startsWith("BrickLayoutDesigner/"));
    EXPECT_TRUE(userAgent().endsWith(" (desktop)"));
    FakeHttp http;
    ServerApi api;
    api.setBase(http.base());
    api.setToken(QStringLiteral("bld_pat_x"));
    http.reply("/api/orgs", 200, { { QStringLiteral("orgs"), QJsonArray{} } });
    bool done = false;
    QObject::connect(&api, &ServerApi::orgsReady, [&] { done = true; });
    api.fetchOrgs();
    ASSERT_TRUE(waitFor([&] { return done; }));
    // A POST too.
    http.reply("/api/layouts", 200, { { QStringLiteral("id"), QStringLiteral("L9") }, { QStringLiteral("title"), QStringLiteral("T") } });
    bool published = false;
    QObject::connect(&api, &ServerApi::published, [&] { published = true; });
    api.publishLayout(QStringLiteral("T"), QByteArray("<Map/>"), {}, {});
    ASSERT_TRUE(waitFor([&] { return published; }));
    ASSERT_EQ(http.requests.size(), 2u);
    for (const auto& r : http.requests) EXPECT_EQ(r.userAgent, userAgent());
}

// Publish refused by a usage limit: the server's sentence, and no sign-in
// prompt (that is only for a token that can't publish).
TEST(ServerApi, PublishShowsTheServersLimitMessage) {
    FakeHttp http;
    ServerApi api;
    api.setBase(http.base());
    api.setToken(QStringLiteral("bld_pat_x"));
    const QString msg = QStringLiteral("Your club has used its 10 GB. Ask the site admin for more room.");
    http.reply("/api/layouts", 403,
               { { QStringLiteral("error"), QStringLiteral("limit_reached") },
                 { QStringLiteral("limit"), QStringLiteral("storagePerClub") },
                 { QStringLiteral("message"), msg } });
    http.reply("/api/layouts", 403, { { QStringLiteral("error"), QStringLiteral("insufficient_scope") } });
    QString what, message;
    bool unauthorized = true;
    int fails = 0;
    QObject::connect(&api, &ServerApi::requestFailed, [&](const QString& w, const QString& m, bool u) {
        what = w;
        message = m;
        unauthorized = u;
        ++fails;
    });
    api.publishLayout(QStringLiteral("T"), QByteArray("<Map/>"), {}, QStringLiteral("club"));
    ASSERT_TRUE(waitFor([&] { return fails == 1; }));
    EXPECT_EQ(what, QStringLiteral("publish"));
    EXPECT_EQ(message, msg);
    EXPECT_FALSE(unauthorized);
    // A token without the scope still asks to sign in again.
    api.publishLayout(QStringLiteral("T"), QByteArray("<Map/>"), {}, {});
    ASSERT_TRUE(waitFor([&] { return fails == 2; }));
    EXPECT_TRUE(unauthorized);
    EXPECT_EQ(message, QStringLiteral("insufficient_scope"));
}

TEST(ServerRefusal, ReadsLimitsReadOnlyAndRateLimits) {
    const auto lim = readRefusal(403, R"({"error":"limit_reached","limit":"layoutsPerUser","message":"You have 500 layouts, the most allowed."})");
    EXPECT_TRUE(isLimitRefusal(lim));
    EXPECT_FALSE(needsSignIn(lim));
    EXPECT_EQ(lim.limit, QStringLiteral("layoutsPerUser"));
    EXPECT_EQ(describe(lim), QStringLiteral("You have 500 layouts, the most allowed."));

    const auto sus = readRefusal(403, R"({"error":"suspended"})");
    EXPECT_EQ(describe(sus), QStringLiteral("This account is read-only for now. Ask the site admin why."));
    EXPECT_FALSE(needsSignIn(sus));
    EXPECT_EQ(describe(readRefusal(429, R"({"error":"rate_limited"})")),
              QStringLiteral("Too many requests at once. Please wait a minute and try again."));

    EXPECT_TRUE(needsSignIn(readRefusal(401, R"({"error":"invalid_token"})")));
    // A 403 the app never saw (empty or non-JSON body) is the site's firewall:
    // say so, and don't ask to sign in again.
    for (const QByteArray& body : { QByteArray(), QByteArray("<html>Request blocked</html>") }) {
        const auto waf = readRefusal(403, body);
        EXPECT_TRUE(isFirewallBlock(waf));
        EXPECT_FALSE(needsSignIn(waf));
        EXPECT_TRUE(describe(waf).startsWith(QStringLiteral("The site's firewall blocked this request.")));
        EXPECT_EQ(failureText(waf), describe(waf));
    }
    EXPECT_FALSE(isFirewallBlock(readRefusal(403, R"({"error":"forbidden"})")));
    EXPECT_FALSE(isFirewallBlock(readRefusal(500, "")));
    EXPECT_TRUE(needsSignIn(readRefusal(403, R"({"error":"insufficient_scope"})")));
    EXPECT_EQ(describe(readRefusal(500, "")), QStringLiteral("The server answered 500"));
    EXPECT_EQ(describe(readRefusal(0, "", QStringLiteral("Connection refused"))), QStringLiteral("Connection refused"));

    EXPECT_FALSE(liveCloseText(4429, QStringLiteral("limit_reached")).isEmpty());
    EXPECT_TRUE(liveCloseText(4429, QStringLiteral("too_many_connections")).isEmpty());
}

// An account being deleted, or on hold for a privacy request: the server's
// own sentence, and never "sign in again".
TEST(ServerRefusal, ExplainsAnAccountBeingDeletedOrOnHold) {
    const auto del = readRefusal(
        403, R"({"error":"account_pending_deletion","deletionDueAt":1900000000000,"message":"This account is being deleted on Sat, 17 Mar 2030. To keep it, sign in on the website before then."})");
    EXPECT_TRUE(isAccountState(del));
    EXPECT_FALSE(needsSignIn(del));
    EXPECT_FALSE(isLimitRefusal(del));
    EXPECT_TRUE(describe(del).startsWith(QStringLiteral("This account is being deleted on")));
    EXPECT_EQ(failureText(del), describe(del));

    const auto held = readRefusal(403, R"({"error":"account_restricted"})");
    EXPECT_TRUE(isAccountState(held));
    EXPECT_FALSE(needsSignIn(held));
    EXPECT_TRUE(describe(held).startsWith(QStringLiteral("Your account is on hold (read only)")));
    EXPECT_EQ(failureText(held), describe(held));
    // Without the server's words, still plain words.
    EXPECT_TRUE(describe(readRefusal(403, R"({"error":"account_pending_deletion"})")).contains(QStringLiteral("sign in on the website")));
    EXPECT_FALSE(isAccountState(readRefusal(403, R"({"error":"forbidden"})")));
    // A live layout closed because of it says why.
    EXPECT_TRUE(liveCloseText(1008, QStringLiteral("account_pending_deletion")).contains(QStringLiteral("being deleted")));
    EXPECT_TRUE(liveCloseText(1008, QStringLiteral("account_restricted")).contains(QStringLiteral("on hold")));
    EXPECT_TRUE(liveCloseText(1008, QStringLiteral("token_revoked")).isEmpty());
}

// Every HTTP caller shows the account's state instead of asking to sign in again.
TEST(ServerApi, ShowsTheAccountStateInsteadOfSignIn) {
    FakeHttp http;
    ServerApi api;
    api.setBase(http.base());
    api.setToken(QStringLiteral("bld_pat_x"));
    http.reply("/api/orgs", 403,
               { { QStringLiteral("error"), QStringLiteral("account_pending_deletion") },
                 { QStringLiteral("message"), QStringLiteral("This account is being deleted on Sat, 17 Mar 2030.") } });
    QString message;
    bool unauthorized = true;
    QObject::connect(&api, &ServerApi::requestFailed, [&](const QString&, const QString& m, bool u) {
        message = m;
        unauthorized = u;
    });
    api.fetchOrgs();
    ASSERT_TRUE(waitFor([&] { return !message.isEmpty(); }));
    EXPECT_EQ(message, QStringLiteral("This account is being deleted on Sat, 17 Mar 2030."));
    EXPECT_FALSE(unauthorized);
}

// Club roles: managers look after the club's things like admins, but only
// admins change its settings.
TEST(ServerApi, ReadsEachClubRoleAndWhatItMayDo) {
    FakeHttp http;
    ServerApi api;
    api.setBase(http.base());
    api.setToken(QStringLiteral("bld_pat_x"));
    const auto org = [](const char* slug, const char* role) {
        return QJsonObject{ { QStringLiteral("slug"), QLatin1String(slug) },
                            { QStringLiteral("name"), QLatin1String(slug) },
                            { QStringLiteral("myRole"), QLatin1String(role) } };
    };
    http.reply("/api/orgs", 200,
               { { QStringLiteral("orgs"), QJsonArray{ org("a", "admin"), org("m", "manager"), org("u", "member") } } });
    QList<OrgEntry> got;
    QObject::connect(&api, &ServerApi::orgsReady, [&](const QList<OrgEntry>& orgs) { got = orgs; });
    api.fetchOrgs();
    ASSERT_TRUE(waitFor([&] { return got.size() == 3; }));
    EXPECT_EQ(got[1].role, QStringLiteral("manager"));
    EXPECT_TRUE(got[0].managesThings());
    EXPECT_TRUE(got[0].isAdmin());
    EXPECT_TRUE(got[1].managesThings());
    EXPECT_FALSE(got[1].isAdmin());
    EXPECT_FALSE(got[2].managesThings());
    EXPECT_FALSE(got[2].isAdmin());
}

// Every HTTP caller shows the firewall message for an empty 403.
TEST(ServerApi, SaysTheFirewallBlockedAnEmpty403) {
    FakeHttp http;
    ServerApi api;
    api.setBase(http.base());
    api.setToken(QStringLiteral("bld_pat_x"));
    http.replyRaw("/api/orgs", 403, QByteArray(), "text/html");
    http.replyRaw("/api/layouts", 403, QByteArray(), "text/html");
    http.replyRaw("/api/version", 403, QByteArray("blocked"), "text/plain");
    QStringList messages;
    bool anySignIn = false;
    QObject::connect(&api, &ServerApi::requestFailed, [&](const QString&, const QString& m, bool u) {
        messages << m;
        anySignIn = anySignIn || u;
    });
    api.fetchOrgs();
    api.publishLayout(QStringLiteral("T"), QByteArray("<Map/>"), {}, {});
    api.fetchVersion();
    ASSERT_TRUE(waitFor([&] { return messages.size() == 3; }));
    for (const QString& m : messages) EXPECT_TRUE(m.startsWith(QStringLiteral("The site's firewall blocked"))) << m.toStdString();
    EXPECT_FALSE(anySignIn);
}
