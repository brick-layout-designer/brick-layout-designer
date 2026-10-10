#include "import/ldraw/LDrawPalette.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace bld;

namespace {

QString writeLDConfig(const QTemporaryDir& dir, const QByteArray& body) {
    const QString path = QDir(dir.path()).absoluteFilePath(QStringLiteral("LDConfig.ldr"));
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(body);
    return path;
}

}

TEST(LDrawPalette, ParsesBasicColorLines) {
    QTemporaryDir tmp;
    const QString cfg = writeLDConfig(tmp, QByteArray(
        "0 // LDraw config\n"
        "0 !COLOUR Black CODE 0 VALUE #1B2A34 EDGE #595959\n"
        "0 !COLOUR Blue CODE 1 VALUE #0055BF EDGE #333333\n"
        "0 !COLOUR Trans_Clear CODE 47 VALUE #FCFCFC EDGE #BBBBBB ALPHA 128\n"
    ));
    import::LDrawPalette p;
    ASSERT_TRUE(p.loadFromLDConfig(cfg));
    EXPECT_EQ(p.size(), 3);
    EXPECT_EQ(p.color(0), QColor::fromRgb(0x1B, 0x2A, 0x34));
    EXPECT_EQ(p.color(1), QColor::fromRgb(0x00, 0x55, 0xBF));

    const QColor trans = p.color(47);
    EXPECT_EQ(trans.alpha(), 128);
    EXPECT_TRUE(p.isTransparent(47));
}

TEST(LDrawPalette, IgnoresMalformedAndComments) {
    QTemporaryDir tmp;
    const QString cfg = writeLDConfig(tmp, QByteArray(
        "0 // not a color line\n"
        "0 !COLOUR BadHex CODE 99 VALUE not-hex EDGE #000000\n"
        "0 !COLOUR NoCode VALUE #ABCDEF\n"
        "0 !COLOUR Good CODE 7 VALUE #8A928D EDGE #4F4F4F\n"
    ));
    import::LDrawPalette p;
    ASSERT_TRUE(p.loadFromLDConfig(cfg));
    EXPECT_EQ(p.size(), 1);
    EXPECT_EQ(p.color(7), QColor::fromRgb(0x8A, 0x92, 0x8D));
}

TEST(LDrawPalette, FallsBackToBundledTableForUnknownCode) {
    import::LDrawPalette empty;
    EXPECT_TRUE(empty.isEmpty());
    // Code 14 (yellow) is in the bundled table.
    const QColor yellow = empty.color(14);
    EXPECT_TRUE(yellow.isValid());
    // The bundled palette returns ~RGB(245, 205, 47) for code 14.
    EXPECT_GT(yellow.red(), 200);
    EXPECT_GT(yellow.green(), 100);
}

TEST(LDrawPalette, OverridesBundledForLoadedCodes) {
    QTemporaryDir tmp;
    const QString cfg = writeLDConfig(tmp, QByteArray(
        "0 !COLOUR CustomBlack CODE 0 VALUE #112233 EDGE #000000\n"
    ));
    import::LDrawPalette p;
    ASSERT_TRUE(p.loadFromLDConfig(cfg));
    EXPECT_EQ(p.color(0), QColor::fromRgb(0x11, 0x22, 0x33));
}
