#include "import/GifWriter.h"

#include <gtest/gtest.h>

#include <QImage>
#include <QImageReader>
#include <QRandomGenerator>
#include <QTemporaryDir>

using bld::import::writeGif;

namespace {

QImage readBack(const QString& path) {
    QImageReader r(path, "gif");
    QImage img = r.read();
    return img.convertToFormat(QImage::Format_ARGB32);
}

}  // namespace

TEST(GifWriter, RoundTripsTransparencyAndColoursExactly) {
    if (!QImageReader::supportedImageFormats().contains("gif")) GTEST_SKIP() << "no Qt GIF reader";
    QTemporaryDir dir;
    QImage src(40, 24, QImage::Format_ARGB32);
    src.fill(Qt::transparent);
    for (int y = 4; y < 20; ++y)
        for (int x = 4; x < 36; ++x)
            src.setPixel(x, y, x < 20 ? qRgb(200, 30, 30) : qRgb(20, 60, 220));
    const QString path = dir.filePath(QStringLiteral("a.gif"));
    QString err;
    ASSERT_TRUE(writeGif(src, path, &err)) << err.toStdString();

    const QImage back = readBack(path);
    ASSERT_EQ(back.size(), src.size());
    for (int y = 0; y < src.height(); ++y)
        for (int x = 0; x < src.width(); ++x)
            ASSERT_EQ(back.pixel(x, y), src.pixel(x, y)) << "at " << x << "," << y;
}

TEST(GifWriter, LargeImageSurvivesDictionaryResets) {
    if (!QImageReader::supportedImageFormats().contains("gif")) GTEST_SKIP() << "no Qt GIF reader";
    QTemporaryDir dir;
    // 200 distinct colours in noise: exact palette, and enough distinct
    // runs to fill the 4096-entry LZW table many times over.
    QImage src(300, 300, QImage::Format_ARGB32);
    QRandomGenerator rng(42);
    for (int y = 0; y < src.height(); ++y)
        for (int x = 0; x < src.width(); ++x) {
            const int c = static_cast<int>(rng.bounded(200));
            src.setPixel(x, y, qRgb(c, 255 - c, (c * 7) % 256));
        }
    const QString path = dir.filePath(QStringLiteral("noise.gif"));
    ASSERT_TRUE(writeGif(src, path));

    const QImage back = readBack(path);
    ASSERT_EQ(back.size(), src.size());
    for (int y = 0; y < src.height(); ++y)
        for (int x = 0; x < src.width(); ++x)
            ASSERT_EQ(back.pixel(x, y), src.pixel(x, y)) << "at " << x << "," << y;
}

TEST(GifWriter, QuantizesManyColoursClosely) {
    if (!QImageReader::supportedImageFormats().contains("gif")) GTEST_SKIP() << "no Qt GIF reader";
    QTemporaryDir dir;
    QImage src(128, 128, QImage::Format_ARGB32);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            src.setPixel(x, y, qRgb(x * 2, y * 2, (x + y) % 256));
    const QString path = dir.filePath(QStringLiteral("grad.gif"));
    ASSERT_TRUE(writeGif(src, path));

    const QImage back = readBack(path);
    ASSERT_EQ(back.size(), src.size());
    double totalErr = 0;
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) {
            const QRgb a = src.pixel(x, y), b = back.pixel(x, y);
            totalErr += std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b))
                      + std::abs(qBlue(a) - qBlue(b));
        }
    EXPECT_LT(totalErr / (128.0 * 128.0 * 3.0), 8.0);  // mean per-channel error
}
