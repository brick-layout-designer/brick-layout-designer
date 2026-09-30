// The native layout file (.bld-layout) and the ZIP writer under it.

#include "import/LayoutFile.h"
#include "import/zip/SafeZip.h"
#include "import/zip/ZipWriter.h"

#include "core/Map.h"
#include "saveload/BbmReader.h"
#include "saveload/BbmWriter.h"
#include "saveload/SidecarIO.h"
#include "saveload/VenueIO.h"

#include <gtest/gtest.h>

#include <QBuffer>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

using namespace bld;
using namespace bld::import;

namespace {

const QString kSource = QStringLiteral(BLD_SOURCE_DIR);

std::unique_ptr<core::Map> fixtureMap() {
    auto r = saveload::readBbm(kSource + QStringLiteral("/fixtures/bbm-corpus/tight-corner.bbm"));
    EXPECT_TRUE(r.ok()) << r.error.toStdString();
    return std::move(r.map);
}

QByteArray bbmOf(const core::Map& map) {
    QBuffer b;
    b.open(QIODevice::WriteOnly);
    EXPECT_TRUE(saveload::writeBbm(map, b).ok);
    return b.data();
}

QByteArray sidecarOf(const core::Map& map) {
    QJsonObject o = saveload::sidecarToJson(map.sidecar);
    o.remove(QStringLiteral("bbmHashSha256"));
    QJsonObject bg = o.value(QLatin1String("backgroundImage")).toObject();
    bg.remove(QStringLiteral("path"));
    o[QStringLiteral("backgroundImage")] = bg;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// Labels, a module, the Grand Lobby venue and a background image.
void addEverything(core::Map& map, const QString& imagePath) {
    auto& s = map.sidecar;
    core::AnchoredLabel label;
    label.id = QStringLiteral("L1");
    label.text = QStringLiteral("Grand Lobby — north end");
    label.targetId = QStringLiteral("brick-1");
    s.anchoredLabels.push_back(label);
    core::Module module;
    module.id = QStringLiteral("M1");
    module.name = QStringLiteral("Corner");
    module.memberIds.insert(QStringLiteral("brick-1"));
    s.modules.push_back(module);
    s.venue = saveload::readVenueFile(kSource + QStringLiteral("/fixtures/venues/grand-lobby.bld-venue"));
    EXPECT_TRUE(s.venue.has_value());
    s.backgroundImagePath = imagePath;
    s.backgroundImageOpacity = 0.3;
    s.backgroundImageRectStuds = QRectF(-10, -20, 300, 200);
}

QString writeImage(const QTemporaryDir& dir, const QByteArray& bytes) {
    const QString path = dir.filePath(QStringLiteral("floor.png"));
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(bytes);
    return path;
}

}  // namespace

TEST(ZipWriter, SafeZipReadsWhatItWrites) {
    ZipWriter w;
    const QByteArray text = QByteArray("<Brick id=\"1\" />\n").repeated(200);
    const QByteArray noise = [] {
        QByteArray b;
        quint32 x = 12345;
        for (int i = 0; i < 4096; ++i) b.append(static_cast<char>((x = x * 1103515245u + 12345u) >> 24));
        return b;
    }();
    w.add(QStringLiteral("text.xml"), text);
    w.add(QStringLiteral("noise.bin"), noise);
    w.add(QStringLiteral("stored.txt"), text, ZipWriter::Method::Stored);
    w.add(QStringLiteral("empty"), {});
    w.add(QStringLiteral("dossier/été.txt"), QByteArray("é"));
    const SafeZip zip(w.finish());
    ASSERT_TRUE(zip.isValid());
    ASSERT_EQ(zip.entries().size(), 5);
    EXPECT_EQ(zip.entries()[0].method, 8);  // text shrinks
    EXPECT_LT(zip.entries()[0].compressedSize, text.size() / 10);
    EXPECT_EQ(zip.entries()[1].method, 0);  // noise doesn't, so it's stored
    EXPECT_EQ(zip.entries()[2].method, 0);
    EXPECT_EQ(zip.read(zip.entries()[0]).value_or(QByteArray()), text);
    EXPECT_EQ(zip.read(zip.entries()[1]).value_or(QByteArray()), noise);
    EXPECT_EQ(zip.read(zip.entries()[2]).value_or(QByteArray()), text);
    EXPECT_EQ(zip.read(zip.entries()[3]).value_or(QByteArray("x")), QByteArray());
    const auto* utf8 = zip.find(QStringLiteral("dossier/été.txt"));
    ASSERT_NE(utf8, nullptr);
    EXPECT_EQ(zip.read(*utf8).value_or(QByteArray()), QByteArray("é"));
}

TEST(LayoutFile, KeepsTheWholeLayoutInOneFile) {
    QTemporaryDir dir;
    auto map = fixtureMap();
    ASSERT_TRUE(map);
    const QByteArray png = QByteArray("\x89PNG\r\n\x1a\n", 8) + QByteArray("pixels").repeated(40);
    addEverything(*map, writeImage(dir, png));

    const QString path = dir.filePath(QStringLiteral("layout.bld-layout"));
    QString error;
    QStringList warnings;
    ASSERT_TRUE(writeLayoutFile(*map, path, &error, &warnings)) << error.toStdString();
    EXPECT_TRUE(warnings.isEmpty());
    // One file: no sidecar beside it.
    EXPECT_FALSE(QFile::exists(saveload::sidecarPathFor(path)));

    const QByteArray original = layoutFileBytes(*map, &error);
    // The image moves away: the file must carry it, not point at it.
    QFile::remove(map->sidecar.backgroundImagePath);
    const QString assets = dir.filePath(QStringLiteral("assets"));
    const auto read = readLayoutFile(path, assets);
    ASSERT_TRUE(read.ok()) << read.error.toStdString();
    EXPECT_TRUE(read.warnings.isEmpty()) << read.warnings.join(QStringLiteral("; ")).toStdString();
    EXPECT_EQ(bbmOf(*read.map), bbmOf(*map));
    EXPECT_EQ(sidecarOf(*read.map), sidecarOf(*map));
    const QString image = read.map->sidecar.backgroundImagePath;
    EXPECT_TRUE(image.startsWith(assets)) << image.toStdString();
    EXPECT_TRUE(image.endsWith(QStringLiteral(".png")));
    QFile f(image);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_EQ(f.readAll(), png);

    // Saving what was read gives the same bytes.
    EXPECT_EQ(layoutFileBytes(*read.map, &error), original);
}

TEST(LayoutFile, EntriesAreTheDocumentedOnes) {
    QTemporaryDir dir;
    auto map = fixtureMap();
    ASSERT_TRUE(map);
    QString error;
    // Without labels, modules, venue or background: no sidecar.json.
    SafeZip plain(layoutFileBytes(*map, &error));
    ASSERT_TRUE(plain.isValid());
    ASSERT_EQ(plain.entries().size(), 2);
    EXPECT_EQ(plain.entries()[0].name, QStringLiteral("manifest.json"));
    EXPECT_EQ(plain.entries()[0].method, 0);
    EXPECT_EQ(plain.entries()[1].name, QStringLiteral("layout.bbm"));
    const QJsonObject manifest = QJsonDocument::fromJson(*plain.read(plain.entries()[0])).object();
    EXPECT_EQ(manifest.value(QLatin1String("format")).toString(), QStringLiteral("bld-layout"));
    EXPECT_EQ(manifest.value(QLatin1String("version")).toInt(), 1);
    EXPECT_EQ(*plain.read(plain.entries()[1]), bbmOf(*map));

    addEverything(*map, writeImage(dir, QByteArray("GIF89a")));
    SafeZip full(layoutFileBytes(*map, &error));
    ASSERT_NE(full.find(QStringLiteral("sidecar.json")), nullptr);
    ASSERT_NE(full.find(QStringLiteral("background.png")), nullptr);
    const QJsonObject sidecar =
        QJsonDocument::fromJson(*full.read(*full.find(QStringLiteral("sidecar.json")))).object();
    const QJsonObject bg = sidecar.value(QLatin1String("backgroundImage")).toObject();
    EXPECT_EQ(bg.value(QLatin1String("file")).toString(), QStringLiteral("background.png"));
    EXPECT_FALSE(bg.contains(QLatin1String("path")));  // no path from this machine
    EXPECT_FALSE(sidecar.contains(QLatin1String("bbmHashSha256")));
}

TEST(LayoutFile, AMissingBackgroundImageKeepsItsPathAndWarns) {
    QTemporaryDir dir;
    auto map = fixtureMap();
    ASSERT_TRUE(map);
    map->sidecar.backgroundImagePath = dir.filePath(QStringLiteral("gone.png"));
    QString error;
    QStringList warnings;
    const QByteArray bytes = layoutFileBytes(*map, &error, &warnings);
    ASSERT_FALSE(bytes.isEmpty());
    EXPECT_EQ(warnings.size(), 1);
    const auto read = readLayoutFileBytes(bytes, dir.filePath(QStringLiteral("assets")));
    ASSERT_TRUE(read.ok());
    EXPECT_EQ(read.map->sidecar.backgroundImagePath, map->sidecar.backgroundImagePath);
}

TEST(LayoutFile, RefusesWhatIsNotALayoutFile) {
    QTemporaryDir dir;
    const QString assets = dir.filePath(QStringLiteral("assets"));
    QFile bbm(kSource + QStringLiteral("/fixtures/bbm-corpus/tight-corner.bbm"));
    ASSERT_TRUE(bbm.open(QIODevice::ReadOnly));
    EXPECT_FALSE(readLayoutFileBytes(bbm.readAll(), assets).ok());

    ZipWriter other;  // a zip, but not ours
    other.add(QStringLiteral("manifest.json"), R"({"format":"something-else","version":1})");
    other.add(QStringLiteral("layout.bbm"), QByteArray("<Map/>"));
    const auto r = readLayoutFileBytes(other.finish(), assets);
    EXPECT_FALSE(r.ok());
    EXPECT_FALSE(r.error.isEmpty());

    ZipWriter empty;  // ours, with no layout in it
    empty.add(QStringLiteral("manifest.json"), R"({"format":"bld-layout","version":1})");
    EXPECT_FALSE(readLayoutFileBytes(empty.finish(), assets).ok());
}

TEST(LayoutFile, ANewerVersionOpensWithAWarning) {
    QTemporaryDir dir;
    auto map = fixtureMap();
    ASSERT_TRUE(map);
    ZipWriter w;
    w.add(QStringLiteral("manifest.json"), R"({"format":"bld-layout","version":2})");
    w.add(QStringLiteral("layout.bbm"), bbmOf(*map));
    w.add(QStringLiteral("parts/new-thing.xml"), QByteArray("<part/>"));
    const auto r = readLayoutFileBytes(w.finish(), dir.path());
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.warnings.size(), 1);
}

