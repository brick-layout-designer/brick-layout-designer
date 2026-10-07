// Desktop parts to the server (sync phase P4b): the user's parts the
// server's catalog lacks are listed, and only the ones they confirm are
// uploaded, personal or to an organisation.

#include "PartsUpload.h"
#include "FakeHttp.h"
#include "ServerApi.h"
#include "UploadPartsDialog.h"

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>

#include <functional>
#include <memory>

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
void write(const QString& path, const QByteArray& data) {
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(data);
}
} // namespace

TEST(PartsUpload, ListsOnlyPartsTheServerLacksAndUploadsWhatWasChosen) {
    QTemporaryDir dir;
    write(dir.filePath(QStringLiteral("3001.8.xml")), "<part/>");
    write(dir.filePath(QStringLiteral("3001.8.gif")), "GIF");
    write(dir.filePath(QStringLiteral("MY.1.xml")),
          "<part><Description><en>My bridge</en></Description></part>");
    write(dir.filePath(QStringLiteral("MY.1.png")), "PNG");
    write(dir.filePath(QStringLiteral("MY.2.xml")),
          "<part><Description><en>My tunnel</en></Description></part>");
    write(dir.filePath(QStringLiteral("MY.2.gif")), "GIF2");
    write(dir.filePath(QStringLiteral("NOSPRITE.xml")), "<part/>");
    const auto local = PartsUpload::scanFolder(dir.path());
    ASSERT_EQ(local.size(), 3); // the one without a sprite is skipped
    EXPECT_EQ(local[1].displayName, QStringLiteral("My bridge"));

    FakeHttp http;
    http.reply("/api/parts/catalog", 200,
               { { QStringLiteral("parts"),
                   QJsonArray{ QJsonObject{ { QStringLiteral("key"), QStringLiteral("3001.8") },
                                            { QStringLiteral("partNumber"), QStringLiteral("3001") } } } } });
    http.reply("/api/orgs", 200,
               { { QStringLiteral("orgs"),
                   QJsonArray{ QJsonObject{ { QStringLiteral("slug"), QStringLiteral("club") },
                                            { QStringLiteral("name"), QStringLiteral("Train Club") } } } } });
    http.reply("/api/custom-parts", 201, { { QStringLiteral("id"), QStringLiteral("p1") } });
    PartsUpload upload(http.base(), QStringLiteral("bld_pat_abc"));
    QList<LocalPart> missing;
    bool got = false;
    QObject::connect(&upload, &PartsUpload::missingReady, [&](const QList<LocalPart>& m) {
        missing = m;
        got = true;
    });
    upload.findMissing(local);
    ASSERT_TRUE(waitFor([&] { return got; }));
    ASSERT_EQ(missing.size(), 2);
    EXPECT_EQ(missing[0].key, QStringLiteral("MY.1"));

    // Confirmed: only the checked one goes, to the chosen organisation.
    ServerApi api;
    api.setBase(http.base());
    api.setToken(QStringLiteral("bld_pat_abc"));
    UploadPartsDialog dialog(api, upload, missing);
    auto* owner = dialog.findChild<QComboBox*>(QStringLiteral("owner"));
    ASSERT_TRUE(waitFor([&] { return owner->count() == 2; }));
    EXPECT_EQ(owner->itemText(0), QStringLiteral("Me"));
    owner->setCurrentIndex(1);
    dialog.findChild<QListWidget*>(QStringLiteral("parts"))->item(1)->setCheckState(Qt::Unchecked);
    // Into a club: asked first; no means nothing goes.
    QStringList asked;
    bool yes = false;
    dialog.confirmClub = [&](const QString& club) {
        asked << club;
        return yes;
    };
    dialog.uploadChecked();
    EXPECT_EQ(asked.size(), 1);
    EXPECT_NE(dialog.result(), QDialog::Accepted);
    yes = true;
    dialog.uploadChecked();
    EXPECT_EQ(asked.size(), 2);
    ASSERT_TRUE(waitFor([&] { return dialog.result() == QDialog::Accepted; }));
    EXPECT_EQ(dialog.uploadedCount(), 1);
    int posts = 0;
    for (const auto& r : http.requests) {
        EXPECT_EQ(r.userAgent, userAgent());
        if (r.path != "/api/custom-parts") continue;
        ++posts;
        const auto body = QJsonDocument::fromJson(r.body).object();
        EXPECT_EQ(body.value(QLatin1String("partNumber")).toString(), QStringLiteral("MY.1"));
        EXPECT_EQ(body.value(QLatin1String("displayName")).toString(), QStringLiteral("My bridge"));
        EXPECT_EQ(body.value(QLatin1String("spriteMime")).toString(), QStringLiteral("image/png"));
        EXPECT_EQ(QByteArray::fromBase64(body.value(QLatin1String("spriteBase64")).toString().toLatin1()),
                  QByteArray("PNG"));
        EXPECT_EQ(body.value(QLatin1String("orgSlug")).toString(), QStringLiteral("club"));
        EXPECT_EQ(r.authorization, QByteArray("Bearer bld_pat_abc"));
    }
    EXPECT_EQ(posts, 1);
}

