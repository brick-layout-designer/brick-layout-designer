// LDD -> LDraw conversion checked against the community lxf2ldr
// converter (https://gitlab.com/sylvainls/lxf2ldr.html): the expected
// lines below are lxf2ldr's own output for the same LXFML + ldraw.xml.
// Also covers locating LDD's brick database (LDDAssets).

#include "import/ldd/LDDAssets.h"
#include "import/ldd/LDDLDrawMapping.h"
#include "import/ldd/LDDReader.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

using namespace bld;

namespace {

void writeFile(const QString& path, const QByteArray& body) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(body);
}

// Excerpt of the community ldraw.xml (2025-08-11).
const QByteArray kMapping = R"XML(<LDrawMapping versionMajor="1" versionMinor="7">
  <Material ldraw="15" lego="1" />
  <Material ldraw="496" lego="194" />
  <Material ldraw="71" lego="194" />
  <Brick ldraw="3710.dat" lego="3710" />
  <Brick ldraw="2865.dat" lego="74746" />
  <Brick ldraw="74746.dat" lego="74746" />
  <Transformation ldraw="3710.dat" tx="-1.2" ty="-0.32" tz="0" ax="1" ay="0" az="0" angle="0" />
  <Transformation ldraw="74746.dat" tx="6" ty="-0.32" tz="2.8" ax="0" ay="1" az="0" angle="1.570796" />
</LDrawMapping>)XML";

// A plate from LDD's 10173 Holiday Train model plus two joined 9V straights.
const QByteArray kModel = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<LXFML versionMajor="5" versionMinor="0" name="test">
  <Bricks>
    <Brick refID="0" designID="3710"><Part refID="0" designID="3710" materials="1">
      <Bone refID="0" transformation="-1,0,0,0,1,0,0,0,-1,-7.5999922752380371,7.9730887413024902,-17.117975234985352"/>
    </Part></Brick>
    <Brick refID="1" designID="74746"><Part refID="1" designID="74746" materials="194">
      <Bone refID="1" transformation="1,0,0,0,1,0,0,0,1,0,0,0"/>
    </Part></Brick>
    <Brick refID="2" designID="74746"><Part refID="2" designID="74746" materials="194">
      <Bone refID="2" transformation="1,0,0,0,1,0,0,0,1,0,0,12.8"/>
    </Part></Brick>
  </Bricks>
</LXFML>)XML";

struct Expected {
    int colour;
    double pos[3];
    double m[9];
    const char* file;
};

}  // namespace

TEST(LDDConversion, MatchesLxf2ldr) {
    QTemporaryDir dir;
    writeFile(dir.filePath(QStringLiteral("ldraw.xml")), kMapping);
    writeFile(dir.filePath(QStringLiteral("model.lxfml")), kModel);
    import::LDDLDrawMapping mapping;
    ASSERT_TRUE(mapping.loadFromFile(dir.filePath(QStringLiteral("ldraw.xml"))));
    const auto ldd = import::readLDD(dir.filePath(QStringLiteral("model.lxfml")));
    ASSERT_TRUE(ldd.ok) << ldd.error.toStdString();
    const auto ldraw = mapping.toLDraw(ldd);
    EXPECT_FALSE(ldraw.lddAxes);

    const Expected expected[] = {
        { 15, { -219.9998068809509, -207.32721853256226, 427.9493808746338 },
          { -1, 0, 0, 0, 1, 0, 0, 0, -1 }, "3710.dat" },
        { 71, { 69.99995098076177, -8, 150.00002287563476 },
          { 0, 0, 1, 0, 1, 0, -1, 0, 0 }, "74746.dat" },
        { 71, { 69.99995098076177, -8, -169.99997712436527 },
          { 0, 0, 1, 0, 1, 0, -1, 0, 0 }, "74746.dat" },
    };
    ASSERT_EQ(ldraw.parts.size(), 3u);
    for (size_t i = 0; i < 3; ++i) {
        SCOPED_TRACE(i);
        const auto& p = ldraw.parts[i];
        EXPECT_EQ(p.filename, QString::fromLatin1(expected[i].file));
        EXPECT_EQ(p.colorCode, expected[i].colour);
        EXPECT_NEAR(p.x, expected[i].pos[0], 1e-3);
        EXPECT_NEAR(p.y, expected[i].pos[1], 1e-3);
        EXPECT_NEAR(p.z, expected[i].pos[2], 1e-3);
        for (int k = 0; k < 9; ++k) EXPECT_NEAR(p.m[k], expected[i].m[k], 1e-6) << "m[" << k << "]";
    }
    // Both LDraw names ldraw.xml gives the 9V straight travel along.
    EXPECT_EQ(ldraw.parts[1].aliases, QStringList{ QStringLiteral("2865.dat") });
}

TEST(LDDAssets, FindsExtractedDatabaseAndMapping) {
    QTemporaryDir dir;
    writeFile(dir.filePath(QStringLiteral("ldraw.xml")), kMapping);
    writeFile(dir.filePath(QStringLiteral("Assets/db/Materials.xml")), "<Materials/>");
    writeFile(dir.filePath(QStringLiteral("Assets/db/Primitives/LOD0/3001.g")), "geometry");
    import::LDDAssets assets;
    ASSERT_TRUE(assets.open(dir.path()));
    EXPECT_EQ(assets.read(QStringLiteral("Primitives/LOD0/3001.g")), QByteArray("geometry"));
    EXPECT_TRUE(assets.read(QStringLiteral("Primitives/LOD0/none.g")).isEmpty());
    EXPECT_EQ(QFileInfo(assets.ldrawXmlPath()).fileName(), QStringLiteral("ldraw.xml"));
}

TEST(LDDAssets, EmptyFolderIsNotAnInstall) {
    QTemporaryDir dir;
    import::LDDAssets assets;
    EXPECT_FALSE(assets.open(dir.path()));
    EXPECT_FALSE(assets.open(QString()));
    EXPECT_TRUE(assets.ldrawXmlPath().isEmpty());
}

// Opt-in: point BLD_LDD_ROOT at a real LDD program folder (Assets.lif) or
// data folder (db.lif).
TEST(LDDAssets, RealInstallSmoke) {
    const QByteArray root = qgetenv("BLD_LDD_ROOT");
    if (root.isEmpty()) GTEST_SKIP() << "BLD_LDD_ROOT not set";
    import::LDDAssets assets;
    ASSERT_TRUE(assets.open(QString::fromLocal8Bit(root)));
    EXPECT_FALSE(assets.read(QStringLiteral("Materials.xml")).isEmpty());
    EXPECT_FALSE(assets.read(QStringLiteral("Primitives/LOD0/3001.g")).isEmpty());
}
