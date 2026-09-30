// The native layout file (.bld-layout) and the ZIP writer under it.

#include "import/LayoutFile.h"
#include "import/zip/SafeZip.h"
#include "import/zip/ZipWriter.h"

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"
#include "saveload/BbmWriter.h"
#include "saveload/SidecarIO.h"
#include "saveload/VenueIO.h"

#include <gtest/gtest.h>

#include <QBuffer>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

namespace {

void writeFile(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(data);
}

QByteArray leafXml(const char* author) {
    return QByteArray("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<part>\n\t<Author>") + author
         + "</Author>\n\t<Description>\n\t\t<en>Test part</en>\n\t</Description>\n</part>\n";
}

QByteArray setXml(const QStringList& parts) {
    QByteArray x = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<group>\n\t<Author>Me</Author>\n\t<SubPartList>\n";
    for (const auto& p : parts)
        x += "\t\t<SubPart id=\"" + p.toUtf8()
           + "\">\n\t\t\t<position>\n\t\t\t\t<x>0</x>\n\t\t\t\t<y>0</y>\n\t\t\t</position>\n\t\t\t<angle>0</angle>\n\t\t</SubPart>\n";
    return x + "\t</SubPartList>\n</group>\n";
}

// A bundled library (std) with STD.1 and a user library (mine) with MINE.1
// (a .png and a .gif), SUB.1 and the set KIT.1 (KIT.1.set.xml) of STD.1 and SUB.1.
struct TwoLibraries {
    QTemporaryDir dir;
    QString std = dir.filePath(QStringLiteral("std"));
    QString mine = dir.filePath(QStringLiteral("mine"));
    parts::PartsLibrary library;
    TwoLibraries() {
        writeFile(std + QStringLiteral("/Track/STD.1.xml"), leafXml("Bundled"));
        writeFile(std + QStringLiteral("/Track/STD.1.gif"), QByteArray("GIF89a-std"));
        writeFile(mine + QStringLiteral("/MINE.1.xml"), leafXml("Me"));
        writeFile(mine + QStringLiteral("/MINE.1.png"), QByteArray("\x89PNG-mine"));
        writeFile(mine + QStringLiteral("/MINE.1.gif"), QByteArray("GIF89a-mine"));
        writeFile(mine + QStringLiteral("/sub/SUB.1.xml"), leafXml("Me"));
        writeFile(mine + QStringLiteral("/sub/SUB.1.gif"), QByteArray("GIF89a-sub"));
        writeFile(mine + QStringLiteral("/KIT.1.set.xml"), setXml({ QStringLiteral("STD.1"), QStringLiteral("SUB.1") }));
        library.addSearchPath(std);
        library.addSearchPath(mine);
        library.scan();
    }
};

std::unique_ptr<core::Map> mapUsing(const QStringList& bricks, const QString& group = {}) {
    auto map = std::make_unique<core::Map>();
    auto layer = std::make_unique<core::LayerBrick>();
    for (const auto& p : bricks) {
        core::Brick b;
        b.partNumber = p;
        layer->bricks.push_back(b);
    }
    if (!group.isEmpty()) {
        core::Group g;
        g.partNumber = group;
        layer->groups.push_back(g);
    }
    map->layers().push_back(std::move(layer));
    return map;
}

}  // namespace

TEST(LayoutFile, CarriesThePartsOutsideTheBundledLibrary) {
    TwoLibraries libs;
    ASSERT_TRUE(libs.library.metadata(QStringLiteral("KIT.1")).has_value());
    const auto map = mapUsing({ QStringLiteral("STD.1"), QStringLiteral("MINE.1"), QStringLiteral("MINE.1") },
                              QStringLiteral("KIT.1"));
    const auto files = layoutPartFiles(*map, libs.library, libs.std);
    EXPECT_EQ(files.keys(), (QStringList{ QStringLiteral("KIT.1.set.xml"), QStringLiteral("MINE.1.gif"),
                                          QStringLiteral("MINE.1.png"), QStringLiteral("MINE.1.xml"),
                                          QStringLiteral("SUB.1.gif"), QStringLiteral("SUB.1.xml") }));
    EXPECT_EQ(files.value(QStringLiteral("MINE.1.png")), QByteArray("\x89PNG-mine"));

    QString error;
    const QByteArray bytes = layoutFileBytes(*map, &error, nullptr, files);
    const SafeZip zip(bytes);
    ASSERT_NE(zip.find(QStringLiteral("parts/MINE.1.xml")), nullptr);
    EXPECT_EQ(zip.find(QStringLiteral("parts/MINE.1.png"))->method, 0);  // images stored
    const auto read = readLayoutFileBytes(bytes, libs.dir.filePath(QStringLiteral("assets")));
    ASSERT_TRUE(read.ok());
    EXPECT_EQ(read.partFiles, files);
}

