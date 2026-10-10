// LDraw -> custom library part, end to end against stub libraries:
// top-down orientation of the rendered sprite, part-number resolution,
// BlueBrick-compatible placement (incl. <LDraw> remaps), snap
// connections, and the written part files.

#include "import/ImportConnections.h"
#include "import/ImportToPart.h"
#include "import/ldraw/LDrawLibrary.h"
#include "import/ldraw/LDrawMeshBuilder.h"
#include "import/ldraw/LDrawMeshLoader.h"
#include "import/ldraw/LDrawPalette.h"
#include "import/ldraw/LDrawReader.h"
#include "import/mesh/MeshRasterize.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>

using namespace bld;

namespace {

struct Tree {
    QTemporaryDir dir;
    QString path(const QString& rel) const { return QDir(dir.path()).absoluteFilePath(rel); }
    void write(const QString& rel, const QByteArray& body) {
        QDir().mkpath(QFileInfo(path(rel)).absolutePath());
        QFile f(path(rel));
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(body);
    }
};

// Library part "TT.7": a 4-stud straight with a connection at each end.
QByteArray trackXml(const QByteArray& extra = {}) {
    return "<part><Author>t</Author><ConnexionList>"
           "<connexion><type>1</type><position><x>-2</x><y>0</y></position><angle>180</angle></connexion>"
           "<connexion><type>1</type><position><x>2</x><y>0</y></position><angle>0</angle></connexion>"
           "</ConnexionList>" + extra + "</part>";
}

const core::LayerBrick& bricksOf(const core::Map& map) {
    return static_cast<const core::LayerBrick&>(*map.layers().front());
}

import::LDrawReadResult readModel(Tree& t, const QByteArray& body) {
    t.write(QStringLiteral("model.ldr"), body);
    return import::readLDraw(t.path(QStringLiteral("model.ldr")));
}

}  // namespace

TEST(ImportPipeline, SpriteIsTopViewSeenFromAbove) {
    Tree ld;
    // Blue base at y=0 over the whole 4x4 studs; red plate above it
    // (LDraw is -Y up, so y=-8 is higher) over the back half (-Z).
    ld.write(QStringLiteral("parts/probe.dat"),
             "4 1 -40 0 -40 40 0 -40 40 0 40 -40 0 40\n"
             "4 4 -40 -8 -40 40 -8 -40 40 -8 0 -40 -8 0\n");
    ld.write(QStringLiteral("LDConfig.ldr"), "0 stub\n");
    Tree m;
    const auto read = readModel(m, "1 16 0 0 0 1 0 0 0 1 0 0 0 1 probe.dat\n");
    import::LDrawLibrary lib(ld.dir.path());
    import::LDrawPalette pal;
    import::LDrawMeshLoader loader(lib, pal);
    const auto baked = import::bakeMeshFromLDraw(read, loader, pal);
    import::RasterizeOptions opt;
    opt.marginPx = 0;
    opt.ssaa = 1;
    const auto r = import::rasterizeMeshTopDown(baked.mesh, opt);
    ASSERT_EQ(r.image.size(), QSize(32, 32));
    const QImage img = r.image.convertToFormat(QImage::Format_ARGB32);
    // Viewed from above, the back (-Z) is at the bottom of the image and
    // the red plate covers the blue base there.
    const QColor back = img.pixelColor(16, 24), front = img.pixelColor(16, 8);
    EXPECT_GT(back.red(), 150);
    EXPECT_LT(back.blue(), 80);
    EXPECT_GT(front.blue(), 120);
    EXPECT_LT(front.red(), 80);
}

