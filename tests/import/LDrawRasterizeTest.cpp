// Tests for the LDraw primitive rasterizer — the fallback path that
// turns inline LDraw geometry (types 2/3/4) into a top-down sprite
// for files whose subpart references don't resolve in our library.

#include "import/ldraw/LDrawRasterize.h"
#include "import/ldraw/LDrawReader.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QTextStream>

using namespace bld;

namespace {

// The rasterizer uses QPainter, which requires a QGuiApplication
// (provided by QApplication). We share one across all tests in the
// executable via a Google Test environment.
class QtFixture : public ::testing::Environment {
public:
    void SetUp() override {
        if (!app_) {
            static int argc = 1;
            static char name[] = "bld_import_tests";
            static char* argv[] = { name, nullptr };
            qputenv("QT_QPA_PLATFORM", "offscreen");
            app_ = new QGuiApplication(argc, argv);
        }
    }
    void TearDown() override {}
private:
    QGuiApplication* app_ = nullptr;
};
::testing::Environment* const kRasterEnv =
    ::testing::AddGlobalTestEnvironment(new QtFixture());

QString writeLdr(QTemporaryDir& dir, const QString& body,
                 const QString& name = "t.ldr") {
    const QString path = dir.filePath(name);
    QFile f(path);
    [&]{ ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Text)); }();
    QTextStream out(&f);
    out << body;
    return path;
}

// True if any pixel in `img` has a non-zero alpha — cheap way to
// assert "the rasterizer actually drew something" without exposing
// pixel coordinates to the test.
bool hasAnyDrawnPixel(const QImage& img) {
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(line[x]) > 0) return true;
        }
    }
    return false;
}

}  // namespace

TEST(LDrawReader, ParsesTypeTwoLinePrimitive) {
    QTemporaryDir dir;
    const QString path = writeLdr(dir,
        "0 line test\n"
        "2 24 0 0 0  20 0 20\n");
    const auto r = import::readLDraw(path);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    ASSERT_EQ(r.primitives.size(), 1u);
    EXPECT_EQ(r.primitives[0].kind, 2);
    EXPECT_EQ(r.primitives[0].colorCode, 24);
    EXPECT_DOUBLE_EQ(r.primitives[0].v[0][0], 0);
    EXPECT_DOUBLE_EQ(r.primitives[0].v[1][0], 20);
}

TEST(LDrawReader, ParsesTypeThreeAndFourPrimitives) {
    QTemporaryDir dir;
    const QString path = writeLdr(dir,
        "0 shapes\n"
        "3 4 0 0 0  20 0 0  20 0 20\n"
        "4 14 0 0 0  20 0 0  20 0 20  0 0 20\n"
        "5 24 0 0 0  20 0 20  0 0 10  20 0 10\n");  // type-5 ignored
    const auto r = import::readLDraw(path);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    ASSERT_EQ(r.primitives.size(), 2u);
    EXPECT_EQ(r.primitives[0].kind, 3);
    EXPECT_EQ(r.primitives[0].colorCode, 4);
    EXPECT_EQ(r.primitives[1].kind, 4);
    EXPECT_EQ(r.primitives[1].colorCode, 14);
}

TEST(LDrawRasterize, EmptyInputReturnsNullImage) {
    import::LDrawReadResult empty;
    empty.ok = true;
    const QImage img = import::rasterizeTopDown(empty);
    EXPECT_TRUE(img.isNull());
}

TEST(LDrawRasterize, FilledTrianglePaintsPixelsAtItsColour) {
    // 20 LDU = 1 stud; this triangle spans 8 studs × 8 studs on
    // the XZ plane (160 LDU / 20). At 8 px/stud + 4 px margin on
    // each side the sprite is 8*8 + 8 = 72 px.
    QTemporaryDir dir;
    const QString path = writeLdr(dir,
        "0 red triangle\n"
        "3 4 0 0 0  160 0 0  0 0 160\n");
    const auto r = import::readLDraw(path);
    ASSERT_TRUE(r.ok);
    const QImage img = import::rasterizeTopDown(r);
    ASSERT_FALSE(img.isNull());
    EXPECT_GE(img.width(),  64);
    EXPECT_GE(img.height(), 64);
    EXPECT_TRUE(hasAnyDrawnPixel(img));
}

TEST(LDrawRasterize, LineOnlyInputProducesStrokedPixels) {
    QTemporaryDir dir;
    const QString path = writeLdr(dir,
        "0 black line\n"
        "2 0 0 0 0  160 0 160\n");
    const auto r = import::readLDraw(path);
    ASSERT_TRUE(r.ok);
    const QImage img = import::rasterizeTopDown(r);
    ASSERT_FALSE(img.isNull());
    EXPECT_TRUE(hasAnyDrawnPixel(img));
}

TEST(LDrawRasterize, MixOfTrisAndLinesProducesComplexSprite) {
    // A small composite: red triangle fill + black outline line,
    // covers both paint-fill and edge-stroke code paths in one
    // rasterize call.
    QTemporaryDir dir;
    const QString path = writeLdr(dir,
        "0 mix\n"
        "3 4 0 0 0  80 0 0  80 0 80\n"
        "2 0 0 0 0  80 0 80\n");
    const auto r = import::readLDraw(path);
    ASSERT_TRUE(r.ok);
    const QImage img = import::rasterizeTopDown(r);
    ASSERT_FALSE(img.isNull());
    // Find both a red-ish pixel (triangle body) and a dark pixel
    // (edge line) to confirm BOTH primitive kinds painted.
    bool sawRed = false, sawDark = false;
    for (int y = 0; y < img.height() && (!sawRed || !sawDark); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(line[x]) == 0) continue;
            const int red = qRed(line[x]);
            const int green = qGreen(line[x]);
            const int blue = qBlue(line[x]);
            if (red > 180 && green < 80 && blue < 80) sawRed = true;
            if (red < 60  && green < 60 && blue < 60) sawDark = true;
        }
    }
    EXPECT_TRUE(sawRed)  << "no red-triangle pixels found";
    EXPECT_TRUE(sawDark) << "no dark-line pixels found";
}