TEST(PartsUpload, StopsWhenTheTokenCantUpload) {
    FakeHttp http;
    http.reply("/api/custom-parts", 403,
               { { QStringLiteral("error"), QStringLiteral("insufficient_scope") } });
    QTemporaryDir dir;
    write(dir.filePath(QStringLiteral("A.xml")), "<part/>");
    write(dir.filePath(QStringLiteral("A.gif")), "G");
    write(dir.filePath(QStringLiteral("B.xml")), "<part/>");
    write(dir.filePath(QStringLiteral("B.gif")), "G");
    PartsUpload upload(http.base(), QStringLiteral("t"));
    int count = -1;
    QStringList failures;
    bool unauthorized = false;
    QObject::connect(&upload, &PartsUpload::failed, [&](const QString&, bool u) { unauthorized = u; });
    QObject::connect(&upload, &PartsUpload::uploaded, [&](int c, const QStringList& f) {
        count = c;
        failures = f;
    });
    upload.upload(PartsUpload::scanFolder(dir.path()), {});
    ASSERT_TRUE(waitFor([&] { return count >= 0; }));
    EXPECT_EQ(count, 0);
    EXPECT_EQ(failures.size(), 1); // stopped after the first refusal
    EXPECT_TRUE(unauthorized);
}

// Placing a part while live: only your own parts the server lacks are
// offered, each once.
namespace {
std::unique_ptr<bld::core::Map> mapUsing(const QStringList& bricks, const QStringList& groups = {}) {
    auto map = std::make_unique<bld::core::Map>();
    auto layer = std::make_unique<bld::core::LayerBrick>();
    for (const auto& n : bricks) {
        bld::core::Brick b;
        b.partNumber = n;
        layer->bricks.push_back(b);
    }
    for (const auto& n : groups) {
        bld::core::Group g;
        g.partNumber = n;
        layer->groups.push_back(g);
    }
    map->layers().push_back(std::move(layer));
    return map;
}
LocalPart localPart(const QString& key, const QString& dir = QStringLiteral("/home/me/imports")) {
    return { key, key, dir + QLatin1Char('/') + key + QStringLiteral(".xml"),
             dir + QLatin1Char('/') + key + QStringLiteral(".gif") };
}
QStringList keysOf(const QList<LocalPart>& parts) {
    QStringList out;
    for (const auto& p : parts) out << p.key;
    return out;
}
} // namespace

TEST(PartsToOffer, OffersYourOwnPlacedPartTheServerLacks) {
    const auto map = mapUsing({ QStringLiteral("MY.1"), QStringLiteral("3001.8") });
    const auto offered =
        partsToOffer(*map, {}, { localPart(QStringLiteral("MY.1")), localPart(QStringLiteral("MY.2")) }, {});
    EXPECT_EQ(keysOf(offered), QStringList{ QStringLiteral("MY.1") }); // MY.2 isn't placed
}

TEST(PartsToOffer, NeverOffersBundledParts) {
    const auto map = mapUsing({ QStringLiteral("3001.8"), QStringLiteral("MY.1") });
    const QString bundled = QStringLiteral("/opt/bld/parts/BlueBrickParts/parts");
    const auto offered = partsToOffer(
        *map, {}, { localPart(QStringLiteral("3001.8"), bundled + QStringLiteral("/Brick")),
                    localPart(QStringLiteral("MY.1")) },
        {}, bundled);
    EXPECT_EQ(keysOf(offered), QStringList{ QStringLiteral("MY.1") });
}

