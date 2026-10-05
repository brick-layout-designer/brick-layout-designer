#include "import/ldraw/LDrawReader.h"
#include "core/Brick.h"
#include "core/LayerBrick.h"
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <cmath>

using namespace bld;

namespace {

QString writeTempLDraw(QTemporaryDir& dir, const QString& body, const QString& name = "t.ldr") {
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return {};
    QTextStream out(&f);
    out << body;
    return path;
}

}

TEST(LDrawReader, ParsesType1References) {
    QTemporaryDir dir;
    const QString path = writeTempLDraw(dir,
        "0 Simple test model\n"
        "1 16 0 0 0 1 0 0 0 1 0 0 0 1 3001.dat\n"
        "1 4 40 0 0 1 0 0 0 1 0 0 0 1 3002.dat\n"
        "2 24 0 0 0 20 0 0   # a line primitive; should be ignored\n");

    auto r = import::readLDraw(path);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(r.title, QStringLiteral("Simple test model"));
    ASSERT_EQ(r.parts.size(), 2u);
    EXPECT_EQ(r.parts[0].colorCode, 16);
    EXPECT_EQ(r.parts[0].filename,  QStringLiteral("3001.dat"));
    EXPECT_DOUBLE_EQ(r.parts[1].x, 40.0);
    EXPECT_EQ(r.parts[1].filename,  QStringLiteral("3002.dat"));
}

TEST(LDrawReader, YRotationExtraction) {
    QTemporaryDir dir;
    // 90° rotation around Y: [0 0 1; 0 1 0; -1 0 0]
    const QString path = writeTempLDraw(dir,
        "0 rotated\n"
        "1 1 0 0 0   0 0 1   0 1 0   -1 0 0   3001.dat\n");
    auto r = import::readLDraw(path);
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.parts.size(), 1u);

    auto map = import::toBlueBrickMap(r);
    const auto* L = static_cast<const core::LayerBrick*>(map->layers()[0].get());
    ASSERT_EQ(L->bricks.size(), 1u);
    // atan2(sin90, cos90) ≈ 90° (±ε).
    EXPECT_NEAR(std::fmod(L->bricks[0].orientation + 360.0f, 360.0f), 90.0f, 0.5f);
}

TEST(LDrawReader, PartNumberAndColorEncoding) {
    QTemporaryDir dir;
    const QString path = writeTempLDraw(dir,
        "0 color test\n"
        "1 1 0 0 0 1 0 0 0 1 0 0 0 1 3001.dat\n"
        "1 14 0 0 0 1 0 0 0 1 0 0 0 1 s/3002s01.dat\n");
    auto r = import::readLDraw(path);
    ASSERT_TRUE(r.ok);
    auto map = import::toBlueBrickMap(r);
    const auto* L = static_cast<const core::LayerBrick*>(map->layers()[0].get());
    ASSERT_EQ(L->bricks.size(), 2u);
    EXPECT_EQ(L->bricks[0].partNumber, QStringLiteral("3001.1"));
    // Subfolder prefix and .dat stripped, upper-cased, paired with color 14.
    EXPECT_EQ(L->bricks[1].partNumber, QStringLiteral("3002S01.14"));
}

TEST(LDrawReader, PositionLduToStuds) {
    QTemporaryDir dir;
    const QString path = writeTempLDraw(dir,
        "0 pos\n"
        "1 16 400 0 200 1 0 0 0 1 0 0 0 1 3001.dat\n"); // 20 LDU/stud => (20,-10) studs
    auto r = import::readLDraw(path);
    ASSERT_TRUE(r.ok);
    auto map = import::toBlueBrickMap(r);
    const auto* L = static_cast<const core::LayerBrick*>(map->layers()[0].get());
    const QPointF center = L->bricks[0].displayArea.center();
    // Top-down view of a -Y-up model: y = -z, as BlueBrick's LDraw loader.
    EXPECT_NEAR(center.x(), 20.0, 0.01);
    EXPECT_NEAR(center.y(), -10.0, 0.01);
}

TEST(LDrawReader, EmptyFileReturnsOkWithNoParts) {
    QTemporaryDir dir;
    const QString path = writeTempLDraw(dir, "");
    auto r = import::readLDraw(path);
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.parts.empty());
}
