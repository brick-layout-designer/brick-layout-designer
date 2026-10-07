// Tests for LDDReader. Exercises both `.lxfml` (raw XML) and `.lxf`
// (LXFML inside a ZIP) paths.

#include "import/ldd/LDDReader.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include <private/qzipwriter_p.h>

using namespace bld;

namespace {

QString writeLxfml(QTemporaryDir& dir, const QString& name, const QByteArray& body) {
    const QString path = dir.filePath(name);
    QFile f(path);
    [&]{ ASSERT_TRUE(f.open(QIODevice::WriteOnly)); }();
    f.write(body);
    return path;
}

QString writeLxf(QTemporaryDir& dir, const QString& name, const QByteArray& lxfmlBody) {
    const QString path = dir.filePath(name);
    {
        QZipWriter zw(path);
        zw.setCompressionPolicy(QZipWriter::AlwaysCompress);
        // LDD stores the XML document at the archive root with the bare
        // filename "LXFML" (no extension).
        zw.addFile(QStringLiteral("LXFML"), lxfmlBody);
    }
    return path;
}

// Minimal valid LXFML carrying one brick + one bone transformation.
QByteArray makeMinimalLxfml(const QString& designID = "3001",
                            const QString& material = "4",
                            // Identity rotation, translation 40,0,0.
                            const QString& transformation =
                                "1,0,0,0,1,0,0,0,1,40,0,0") {
    return QString(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<LXFML name="test model">
  <Bricks>
    <Brick designID="%1">
      <Part materials="%2">
        <Bone transformation="%3"/>
      </Part>
    </Brick>
  </Bricks>
</LXFML>
)xml").arg(designID, material, transformation).toUtf8();
}

}  // namespace

TEST(LDDReader, ParsesRawLxfml) {
    QTemporaryDir dir;
    const QString path = writeLxfml(dir, "model.lxfml", makeMinimalLxfml());
    auto r = import::readLDD(path);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(r.title, QStringLiteral("test model"));
    ASSERT_EQ(r.parts.size(), 1u);
    EXPECT_EQ(r.parts[0].colorCode, 4);
    EXPECT_EQ(r.parts[0].filename, QStringLiteral("3001.4.dat"));
}

TEST(LDDReader, ParsesLxfZip) {
    QTemporaryDir dir;
    const QString path = writeLxf(dir, "model.lxf", makeMinimalLxfml());
    auto r = import::readLDD(path);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(r.title, QStringLiteral("test model"));
    ASSERT_EQ(r.parts.size(), 1u);
}

TEST(LDDReader, MultipleBricks) {
    const QByteArray body = QByteArray(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<LXFML name="multi">
  <Bricks>
    <Brick designID="3001">
      <Part materials="4"><Bone transformation="1,0,0,0,1,0,0,0,1,0,0,0"/></Part>
    </Brick>
    <Brick designID="3002">
      <Part materials="7"><Bone transformation="1,0,0,0,1,0,0,0,1,32,0,0"/></Part>
    </Brick>
    <Brick designID="3003">
      <Part materials="1"><Bone transformation="1,0,0,0,1,0,0,0,1,0,0,32"/></Part>
    </Brick>
  </Bricks>
</LXFML>
)xml");
    QTemporaryDir dir;
    const QString path = writeLxfml(dir, "multi.lxfml", body);
    auto r = import::readLDD(path);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    ASSERT_EQ(r.parts.size(), 3u);
    EXPECT_EQ(r.parts[0].filename, QStringLiteral("3001.4.dat"));
    EXPECT_EQ(r.parts[1].filename, QStringLiteral("3002.7.dat"));
    EXPECT_EQ(r.parts[2].filename, QStringLiteral("3003.1.dat"));
}

TEST(LDDReader, BrickWithoutTransformIsSkipped) {
    // A <Brick> with no <Bone transformation="..."> shouldn't add a
    // phantom part at the origin — silently drop it and carry on.
    const QByteArray body = QByteArray(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<LXFML name="skip-me">
  <Bricks>
    <Brick designID="3001">
      <Part materials="4"/>
    </Brick>
  </Bricks>
</LXFML>
)xml");
    QTemporaryDir dir;
    const QString path = writeLxfml(dir, "no-bone.lxfml", body);
    auto r = import::readLDD(path);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(r.parts.size(), 0u);
}

TEST(LDDReader, EmptyMaterialFallsBackToDesignId) {
    const QByteArray body = QByteArray(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<LXFML name="no-mat">
  <Bricks>
    <Brick designID="3001">
      <Part><Bone transformation="1,0,0,0,1,0,0,0,1,0,0,0"/></Part>
    </Brick>
  </Bricks>
</LXFML>
)xml");
    QTemporaryDir dir;
    const QString path = writeLxfml(dir, "no-mat.lxfml", body);
    auto r = import::readLDD(path);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    ASSERT_EQ(r.parts.size(), 1u);
    EXPECT_EQ(r.parts[0].filename, QStringLiteral("3001.dat"));
}

TEST(LDDReader, RejectsWrongExtension) {
    QTemporaryDir dir;
    const QString path = writeLxfml(dir, "model.ldr", makeMinimalLxfml());
    auto r = import::readLDD(path);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.isEmpty());
}

TEST(LDDReader, MissingLxfmlEntryFails) {
    QTemporaryDir dir;
    const QString path = dir.filePath("no-lxfml.lxf");
    {
        QZipWriter zw(path);
        zw.addFile(QStringLiteral("other.txt"), QByteArray("not lxfml"));
    }
    auto r = import::readLDD(path);
    EXPECT_FALSE(r.ok);
}

TEST(LDDReader, AssembliesArePlacedPartByPart) {
    // A minifigure torso (76382) is an assembly: LDD has shapes for its
    // torso, arms and hands, not for 76382 itself.
    const QByteArray body = QByteArray(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<LXFML name="fig">
  <Bricks>
    <Brick designID="76382">
      <Part designID="3814" materials="102,0,0"><Bone transformation="1,0,0,0,1,0,0,0,1,1,2,3"/></Part>
      <Part designID="3818" materials="102"><Bone transformation="1,0,0,0,1,0,0,0,1,4,5,6"/></Part>
      <Part designID="3820" materials="24"><Bone transformation="1,0,0,0,1,0,0,0,1,7,8,9"/></Part>
    </Brick>
    <Brick designID="3001">
      <Part designID="3001" materials="21"><Bone transformation="1,0,0,0,1,0,0,0,1,0,0,0"/></Part>
    </Brick>
  </Bricks>
</LXFML>
)xml");
    QTemporaryDir dir;
    const QString path = writeLxfml(dir, "fig.lxfml", body);
    auto r = import::readLDD(path);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    ASSERT_EQ(r.parts.size(), 4u);
    EXPECT_EQ(r.parts[0].filename, QStringLiteral("3814.102.dat"));
    EXPECT_EQ(r.parts[1].filename, QStringLiteral("3818.102.dat"));
    EXPECT_EQ(r.parts[2].filename, QStringLiteral("3820.24.dat"));
    EXPECT_DOUBLE_EQ(r.parts[1].x, 4 * 25.0);
    EXPECT_DOUBLE_EQ(r.parts[2].z, 9 * 25.0);
    EXPECT_EQ(r.parts[3].filename, QStringLiteral("3001.21.dat"));
}
