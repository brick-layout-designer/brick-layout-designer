// Server parts to the desktop (sync phase P4b): download what's missing or
// changed from the server's parts manifest, skip unchanged libraries,
// remove files the server dropped, refuse paths that escape the cache, and
// retry what failed next time.

#include "PartsSync.h"
#include "parts/PartsLibrary.h"
#include "ServerApi.h"
#include "FakeHttp.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

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

QString sha(const QByteArray& b) {
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}
QByteArray read(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

const QByteArray kXml = "<part><Author>club</Author></part>";
const QByteArray kGif = "GIF89a-3001";
const QByteArray kCustomXml = "<part><Author>me</Author></part>";
const QByteArray kPng = "\x89PNG-custom";

void serveManifest(FakeHttp& http, const QString& libHash, const QString& customHash,
                   const QJsonArray& files) {
    http.clear("/api/parts/manifest");
    http.clear("/api/parts/manifest/libraries/club");
    http.reply(
        "/api/parts/manifest", 200,
        { { QStringLiteral("libraries"),
            QJsonArray{
                QJsonObject{ { QStringLiteral("slug"), QStringLiteral("club") },
                             { QStringLiteral("name"), QStringLiteral("Club parts") },
                             { QStringLiteral("urlPrefix"), QStringLiteral("/parts/libraries/club/") },
                             { QStringLiteral("hash"), libHash } } } },
          { QStringLiteral("customParts"),
            QJsonArray{ QJsonObject{
                { QStringLiteral("id"), QStringLiteral("c1") },
                { QStringLiteral("partNumber"), QStringLiteral("MY.1") },
                { QStringLiteral("hash"), customHash },
                { QStringLiteral("xmlUrl"), QStringLiteral("/api/custom-parts/c1/xml") },
                { QStringLiteral("spriteUrl"), QStringLiteral("/api/custom-parts/c1/sprite") } } } } });
    http.reply("/api/parts/manifest/libraries/club", 200,
               { { QStringLiteral("slug"), QStringLiteral("club") },
                 { QStringLiteral("urlPrefix"), QStringLiteral("/parts/libraries/club/") },
                 { QStringLiteral("files"), files } });
}

QJsonObject file(const char* path, const QByteArray& data) {
    return { { QStringLiteral("path"), QString::fromLatin1(path) },
             { QStringLiteral("sha256"), sha(data) },
             { QStringLiteral("size"), data.size() } };
}

PartsSyncResult sync(FakeHttp& http, const QString& dir) {
    PartsSync s(http.base(), QStringLiteral("bld_pat_abc"), dir);
    std::optional<PartsSyncResult> out;
    QObject::connect(&s, &PartsSync::finished, [&](const PartsSyncResult& r) { out = r; });
    s.start();
    EXPECT_TRUE(waitFor([&] { return out.has_value(); }));
    return out.value_or(PartsSyncResult{});
}

} // namespace