TEST(PartsToOffer, RecognisesBundledPartsWhateverTheRootsSpelling) {
    // Windows hands paths over with '\\', a trailing separator, and the
    // drive / folder names in whatever case: the bundled root and the parts
    // found under it must still match.
    const auto map = mapUsing({ QStringLiteral("3001.8"), QStringLiteral("3002.8"), QStringLiteral("MY.1") });
    const QString root = QStringLiteral("/Opt/BLD/parts\\BlueBrickParts\\parts\\");
    const auto offered = partsToOffer(
        *map, {},
        { localPart(QStringLiteral("3001.8"), QStringLiteral("/opt/bld/parts/BlueBrickParts/parts/Brick")),
          localPart(QStringLiteral("3002.8"), QStringLiteral("\\opt\\bld\\parts\\BlueBrickParts/parts\\Plate")),
          localPart(QStringLiteral("MY.1")) },
        {}, root);
    EXPECT_EQ(keysOf(offered), QStringList{ QStringLiteral("MY.1") });
}

TEST(PartsToOffer, BundledRootIsAFolderNotAPrefix) {
    // "parts2" isn't inside "parts".
    const auto map = mapUsing({ QStringLiteral("MY.1") });
    const auto offered = partsToOffer(*map, {}, { localPart(QStringLiteral("MY.1"), QStringLiteral("/opt/bld/parts2")) },
                                      {}, QStringLiteral("/opt/bld/parts"));
    EXPECT_EQ(keysOf(offered), QStringList{ QStringLiteral("MY.1") });
}

TEST(PartsToOffer, RecognisesBundledPartsThroughALinkedRoot) {
#ifdef Q_OS_WIN
    GTEST_SKIP() << "QFile::link makes a .lnk shortcut on Windows, not a folder link";
#else
    // The installed library reached through a link (or, on Windows, an
    // 8.3 short name): the parts, listed by their real path, are still in it.
    QTemporaryDir dir;
    ASSERT_TRUE(QDir(dir.path()).mkpath(QStringLiteral("real/parts/Brick")));
    write(dir.filePath(QStringLiteral("real/parts/Brick/3001.8.xml")), "<part/>");
    ASSERT_TRUE(QFile::link(dir.filePath(QStringLiteral("real/parts")), dir.filePath(QStringLiteral("linked"))));
    const auto map = mapUsing({ QStringLiteral("3001.8"), QStringLiteral("MY.1") });
    const auto offered = partsToOffer(
        *map, {},
        { localPart(QStringLiteral("3001.8"), QFileInfo(dir.filePath(QStringLiteral("real/parts/Brick"))).canonicalFilePath()),
          localPart(QStringLiteral("MY.1")) },
        {}, dir.filePath(QStringLiteral("linked")));
    EXPECT_EQ(keysOf(offered), QStringList{ QStringLiteral("MY.1") });
#endif
}

TEST(PartsToOffer, SkipsPartsTheServerKnowsIgnoringCase) {
    // The server's keys arrive upper-cased, as findMissing compares them.
    const auto map = mapUsing({ QStringLiteral("my.1"), QStringLiteral("Tunnel.8") });
    const auto offered = partsToOffer(
        *map, { QStringLiteral("MY.1") },
        { localPart(QStringLiteral("My.1")), localPart(QStringLiteral("tunnel.8")) }, {});
    EXPECT_EQ(keysOf(offered), QStringList{ QStringLiteral("tunnel.8") });
}

TEST(PartsToOffer, AsksOnlyOncePerPart) {
    const auto map = mapUsing({ QStringLiteral("MY.1"), QStringLiteral("MY.2") });
    const auto offered = partsToOffer(
        *map, {}, { localPart(QStringLiteral("MY.1")), localPart(QStringLiteral("MY.2")) },
        { QStringLiteral("MY.1") });
    EXPECT_EQ(keysOf(offered), QStringList{ QStringLiteral("MY.2") });
}

