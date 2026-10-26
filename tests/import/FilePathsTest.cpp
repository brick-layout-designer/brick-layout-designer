// Files in folders with names people really use, as Windows has them: not
// ASCII ("Clubs – Ünïcødé 日本"), with spaces, and paths longer than
// Windows' old 260-character limit. Every kind of file the app saves and
// reads (a .bbm and its sidecar, a .bld-layout with its picture, a venue,
// a parts library folder) goes there and comes back the same. CI runs
// these on Windows, macOS and Linux.

#include "import/LayoutFile.h"

#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"
#include "saveload/BbmWriter.h"
#include "saveload/SidecarIO.h"
#include "saveload/VenueIO.h"

#include <gtest/gtest.h>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace bld;

namespace {

const QString kSource = QStringLiteral(BLD_SOURCE_DIR);

QByteArray bbmOf(const core::Map& map) {
    QBuffer b;
    b.open(QIODevice::WriteOnly);
    EXPECT_TRUE(saveload::writeBbm(map, b).ok);
    return b.data();
}

QByteArray sidecarOf(const core::Map& map) {
    QJsonObject o = saveload::sidecarToJson(map.sidecar);
    // Where the files are differs by design.
    o.remove(QStringLiteral("bbmHashSha256"));
    QJsonObject bg = o.value(QLatin1String("backgroundImage")).toObject();
    bg.remove(QStringLiteral("path"));
    o[QStringLiteral("backgroundImage")] = bg;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

std::unique_ptr<core::Map> sampleMap() {
    auto r = saveload::readBbm(kSource + QStringLiteral("/fixtures/bbm-corpus/tight-corner.bbm"));
    EXPECT_TRUE(r.ok()) << r.error.toStdString();
    if (!r.map) return nullptr;
    auto& s = r.map->sidecar;
    s.venue = saveload::readVenueFile(kSource + QStringLiteral("/fixtures/venues/grand-lobby.bld-venue"));
    EXPECT_TRUE(s.venue.has_value());
    core::Module module;
    module.id = QStringLiteral("M1");
    module.name = QStringLiteral("Bahnhof Süd – 駅");
    s.modules.push_back(module);
    return std::move(r.map);
}

struct Place {
    const char* what;
    QString folder;
};

// The folders, under `root`: one with letters from several scripts, one
// with spaces, and one whose files are over 300 characters long.
std::vector<Place> places(const QString& root) {
    QString deep = root + QStringLiteral("/Long path");
    while (deep.size() < 280) deep += QStringLiteral("/Layouts for the spring show (draft)");
    return { { "unicode", root + QStringLiteral("/Clubs – Ünïcødé 日本 ✓") },
             { "spaces", root + QStringLiteral("/My Documents/Brick  Layouts") },
             { "long", deep } };
}

}  // namespace

TEST(FilePaths, ABbmAndItsSidecarSaveAndOpenAnywhere) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    auto map = sampleMap();
    ASSERT_TRUE(map);
    for (const Place& p : places(root.path())) {
        SCOPED_TRACE(p.what);
        ASSERT_TRUE(QDir().mkpath(p.folder));
        const QString bbm = p.folder + QStringLiteral("/Bahnhof Süd 2026 – v2.bbm");
        if (QString(p.what) == QLatin1String("long")) {
            EXPECT_GT(bbm.size(), 300);
        }
        ASSERT_TRUE(saveload::writeBbm(*map, bbm).ok);
        const QString sidecar = saveload::sidecarPathFor(bbm);
        EXPECT_EQ(QFileInfo(sidecar).absolutePath(), QFileInfo(bbm).absolutePath());
        QFile written(bbm);
        ASSERT_TRUE(written.open(QIODevice::ReadOnly)) << written.errorString().toStdString();
        const QByteArray bytes = written.readAll();
        QString error;
        ASSERT_TRUE(saveload::writeSidecar(sidecar, bytes, map->sidecar, &error)) << error.toStdString();
        const auto read = saveload::readBbm(bbm);
        ASSERT_TRUE(read.ok()) << read.error.toStdString();
        EXPECT_EQ(bbmOf(*read.map), bbmOf(*map));
        core::Map withSidecar;
        const auto side = saveload::readSidecar(sidecar, bytes, withSidecar.sidecar);
        ASSERT_TRUE(side.ok) << side.error.toStdString();
        EXPECT_FALSE(side.hashMismatch);
        EXPECT_EQ(sidecarOf(withSidecar), sidecarOf(*map));
    }
}