TEST(PartsSync, DownloadsWhatChangedAndSkipsWhatDidnt) {
    QTemporaryDir dir;
    FakeHttp http;
    serveManifest(http, QStringLiteral("L1"), QStringLiteral("C1"),
                  QJsonArray{ file("3001.xml", kXml), file("3001.gif", kGif) });
    http.replyRaw("/parts/libraries/club/3001.xml", 200, kXml, "application/xml");
    http.replyRaw("/parts/libraries/club/3001.gif", 200, kGif, "image/gif");
    http.replyRaw("/api/custom-parts/c1/xml", 200, kCustomXml, "application/xml");
    http.replyRaw("/api/custom-parts/c1/sprite", 200, kPng, "image/png");

    const auto first = sync(http, dir.path());
    EXPECT_EQ(first.downloaded, 4);
    EXPECT_TRUE(first.failed.isEmpty()) << first.failed.join(QStringLiteral("; ")).toStdString();
    EXPECT_EQ(read(dir.filePath(QStringLiteral("libs/club/3001.gif"))), kGif);
    // The website's key for it is kept as an old name, so layouts made there find it.
    EXPECT_EQ(read(dir.filePath(QStringLiteral("custom/MY.1.xml"))),
              QByteArray("<part><Author>me</Author><OldNameList><OldName>custom:c1</OldName></OldNameList></part>"));
    EXPECT_EQ(read(dir.filePath(QStringLiteral("custom/MY.1.png"))), kPng);
    for (const auto& r : http.requests) {
        // The token goes to the API, never with the public part files (the
        // server refuses a token on routes that don't take one).
        EXPECT_EQ(r.authorization, r.path.startsWith("/api/") ? QByteArray("Bearer bld_pat_abc") : QByteArray())
            << r.path.toStdString();
        EXPECT_EQ(r.userAgent, userAgent());  // so a server's filters can tell the app apart
    }

    // Nothing changed on the server: only the manifest is read.
    const auto before = http.requests.size();
    const auto second = sync(http, dir.path());
    EXPECT_EQ(second.downloaded, 0);
    EXPECT_EQ(second.unchanged, 2);
    EXPECT_EQ(http.requests.size(), before + 1);

    // The library changed: a new file comes, the dropped one goes, the unchanged one stays.
    const QByteArray kTile = "<part>tile</part>";
    serveManifest(http, QStringLiteral("L2"), QStringLiteral("C1"),
                  QJsonArray{ file("3001.xml", kXml), file("sub/3068.xml", kTile) });
    http.replyRaw("/parts/libraries/club/sub/3068.xml", 200, kTile, "application/xml");
    const auto third = sync(http, dir.path());
    EXPECT_EQ(third.downloaded, 1);
    EXPECT_EQ(third.removed, 1);
    EXPECT_FALSE(QFile::exists(dir.filePath(QStringLiteral("libs/club/3001.gif"))));
    EXPECT_EQ(read(dir.filePath(QStringLiteral("libs/club/sub/3068.xml"))), kTile);
}

TEST(PartsSync, RefusesEscapingPathsAndRetriesFailuresNextTime) {
    QTemporaryDir dir;
    FakeHttp http;
    serveManifest(http, QStringLiteral("L1"), QStringLiteral("C1"),
                  QJsonArray{ file("../evil.xml", kXml), file("3001.xml", kXml) });
    http.replyRaw("/parts/libraries/club/3001.xml", 200, "tampered",
                  "application/xml"); // doesn't match its hash
    http.replyRaw("/api/custom-parts/c1/xml", 200, kCustomXml, "application/xml");
    http.replyRaw("/api/custom-parts/c1/sprite", 200, kPng, "image/png");
    const auto r = sync(http, dir.path());
    EXPECT_EQ(r.failed.size(), 2); // the escaping path and the bad download
    EXPECT_FALSE(QFile::exists(QDir(dir.path()).filePath(QStringLiteral("libs/evil.xml"))));
    EXPECT_FALSE(QFile::exists(dir.filePath(QStringLiteral("libs/club/3001.xml"))));
    // The library is tried again next time (its hash wasn't recorded).
    http.clear("/parts/libraries/club/3001.xml");
    http.replyRaw("/parts/libraries/club/3001.xml", 200, kXml, "application/xml");
    serveManifest(http, QStringLiteral("L1"), QStringLiteral("C1"), QJsonArray{ file("3001.xml", kXml) });
    const auto again = sync(http, dir.path());
    EXPECT_EQ(again.downloaded, 1);
    EXPECT_TRUE(again.failed.isEmpty());

    EXPECT_FALSE(PartsSync::safeRelativePath(QStringLiteral("/etc/passwd")));
    EXPECT_FALSE(PartsSync::safeRelativePath(QStringLiteral("a/../../b")));
    EXPECT_FALSE(PartsSync::safeRelativePath(QStringLiteral("C:/x")));
    EXPECT_TRUE(PartsSync::safeRelativePath(QStringLiteral("sub/3068.xml")));
}