TEST(PartsToOffer, CountsSetAndGroupParts) {
    const auto map = mapUsing({}, { QStringLiteral("MYSTATION.SET") });
    const auto offered = partsToOffer(*map, {}, { localPart(QStringLiteral("MYSTATION.SET")) }, {});
    EXPECT_EQ(keysOf(offered), QStringList{ QStringLiteral("MYSTATION.SET") });
    EXPECT_TRUE(partNumbersIn(*map).contains(QStringLiteral("MYSTATION.SET")));
}

TEST(PartsUpload, FetchesTheCatalogsKnownPartsUpperCased) {
    FakeHttp http;
    http.reply("/api/parts/catalog", 200,
               { { QStringLiteral("parts"),
                   QJsonArray{ QJsonObject{ { QStringLiteral("key"), QStringLiteral("my.1") },
                                            { QStringLiteral("partNumber"), QStringLiteral("My") } } } } });
    PartsUpload upload(http.base(), QStringLiteral("t"));
    QSet<QString> known;
    bool got = false;
    QObject::connect(&upload, &PartsUpload::catalogReady, [&](const QSet<QString>& k) {
        known = k;
        got = true;
    });
    upload.fetchCatalog();
    ASSERT_TRUE(waitFor([&] { return got; }));
    EXPECT_EQ(known, (QSet<QString>{ QStringLiteral("MY.1"), QStringLiteral("MY") }));
}

// A usage limit stops the rest (they'd be refused the same way) and shows
// the server's sentence, but is no reason to sign in again; a file too big
// on its own only fails that file.
TEST(PartsUpload, ShowsTheServersLimitMessageWithoutAskingToSignInAgain) {
    FakeHttp http;
    const QString msg = QStringLiteral("You have 2,000 custom parts, the most allowed. Ask the site admin if you need more.");
    http.reply("/api/custom-parts", 403,
               { { QStringLiteral("error"), QStringLiteral("limit_reached") },
                 { QStringLiteral("limit"), QStringLiteral("customPartsPerUser") },
                 { QStringLiteral("message"), msg } });
    QTemporaryDir dir;
    write(dir.filePath(QStringLiteral("A.xml")), "<part/>");
    write(dir.filePath(QStringLiteral("A.gif")), "G");
    write(dir.filePath(QStringLiteral("B.xml")), "<part/>");
    write(dir.filePath(QStringLiteral("B.gif")), "G");
    PartsUpload upload(http.base(), QStringLiteral("t"));
    int count = -1;
    QStringList failures;
    QString failedWith;
    bool unauthorized = true;
    QObject::connect(&upload, &PartsUpload::failed, [&](const QString& m, bool u) {
        failedWith = m;
        unauthorized = u;
    });
    QObject::connect(&upload, &PartsUpload::uploaded, [&](int c, const QStringList& f) {
        count = c;
        failures = f;
    });
    upload.upload(PartsUpload::scanFolder(dir.path()), {});
    ASSERT_TRUE(waitFor([&] { return count >= 0; }));
    EXPECT_EQ(failures.size(), 1);
    EXPECT_TRUE(failures.value(0).endsWith(msg));
    EXPECT_EQ(failedWith, msg);
    EXPECT_FALSE(unauthorized);
}

TEST(PartsUpload, AFileTooBigOnItsOwnOnlyFailsThatFile) {
    FakeHttp http;
    http.reply("/api/custom-parts", 413,
               { { QStringLiteral("error"), QStringLiteral("limit_reached") },
                 { QStringLiteral("limit"), QStringLiteral("uploadBytes") },
                 { QStringLiteral("message"), QStringLiteral("That file is 60 MB; the most you can send at once is 50 MB.") } });
    http.reply("/api/custom-parts", 201, { { QStringLiteral("id"), QStringLiteral("p2") } });
    QTemporaryDir dir;
    write(dir.filePath(QStringLiteral("A.xml")), "<part/>");
    write(dir.filePath(QStringLiteral("A.gif")), "G");
    write(dir.filePath(QStringLiteral("B.xml")), "<part/>");
    write(dir.filePath(QStringLiteral("B.gif")), "G");
    PartsUpload upload(http.base(), QStringLiteral("t"));
    int count = -1;
    QStringList failures;
    QObject::connect(&upload, &PartsUpload::uploaded, [&](int c, const QStringList& f) {
        count = c;
        failures = f;
    });
    upload.upload(PartsUpload::scanFolder(dir.path()), {});
    ASSERT_TRUE(waitFor([&] { return count >= 0; }));
    EXPECT_EQ(count, 1);
    ASSERT_EQ(failures.size(), 1);
    EXPECT_TRUE(failures.value(0).contains(QStringLiteral("the most you can send at once is 50 MB")));
}