TEST(ImportPipeline, FormerNamesFollowMovedToChains) {
    Tree ld;
    ld.write(QStringLiteral("parts/new1.dat"), "0 New\n");
    ld.write(QStringLiteral("parts/old1.dat"), "0 ~Moved to new1\n1 16 0 0 0 1 0 0 0 1 0 0 0 1 new1.dat\n");
    ld.write(QStringLiteral("parts/older.dat"), "0 ~Moved to OLD1\n");
    import::LDrawLibrary lib(ld.dir.path());
    EXPECT_EQ(lib.formerNames(QStringLiteral("NEW1.dat")),
              (QStringList{ QStringLiteral("old1"), QStringLiteral("older") }));
    EXPECT_TRUE(lib.formerNames(QStringLiteral("older")).isEmpty());
}

TEST(ImportPipeline, FreeConnectionsOfJoinedTrack) {
    Tree p;
    p.write(QStringLiteral("TT.7.xml"), trackXml());
    parts::PartsLibrary parts;
    parts.addSearchPath(p.dir.path());
    parts.scan();

    Tree m;
    // Two straights end to end (4 studs = 80 LDU apart), in a color the
    // library doesn't have (falls back to TT.7).
    const auto read = readModel(m,
        "1 8 0 0 0 1 0 0 0 1 0 0 0 1 tt.dat\n"
        "1 8 80 0 0 1 0 0 0 1 0 0 0 1 tt.dat\n");
    auto map = import::toBlueBrickMap(read, &parts);
    ASSERT_EQ(bricksOf(*map).bricks.size(), 2u);
    EXPECT_EQ(bricksOf(*map).bricks[0].partNumber, QStringLiteral("TT.7"));

    const auto conns = import::externalConnections(*map, parts, QPointF(2, 0));
    ASSERT_EQ(conns.size(), 2);
    EXPECT_NEAR(conns[0].xStuds, -4.0, 1e-6);
    EXPECT_NEAR(conns[0].yStuds, 0.0, 1e-6);
    EXPECT_NEAR(conns[0].angleDeg, 180.0, 1e-6);
    EXPECT_NEAR(conns[1].xStuds, 4.0, 1e-6);
    EXPECT_NEAR(conns[1].angleDeg, 0.0, 1e-6);
}

TEST(ImportPipeline, RotatedPartConnectionsFollowGeometry) {
    Tree p;
    p.write(QStringLiteral("TT.7.xml"), trackXml());
    parts::PartsLibrary parts;
    parts.addSearchPath(p.dir.path());
    parts.scan();

    Tree m;
    // 90° about Y: the part's +X end (x=40) lands at LDraw z=-40, i.e.
    // top-down y = +2 studs.
    const auto read = readModel(m, "1 7 0 0 0 0 0 1 0 1 0 -1 0 0 tt.dat\n");
    auto map = import::toBlueBrickMap(read, &parts);
    const auto conns = import::externalConnections(*map, parts, QPointF(0, 0));
    ASSERT_EQ(conns.size(), 2);
    EXPECT_NEAR(conns[1].xStuds, 0.0, 1e-6);
    EXPECT_NEAR(conns[1].yStuds, 2.0, 1e-6);
    EXPECT_NEAR(conns[1].angleDeg, 90.0, 1e-6);
    EXPECT_NEAR(conns[0].yStuds, -2.0, 1e-6);
}