TEST(LayoutFile, RecognisesItsExtension) {
    EXPECT_TRUE(isLayoutFile(QStringLiteral("/x/Fordyce 2026.bld-layout")));
    EXPECT_TRUE(isLayoutFile(QStringLiteral("a.BLD-LAYOUT")));
    EXPECT_FALSE(isLayoutFile(QStringLiteral("a.bbm")));
    EXPECT_FALSE(isLayoutFile(QStringLiteral("a.bbm.bld")));
    EXPECT_FALSE(isLayoutFile(QStringLiteral("a.bld-venue")));
}

// fixtures/layouts/corner-lobby.bld-layout, which the web app reads too:
// tight-corner.bbm with a label, a module, the Grand Lobby venue and a 4x4
// background image. BLD_UPDATE_FIXTURES=1 writes it again.
TEST(LayoutFile, ReadsTheSharedFixture) {
    const QString fixture = kSource + QStringLiteral("/fixtures/layouts/corner-lobby.bld-layout");
    auto map = fixtureMap();
    ASSERT_TRUE(map);
    QTemporaryDir dir;
    if (qEnvironmentVariableIsSet("BLD_UPDATE_FIXTURES")) {
        QImage image(4, 4, QImage::Format_RGB32);
        image.fill(QColor(0x33, 0x66, 0x99));
        const QString png = dir.filePath(QStringLiteral("floor.png"));
        ASSERT_TRUE(image.save(png));
        QFile f(png);
        ASSERT_TRUE(f.open(QIODevice::ReadOnly));
        addEverything(*map, writeImage(dir, f.readAll()));
        QString error;
        ASSERT_TRUE(writeLayoutFile(*map, fixture, &error)) << error.toStdString();
    }
    const auto read = readLayoutFile(fixture, dir.filePath(QStringLiteral("assets")));
    ASSERT_TRUE(read.ok()) << read.error.toStdString();
    EXPECT_TRUE(read.warnings.isEmpty());
    EXPECT_EQ(bbmOf(*read.map), bbmOf(*map));
    const auto& s = read.map->sidecar;
    ASSERT_EQ(s.anchoredLabels.size(), 1u);
    EXPECT_EQ(s.anchoredLabels[0].text, QStringLiteral("Grand Lobby — north end"));
    ASSERT_EQ(s.modules.size(), 1u);
    EXPECT_EQ(s.modules[0].name, QStringLiteral("Corner"));
    ASSERT_TRUE(s.venue.has_value());
    EXPECT_EQ(s.venue->name, saveload::readVenueFile(kSource + QStringLiteral("/fixtures/venues/grand-lobby.bld-venue"))->name);
    EXPECT_DOUBLE_EQ(s.backgroundImageOpacity, 0.3);
    EXPECT_EQ(s.backgroundImageRectStuds, QRectF(-10, -20, 300, 200));
    EXPECT_EQ(QImage(s.backgroundImagePath).size(), QSize(4, 4));
}