// The same part keeps the same size in studs on the server: the shared
// fixture (fixtures/part-scale, also in the web repo) is an import's hi-res
// .png beside its 8 px a stud .gif.
namespace {
const QString kScaleFixture = QStringLiteral(BLD_SOURCE_DIR "/fixtures/part-scale");

QSizeF expectedStuds() {
    QFile f(kScaleFixture + QStringLiteral("/expected.json"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonObject studs = QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("studs")).toObject();
    return { studs.value(QLatin1String("w")).toDouble(), studs.value(QLatin1String("h")).toDouble() };
}

// The fixture's part copied into `dir`, as the imports folder holds it.
void copyFixture(const QString& dir, bool withPng = true) {
    for (const char* ext : { ".xml", ".png", ".gif" }) {
        if (!withPng && QLatin1String(ext) == QLatin1String(".png")) continue;
        QFile::copy(kScaleFixture + QStringLiteral("/SCALE.1") + QLatin1String(ext),
                    dir + QStringLiteral("/SCALE.1") + QLatin1String(ext));
    }
}

// The part's footprint in studs on a desktop that downloads `payload` from
// the server (PartsSync writes the XML and the sprite named by its type).
QSizeF studsAfterDownload(const PartPayload& payload) {
    QTemporaryDir cache;
    QFile xml(cache.filePath(QStringLiteral("SCALE.1.xml")));
    if (!xml.open(QIODevice::WriteOnly)) return {};
    xml.write(payload.xml);
    xml.close();
    QFile sprite(cache.filePath(payload.mime == QLatin1String("image/png") ? QStringLiteral("SCALE.1.png")
                                                                            : QStringLiteral("SCALE.1.gif")));
    if (!sprite.open(QIODevice::WriteOnly)) return {};
    sprite.write(payload.sprite);
    sprite.close();
    bld::parts::PartsLibrary lib;
    const QString key = lib.scanFile(xml.fileName());
    const auto fp = lib.footprint(key, 0.0);
    return fp ? fp->size : QSizeF();
}
} // namespace

TEST(PartsUpload, AnImportedPartKeepsItsSizeInStudsOnTheServer) {
    const QSizeF studs = expectedStuds();
    ASSERT_FALSE(studs.isEmpty());
    QTemporaryDir dir;
    copyFixture(dir.path());
    // Here: the parts library draws the .png at the XML's 32 px a stud.
    {
        bld::parts::PartsLibrary lib;
        const auto fp = lib.footprint(lib.scanFile(dir.filePath(QStringLiteral("SCALE.1.xml"))), 0.0);
        ASSERT_TRUE(fp);
        EXPECT_EQ(fp->size, studs);
    }
    // Sent: the .png, not the vanilla .gif, with the XML that describes it.
    const auto local = PartsUpload::scanFolder(dir.path());
    ASSERT_EQ(local.size(), 1);
    EXPECT_TRUE(local[0].spritePath.endsWith(QLatin1String("SCALE.1.png")));
    const PartPayload payload = PartsUpload::payloadFor(local[0]);
    EXPECT_EQ(payload.mime, QStringLiteral("image/png"));
    EXPECT_EQ(payload.pxPerStud, 32);
    const QImage sent = QImage::fromData(payload.sprite);
    EXPECT_EQ(QSizeF(sent.width() / 32.0, sent.height() / 32.0), studs); // as the web sizes it
    EXPECT_EQ(studsAfterDownload(payload), studs);

    // What actually goes over the wire.
    FakeHttp http;
    http.reply("/api/custom-parts", 201, { { QStringLiteral("id"), QStringLiteral("p1") } });
    PartsUpload upload(http.base(), QStringLiteral("t"));
    int count = -1;
    QObject::connect(&upload, &PartsUpload::uploaded, [&](int c, const QStringList&) { count = c; });
    upload.upload(local, {});
    ASSERT_TRUE(waitFor([&] { return count >= 0; }));
    ASSERT_EQ(count, 1);
    for (const auto& r : http.requests) {
        if (r.path != "/api/custom-parts") continue;
        const auto body = QJsonDocument::fromJson(r.body).object();
        EXPECT_EQ(body.value(QLatin1String("spriteMime")).toString(), QStringLiteral("image/png"));
        EXPECT_EQ(QByteArray::fromBase64(body.value(QLatin1String("spriteBase64")).toString().toLatin1()),
                  payload.sprite);
        EXPECT_TRUE(QByteArray::fromBase64(body.value(QLatin1String("xmlBase64")).toString().toLatin1())
                        .contains("<PixelsPerStud>32</PixelsPerStud>"));
    }
}

TEST(PartsUpload, AGifIsSentAsEightPixelsAStud) {
    // Only the vanilla .gif beside an XML that says 32: the XML sent says 8.
    const QSizeF studs = expectedStuds();
    QTemporaryDir dir;
    copyFixture(dir.path(), false);
    const auto local = PartsUpload::scanFolder(dir.path());
    ASSERT_EQ(local.size(), 1);
    const PartPayload payload = PartsUpload::payloadFor(local[0]);
    EXPECT_EQ(payload.mime, QStringLiteral("image/gif"));
    EXPECT_EQ(payload.pxPerStud, 8);
    EXPECT_FALSE(payload.xml.contains("PixelsPerStud"));
    EXPECT_TRUE(payload.xml.contains("<Description>")); // the rest kept
    const QImage sent = QImage::fromData(payload.sprite);
    EXPECT_EQ(QSizeF(sent.width() / 8.0, sent.height() / 8.0), studs);
    EXPECT_EQ(studsAfterDownload(payload), studs);
}

TEST(PartsUpload, APartTooBigIsSentAtALowerResolutionTheSameSize) {
    const QSizeF studs = expectedStuds();
    QTemporaryDir dir;
    copyFixture(dir.path());
    const auto local = PartsUpload::scanFolder(dir.path());
    ASSERT_EQ(local.size(), 1);
    const PartPayload full = PartsUpload::payloadFor(local[0]);
    const PartPayload smaller = PartsUpload::payloadFor(local[0], full.xml.size() + full.sprite.size() - 1);
    EXPECT_EQ(smaller.mime, QStringLiteral("image/png"));
    EXPECT_LT(smaller.pxPerStud, 32);
    EXPECT_LT(smaller.xml.size() + smaller.sprite.size(), full.xml.size() + full.sprite.size());
    const QImage sent = QImage::fromData(smaller.sprite);
    EXPECT_EQ(QSizeF(sent.width() / double(smaller.pxPerStud), sent.height() / double(smaller.pxPerStud)), studs);
    EXPECT_EQ(studsAfterDownload(smaller), studs);
}

TEST(PartsUpload, TheLocalPathOfAnImportStaysOnThisComputer) {
    QTemporaryDir dir;
    QFile xml(dir.filePath(QStringLiteral("p.xml")));
    ASSERT_TRUE(xml.open(QIODevice::WriteOnly));
    xml.write("<part><Description><en>Station</en></Description>\n"
              "  <ImportedFrom file=\"station.lxf\" format=\"ldd\" date=\"2026-10-07\"/>\n"
              "  <ImportSource>\n    <SourcePath>/home/aaron/secret/station.lxf</SourcePath>\n  </ImportSource>\n</part>");
    xml.close();
    QImage(16, 16, QImage::Format_ARGB32).save(dir.filePath(QStringLiteral("p.png")));
    const auto parts = PartsUpload::scanFolder(dir.path());
    ASSERT_EQ(parts.size(), 1);
    EXPECT_EQ(parts.front().displayName, QStringLiteral("Station"));
    const auto payload = PartsUpload::payloadFor(parts.front(), 1 << 20);
    EXPECT_FALSE(payload.xml.contains("/home/aaron")) << payload.xml.toStdString();
    EXPECT_FALSE(payload.xml.contains("ImportSource"));
    EXPECT_TRUE(payload.xml.contains("<ImportedFrom file=\"station.lxf\""));
}