TEST(FilePaths, ALayoutFileAndItsPictureSaveAndOpenAnywhere) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    for (const Place& p : places(root.path())) {
        SCOPED_TRACE(p.what);
        ASSERT_TRUE(QDir().mkpath(p.folder));
        auto map = sampleMap();
        ASSERT_TRUE(map);
        // The floor picture lives there too.
        const QByteArray png = QByteArray("\x89PNG\r\n\x1a\n", 8) + QByteArray("pixels").repeated(40);
        const QString picture = p.folder + QStringLiteral("/Hallenplan Ü.png");
        {
            QFile f(picture);
            ASSERT_TRUE(f.open(QIODevice::WriteOnly)) << f.errorString().toStdString();
            f.write(png);
        }
        map->sidecar.backgroundImagePath = picture;
        map->sidecar.backgroundImageRectStuds = QRectF(0, 0, 100, 50);
        const QString path = p.folder + QStringLiteral("/Bahnhof Süd 2026.bld-layout");
        QString error;
        ASSERT_TRUE(import::writeLayoutFile(*map, path, &error)) << error.toStdString();
        ASSERT_TRUE(import::isLayoutFile(path));
        // Its picture comes out in an assets folder beside it.
        const QString assets = p.folder + QStringLiteral("/Bilder – assets");
        const auto read = import::readLayoutFile(path, assets);
        ASSERT_TRUE(read.ok()) << read.error.toStdString();
        EXPECT_EQ(bbmOf(*read.map), bbmOf(*map));
        EXPECT_EQ(sidecarOf(*read.map), sidecarOf(*map));
        const QString extracted = read.map->sidecar.backgroundImagePath;
        ASSERT_FALSE(extracted.isEmpty());
        EXPECT_TRUE(QDir::cleanPath(extracted).startsWith(QDir::cleanPath(assets))) << extracted.toStdString();
        QFile f(extracted);
        ASSERT_TRUE(f.open(QIODevice::ReadOnly)) << extracted.toStdString();
        EXPECT_EQ(f.readAll(), png);
    }
}

TEST(FilePaths, AVenueSavesAndOpensAnywhere) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const auto venue = saveload::readVenueFile(kSource + QStringLiteral("/fixtures/venues/grand-lobby.bld-venue"));
    ASSERT_TRUE(venue);
    for (const Place& p : places(root.path())) {
        SCOPED_TRACE(p.what);
        ASSERT_TRUE(QDir().mkpath(p.folder));
        const QString path = p.folder + QStringLiteral("/Stadthalle – Saal 1.bld-venue");
        QString error;
        ASSERT_TRUE(saveload::writeVenueFile(path, *venue, &error)) << error.toStdString();
        const auto back = saveload::readVenueFile(path, &error);
        ASSERT_TRUE(back) << error.toStdString();
        EXPECT_EQ(back->name, venue->name);
        EXPECT_EQ(back->edges.size(), venue->edges.size());
        EXPECT_EQ(back->obstacles.size(), venue->obstacles.size());
    }
}

// A parts library folder (your own, or the server's parts) under such a
// path is found and read.
TEST(FilePaths, APartsLibraryFolderIsFoundAnywhere) {
    const QString source = kSource + QStringLiteral("/parts/BlueBrickParts/parts/Baseplate");
    if (!QFile::exists(source + QStringLiteral("/3811.1.xml")))
        GTEST_SKIP() << "the BlueBrickParts library isn't checked out";
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    for (const Place& p : places(root.path())) {
        SCOPED_TRACE(p.what);
        const QString folder = p.folder + QStringLiteral("/Meine Teile – parts");
        ASSERT_TRUE(QDir().mkpath(folder));
        for (const char* ext : { ".xml", ".gif" })
            ASSERT_TRUE(QFile::copy(source + QStringLiteral("/3811.1") + QLatin1String(ext),
                                    folder + QStringLiteral("/3811.1") + QLatin1String(ext)));
        parts::PartsLibrary lib;
        lib.addSearchPath(p.folder);
        EXPECT_EQ(lib.scan(), 1);
        const auto meta = lib.metadata(QStringLiteral("3811.1"));
        ASSERT_TRUE(meta);
        EXPECT_FALSE(meta->descriptions.isEmpty());
    }
}