TEST(PartsSync, ReportsAManifestItCantRead) {
    QTemporaryDir dir;
    FakeHttp http;
    http.reply("/api/parts/manifest", 403,
               { { QStringLiteral("error"), QStringLiteral("insufficient_scope") } });
    PartsSync s(http.base(), QStringLiteral("t"), dir.path());
    bool unauthorized = false, failed = false;
    QObject::connect(&s, &PartsSync::failed, [&](const QString&, bool u) {
        failed = true;
        unauthorized = u;
    });
    s.start();
    ASSERT_TRUE(waitFor([&] { return failed; }));
    EXPECT_TRUE(unauthorized);
}

TEST(PartsSync, SaysTheFirewallBlockedAnEmpty403) {
    QTemporaryDir dir;
    FakeHttp http;
    http.replyRaw("/api/parts/manifest", 403, QByteArray(), "text/html");
    PartsSync s(http.base(), QStringLiteral("t"), dir.path());
    QString message;
    bool unauthorized = true, failed = false;
    QObject::connect(&s, &PartsSync::failed, [&](const QString& m, bool u) {
        failed = true;
        message = m;
        unauthorized = u;
    });
    s.start();
    ASSERT_TRUE(waitFor([&] { return failed; }));
    EXPECT_TRUE(message.startsWith(QStringLiteral("The site's firewall blocked this request.")));
    EXPECT_FALSE(unauthorized);
}

TEST(PartsSync, APartResentWithTheOtherSpriteDropsTheOldOne) {
    // The parts library prefers a .png for a hi-res XML and a .gif
    // otherwise, so a stale sibling could be drawn at the wrong size.
    QTemporaryDir dir;
    FakeHttp http;
    serveManifest(http, QStringLiteral("L1"), QStringLiteral("C1"), QJsonArray{});
    http.replyRaw("/api/custom-parts/c1/xml", 200, kCustomXml, "application/xml");
    http.replyRaw("/api/custom-parts/c1/sprite", 200, kPng, "image/png");
    sync(http, dir.path());
    ASSERT_TRUE(QFile::exists(dir.filePath(QStringLiteral("custom/MY.1.png"))));

    serveManifest(http, QStringLiteral("L1"), QStringLiteral("C2"), QJsonArray{});
    http.clear("/api/custom-parts/c1/sprite");
    http.replyRaw("/api/custom-parts/c1/sprite", 200, kGif, "image/gif");
    const auto second = sync(http, dir.path());
    EXPECT_TRUE(second.failed.isEmpty()) << second.failed.join(QStringLiteral("; ")).toStdString();
    EXPECT_EQ(read(dir.filePath(QStringLiteral("custom/MY.1.gif"))), kGif);
    EXPECT_FALSE(QFile::exists(dir.filePath(QStringLiteral("custom/MY.1.png"))));
}

TEST(PartsSync, ALayoutMadeOnTheWebsiteFindsTheServersCustomParts) {
    // The website places a custom part as "custom:<id>"; the desktop knows it by part number.
    QTemporaryDir dir;
    FakeHttp http;
    serveManifest(http, QStringLiteral("L1"), QStringLiteral("C1"), QJsonArray{});
    const QByteArray xml = "<part><Author>me</Author><ImageURL></ImageURL></part>";
    http.replyRaw("/api/custom-parts/c1/xml", 200, xml, "application/xml");
    http.replyRaw("/api/custom-parts/c1/sprite", 200, kPng, "image/png");
    const auto first = sync(http, dir.path());
    EXPECT_TRUE(first.failed.isEmpty()) << first.failed.join(QStringLiteral("; ")).toStdString();

    bld::parts::PartsLibrary lib;
    lib.addSearchPath(dir.filePath(QStringLiteral("custom")));
    lib.scan();
    EXPECT_TRUE(lib.metadata(QStringLiteral("MY.1")).has_value());
    EXPECT_TRUE(lib.metadata(QStringLiteral("custom:c1")).has_value());
    EXPECT_TRUE(lib.metadata(QStringLiteral("CUSTOM:C1")).has_value());

    // A copy cached before this fix (no key in it) is fetched again, once.
    QFile f(dir.filePath(QStringLiteral("custom/MY.1.xml")));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(xml);
    f.close();
    EXPECT_EQ(sync(http, dir.path()).downloaded, 2);
    EXPECT_EQ(sync(http, dir.path()).downloaded, 0);
}
