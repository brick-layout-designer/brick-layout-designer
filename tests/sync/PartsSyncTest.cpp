// Server parts to the desktop (sync phase P4b): download what's missing or
// changed from the server's parts manifest, skip unchanged libraries,
// remove files the server dropped, refuse paths that escape the cache, and
// retry what failed next time.

#include "PartsSync.h"
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
    EXPECT_EQ(read(dir.filePath(QStringLiteral("custom/MY.1.xml"))), kCustomXml);
    EXPECT_EQ(read(dir.filePath(QStringLiteral("custom/MY.1.png"))), kPng);
    for (const auto& r : http.requests) {
        EXPECT_EQ(r.authorization, QByteArray("Bearer bld_pat_abc"));
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