// fixtures/layouts/web-made.bld-layout: corner-lobby.bld-layout opened by
// the web app and downloaded again (its apps/web/scripts/make-web-made-layout.ts).
// The web writes its own sidecar JSON; the layout must come back the same.
TEST(LayoutFile, ReadsTheWebMadeFixture) {
    QTemporaryDir dir;
    const QString fixtures = kSource + QStringLiteral("/fixtures/layouts/");
    const auto web = readLayoutFile(fixtures + QStringLiteral("web-made.bld-layout"), dir.filePath(QStringLiteral("web")));
    const auto desktop =
        readLayoutFile(fixtures + QStringLiteral("corner-lobby.bld-layout"), dir.filePath(QStringLiteral("desktop")));
    ASSERT_TRUE(web.ok()) << web.error.toStdString();
    ASSERT_TRUE(desktop.ok());
    EXPECT_TRUE(web.warnings.isEmpty()) << web.warnings.join(QStringLiteral("; ")).toStdString();
    EXPECT_EQ(bbmOf(*web.map), bbmOf(*desktop.map));
    EXPECT_EQ(sidecarOf(*web.map), sidecarOf(*desktop.map));
    QFile a(web.map->sidecar.backgroundImagePath), b(desktop.map->sidecar.backgroundImagePath);
    ASSERT_TRUE(a.open(QIODevice::ReadOnly));
    ASSERT_TRUE(b.open(QIODevice::ReadOnly));
    EXPECT_EQ(a.readAll(), b.readAll());
}