TEST(LayoutFile, OpeningTakesInOnlyThePartsTheLibraryLacks) {
    TwoLibraries source;
    const auto map = mapUsing({ QStringLiteral("MINE.1") }, QStringLiteral("KIT.1"));
    auto files = layoutPartFiles(*map, source.library, source.std);

    // Another machine: the bundled part, and its own SUB.1 that differs.
    QTemporaryDir other;
    writeFile(other.filePath(QStringLiteral("std/STD.1.xml")), leafXml("Bundled"));
    writeFile(other.filePath(QStringLiteral("own/SUB.1.xml")), leafXml("Someone else"));
    parts::PartsLibrary library;
    library.addSearchPath(other.filePath(QStringLiteral("std")));
    library.addSearchPath(other.filePath(QStringLiteral("own")));
    library.scan();

    const QString dir = other.filePath(QStringLiteral("layout-parts"));
    const auto installed = installLayoutParts(files, dir, library);
    EXPECT_EQ(installed.newParts, (QStringList{ dir + QStringLiteral("/KIT.1.set.xml"), dir + QStringLiteral("/MINE.1.xml") }));
    EXPECT_EQ(installed.differing, QStringList{ QStringLiteral("SUB.1") });
    EXPECT_TRUE(installed.failed.isEmpty());
    EXPECT_TRUE(QFile::exists(dir + QStringLiteral("/MINE.1.png")));
    EXPECT_TRUE(QFile::exists(dir + QStringLiteral("/MINE.1.gif")));
    EXPECT_FALSE(QFile::exists(dir + QStringLiteral("/SUB.1.xml")));  // theirs is kept

    // Taken in, the parts are the library's: opening again adds nothing.
    for (const auto& xml : installed.newParts) library.scanFile(xml);
    ASSERT_TRUE(library.metadata(QStringLiteral("MINE.1")).has_value());
    EXPECT_FALSE(library.metadata(QStringLiteral("MINE.1"))->gifFilePath.isEmpty());
    const auto again = installLayoutParts(files, dir, library);
    EXPECT_TRUE(again.newParts.isEmpty());
    EXPECT_EQ(again.differing, QStringList{ QStringLiteral("SUB.1") });
}

TEST(LayoutFile, PartFileNamesStayInTheirFolder) {
    for (const char* ok : { "MINE.1.xml", "3001.1.gif", "KIT.set.xml", "Track 18 #2.8.png", "x.JPEG" })
        EXPECT_TRUE(isLayoutPartFileName(QString::fromUtf8(ok))) << ok;
    for (const char* bad : { "../evil.xml", "sub/MINE.1.xml", "sub\\MINE.1.xml", ".hidden.xml", "C:evil.xml",
                             "MINE.1.exe", "MINE.1.xml.sh", "" })
        EXPECT_FALSE(isLayoutPartFileName(QString::fromUtf8(bad))) << bad;

    // A file carrying such a name: read without it, with a warning; nothing written.
    QTemporaryDir dir;
    auto map = mapUsing({});
    QBuffer bbm;
    bbm.open(QIODevice::WriteOnly);
    ASSERT_TRUE(saveload::writeBbm(*map, bbm).ok);
    ZipWriter w;
    w.add(QStringLiteral("manifest.json"), R"({"format":"bld-layout","version":1})");
    w.add(QStringLiteral("layout.bbm"), bbm.data());
    w.add(QStringLiteral("parts/../../evil.xml"), leafXml("x"));
    w.add(QStringLiteral("parts/GOOD.1.xml"), leafXml("x"));
    const auto r = readLayoutFileBytes(w.finish(), dir.path());
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.partFiles.keys(), QStringList{ QStringLiteral("GOOD.1.xml") });
    EXPECT_EQ(r.warnings.size(), 1);
}

// fixtures/layouts/with-parts.bld-layout, which the web app opens too: one
// brick of CLDTEST.1, a part of the user's own that the file carries.
// BLD_UPDATE_FIXTURES=1 writes it again.
TEST(LayoutFile, ReadsTheSharedFixtureWithParts) {
    const QString fixture = kSource + QStringLiteral("/fixtures/layouts/with-parts.bld-layout");
    QTemporaryDir dir;
    const QString mine = dir.filePath(QStringLiteral("mine"));
    QImage sprite(32, 16, QImage::Format_ARGB32);
    sprite.fill(QColor(0xcc, 0x33, 0x33));
    writeFile(mine + QStringLiteral("/CLDTEST.1.xml"), leafXml("Brick Layout Designer tests"));
    ASSERT_TRUE(sprite.save(mine + QStringLiteral("/CLDTEST.1.png")));
    parts::PartsLibrary library;
    library.addSearchPath(mine);
    library.scan();
    const auto map = mapUsing({ QStringLiteral("CLDTEST.1") });
    auto& layer = static_cast<core::LayerBrick&>(*map->layers().front());
    layer.guid = QStringLiteral("11111111-1111-1111-1111-111111111111");
    layer.name = QStringLiteral("Layer 1");
    layer.bricks.front().guid = QStringLiteral("22222222-2222-2222-2222-222222222222");
    layer.bricks.front().displayArea = QRectF(0, 0, 4, 2);
    map->nbItems = 1;
    if (qEnvironmentVariableIsSet("BLD_UPDATE_FIXTURES")) {
        QString error;
        ASSERT_TRUE(writeLayoutFile(*map, fixture, &error, nullptr, layoutPartFiles(*map, library, {})))
            << error.toStdString();
    }
    const auto read = readLayoutFile(fixture, dir.filePath(QStringLiteral("assets")));
    ASSERT_TRUE(read.ok()) << read.error.toStdString();
    ASSERT_EQ(read.map->layers().size(), 1u);
    const auto& bricks = static_cast<const core::LayerBrick&>(*read.map->layers().front()).bricks;
    ASSERT_EQ(bricks.size(), 1u);
    EXPECT_EQ(bricks.front().partNumber, QStringLiteral("CLDTEST.1"));
    EXPECT_EQ(read.partFiles.keys(), (QStringList{ QStringLiteral("CLDTEST.1.png"), QStringLiteral("CLDTEST.1.xml") }));
    EXPECT_EQ(read.partFiles.value(QStringLiteral("CLDTEST.1.xml")), leafXml("Brick Layout Designer tests"));
    EXPECT_EQ(QImage::fromData(read.partFiles.value(QStringLiteral("CLDTEST.1.png"))).size(), QSize(32, 16));
}