TEST(ImportPipeline, RemapTranslationAndStaleRemapFallback) {
    Tree p;
    // LDraw origin sits at the part's left end: image centre = +40 LDU.
    p.write(QStringLiteral("TT.7.xml"),
            trackXml("<LDraw><Translation><x>-40</x><y>0</y></Translation></LDraw>"));
    parts::PartsLibrary parts;
    parts.addSearchPath(p.dir.path());
    parts.scan();
    ASSERT_DOUBLE_EQ(parts.metadata(QStringLiteral("tt.7"))->ldrawTranslation.x(), -40.0);

    Tree m;
    const auto read = readModel(m, "1 7 0 0 0 1 0 0 0 1 0 0 0 1 tt.dat\n");
    EXPECT_NEAR(bricksOf(*import::toBlueBrickMap(read, &parts)).bricks[0].displayArea.center().x(), 2.0, 1e-6);

    // Geometry agrees with the remap (spans x 0..80): remap kept.
    Tree agree;
    agree.write(QStringLiteral("parts/tt.dat"), "4 7 0 0 -20 80 0 -20 80 0 20 0 0 20\n");
    import::LDrawLibrary agreeLib(agree.dir.path());
    import::LDrawPalette pal;
    import::LDrawMeshLoader agreeLoader(agreeLib, pal);
    EXPECT_NEAR(bricksOf(*import::toBlueBrickMap(read, &parts, &agreeLib, &agreeLoader))
                    .bricks[0].displayArea.center().x(), 2.0, 1e-6);

    // Part re-origined since the remap was authored (spans -40..40):
    // the geometry centre wins.
    Tree moved;
    moved.write(QStringLiteral("parts/tt.dat"), "4 7 -40 0 -20 40 0 -20 40 0 20 -40 0 20\n");
    import::LDrawLibrary movedLib(moved.dir.path());
    import::LDrawMeshLoader movedLoader(movedLib, pal);
    EXPECT_NEAR(bricksOf(*import::toBlueBrickMap(read, &parts, &movedLib, &movedLoader))
                    .bricks[0].displayArea.center().x(), 0.0, 1e-6);
}

TEST(ImportPipeline, HiResPartWritesPngAndVanillaGif) {
    if (!QImageReader::supportedImageFormats().contains("gif")) GTEST_SKIP() << "no Qt GIF reader";
    Tree out;
    QImage sprite(4 * 32, 2 * 32, QImage::Format_ARGB32);
    sprite.fill(QColor(200, 40, 40));
    const QVector<import::ImportedConnection> conns{ { QStringLiteral("1"), -2, 0, 180 },
                                                     { QStringLiteral("1"), 2, 0, 0 } };
    QString err;
    const QString key = import::writeImportedModelAsLibraryPart(
        QStringLiteral("/models/My Loop.ldr"), sprite, 4, 2, out.dir.path(), {}, conns, &err);
    ASSERT_EQ(key, QStringLiteral("My_Loop")) << err.toStdString();
    ASSERT_TRUE(QFile::exists(out.path(key + QStringLiteral(".png"))));
    const QImage gif(out.path(key + QStringLiteral(".gif")));
    EXPECT_EQ(gif.size(), QSize(32, 16));

    parts::PartsLibrary lib;
    lib.addSearchPath(out.dir.path());
    lib.scan();
    auto meta = lib.metadata(key);
    ASSERT_TRUE(meta);
    EXPECT_TRUE(meta->gifFilePath.endsWith(QStringLiteral(".png")));
    EXPECT_EQ(meta->pxPerStud, 32);
    EXPECT_EQ(meta->connections.size(), 2);

    // Without the .png (e.g. copied to a vanilla install) the .gif is
    // used at its native 8 px/stud.
    QFile::remove(out.path(key + QStringLiteral(".png")));
    parts::PartsLibrary vanilla;
    vanilla.addSearchPath(out.dir.path());
    vanilla.scan();
    meta = vanilla.metadata(key);
    ASSERT_TRUE(meta);
    EXPECT_TRUE(meta->gifFilePath.endsWith(QStringLiteral(".gif")));
    EXPECT_EQ(meta->pxPerStud, 8);
}

TEST(ImportPipeline, NativeResolutionPartWritesGifOnly) {
    if (!QImageReader::supportedImageFormats().contains("gif")) GTEST_SKIP() << "no Qt GIF reader";
    Tree out;
    QImage sprite(16, 8, QImage::Format_ARGB32);
    sprite.fill(Qt::darkGray);
    const QString key = import::writeImportedModelAsLibraryPart(
        QStringLiteral("small.ldr"), sprite, 2, 1, out.dir.path(), {});
    ASSERT_FALSE(key.isEmpty());
    EXPECT_TRUE(QFile::exists(out.path(key + QStringLiteral(".gif"))));
    EXPECT_FALSE(QFile::exists(out.path(key + QStringLiteral(".png"))));
}
