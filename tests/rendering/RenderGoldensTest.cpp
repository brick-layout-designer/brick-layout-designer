// Render-goldens harness. For each `.bbm` in the corpus, render at a
// fixed resolution with the offscreen Qt platform and compare against
// a reference PNG under `fixtures/render-goldens/`.
//
// This is a *regression gate*, not a parity gate: the references are
// whatever the previous good build produced (or, for the initial
// capture, PNGs rendered by vanilla BlueBrick on Windows). If a
// reference is missing we skip rather than fail, so the harness runs
// cleanly on a fresh clone before anyone's captured anything.
//
// Capturing / refreshing the goldens is a manual step — see
// scripts/capture-render-goldens.sh. The harness here is purely
// read + diff.

#include "rendering/SceneBuilder.h"
#include "saveload/BbmReader.h"
#include "parts/PartsLibrary.h"
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QString>

#include <cmath>

namespace {

// Size of every golden render. Fixed so the reference PNG and the
// build-time output always match dimensions before we even do the
// pixel diff.
constexpr int kGoldenWidth  = 1600;
constexpr int kGoldenHeight = 1200;

// Per-channel tolerance. Font rendering and GIF decoding aren't
// pixel-identical across platforms, and even Qt's antialiasing
// shifts slightly with version bumps. 8 (out of 255) ≈ 3% per
// channel — generous enough for font hinting drift, tight enough
// to flag real regressions (missing brick, wrong color, rotation
// off by a degree).
constexpr int kChannelTolerance = 8;

// Fraction of pixels that must exceed kChannelTolerance for the
// test to call the images different. 0.1 % catches a wholly-missing
// brick in a typical 1600x1200 = 1.92M-pixel frame (a single 48x16
// = 768 px brick = 0.04 % missing already reads as "real change").
constexpr double kMaxPixelDiffFraction = 0.001;

QString corpusDir()     { return QString::fromUtf8(BLD_BBM_CORPUS_DIR); }
QString partsLibRoot()  { return QString::fromUtf8(BLD_PARTS_LIBRARY_ROOT); }
QString goldensDir()    { return QString::fromUtf8(BLD_RENDER_GOLDENS_DIR); }

class QtFixture : public ::testing::Environment {
public:
    void SetUp() override {
        if (!app_) {
            static int argc = 1;
            static char name[] = "bld_rendering_tests";
            static char* argv[] = { name, nullptr };
            app_ = new QApplication(argc, argv);
        }
    }
    void TearDown() override {}
private:
    QApplication* app_ = nullptr;
};
::testing::Environment* const kEnv2 = ::testing::AddGlobalTestEnvironment(new QtFixture());

// Render one .bbm file to an in-memory QImage at the golden resolution.
QImage renderBbm(const QString& path, bld::parts::PartsLibrary& lib) {
    auto loaded = bld::saveload::readBbm(path);
    if (!loaded.ok()) return {};
    QGraphicsScene scene;
    bld::rendering::SceneBuilder builder(scene, lib);
    builder.build(*loaded.map);
    const QRectF bounds = scene.itemsBoundingRect();
    if (bounds.isEmpty()) return {};

    QImage img(kGoldenWidth, kGoldenHeight, QImage::Format_ARGB32);
    img.fill(loaded.map->backgroundColor.color);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    // Add a small margin around the content so bricks near the edge
    // aren't clipped by sub-pixel rounding.
    const QRectF padded = bounds.adjusted(-20, -20, 20, 20);
    scene.render(&p, QRectF(0, 0, kGoldenWidth, kGoldenHeight),
                  padded, Qt::KeepAspectRatio);
    p.end();
    return img;
}

struct DiffResult {
    qint64 pixelsDiffering = 0;
    qint64 totalPixels = 0;
    int    maxChannelDelta = 0;
};

// Per-pixel per-channel absolute-difference comparison. Returns how
// many pixels had any channel differing by more than `tolerance`.
DiffResult imageDiff(const QImage& a, const QImage& b, int tolerance) {
    DiffResult d;
    const int W = std::min(a.width(),  b.width());
    const int H = std::min(a.height(), b.height());
    d.totalPixels = static_cast<qint64>(W) * H;
    for (int y = 0; y < H; ++y) {
        const QRgb* la = reinterpret_cast<const QRgb*>(a.constScanLine(y));
        const QRgb* lb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
        for (int x = 0; x < W; ++x) {
            const QRgb pa = la[x], pb = lb[x];
            const int dr = std::abs(qRed(pa)   - qRed(pb));
            const int dg = std::abs(qGreen(pa) - qGreen(pb));
            const int db = std::abs(qBlue(pa)  - qBlue(pb));
            const int da = std::abs(qAlpha(pa) - qAlpha(pb));
            const int maxD = std::max({ dr, dg, db, da });
            if (maxD > d.maxChannelDelta) d.maxChannelDelta = maxD;
            if (maxD > tolerance) ++d.pixelsDiffering;
        }
    }
    return d;
}

// Every `.bbm` in the corpus becomes one TEST case via this helper.
// We also write the actual output to `${goldensDir()}/actual/` on
// failure so CI can upload it for inspection.
void checkGolden(const QString& bbmPath) {
    const QString stem = QFileInfo(bbmPath).completeBaseName();
    const QString golden = QDir(goldensDir()).filePath(stem + QStringLiteral(".png"));

    bld::parts::PartsLibrary lib;
    lib.addSearchPath(partsLibRoot());
    ASSERT_GT(lib.scan(), 100) << "parts library didn't load any parts";

    const QImage rendered = renderBbm(bbmPath, lib);
    ASSERT_FALSE(rendered.isNull()) << "renderBbm returned a null image";

    if (!QFile::exists(golden)) {
        // Initial capture hasn't happened yet for this fixture —
        // write what we just rendered to a suggestion file next to
        // the expected golden so the human running the test can
        // promote it manually (see scripts/capture-render-goldens.sh).
        const QString suggestion = QDir(goldensDir()).filePath(stem + QStringLiteral(".suggested.png"));
        QDir().mkpath(goldensDir());
        rendered.save(suggestion, "PNG");
        GTEST_SKIP() << "no golden at " << golden.toStdString()
                     << " — captured current render as "
                     << suggestion.toStdString();
    }

    QImage reference(golden);
    ASSERT_FALSE(reference.isNull()) << "reference " << golden.toStdString()
                                        << " failed to decode";
    ASSERT_EQ(reference.width(),  rendered.width())  << "dimension mismatch";
    ASSERT_EQ(reference.height(), rendered.height()) << "dimension mismatch";

    const DiffResult d = imageDiff(reference, rendered, kChannelTolerance);
    const double fraction = static_cast<double>(d.pixelsDiffering) / d.totalPixels;
    if (fraction > kMaxPixelDiffFraction) {
        // Dump the actual output so CI / reviewers can see the
        // regression without rebuilding locally.
        const QDir actualDir(QDir(goldensDir()).filePath(QStringLiteral("actual")));
        actualDir.mkpath(QStringLiteral("."));
        rendered.save(actualDir.filePath(stem + QStringLiteral(".png")), "PNG");
        FAIL() << "render drift for " << bbmPath.toStdString()
               << ": " << d.pixelsDiffering << "/" << d.totalPixels
               << " pixels differ beyond tolerance "
               << " (max channel delta " << d.maxChannelDelta << ")";
    }
}

}  // namespace

// Render goldens are an OPT-IN local regression gate. Even at 8/255
// channel tolerance + 0.1% pixel fraction they fail cross-platform
// (and across Qt minor versions within the same platform) because
// font hinting, image scaler, and GIF decode all differ. The only
// environment that can meaningfully enforce them is "the same box
// the references were captured on". Set BLD_ENABLE_RENDER_GOLDENS=1
// to actually run — CI doesn't, so cross-platform nightly builds
// don't break every time antialiasing shifts.
bool goldensEnabled() {
    return qEnvironmentVariableIsSet("BLD_ENABLE_RENDER_GOLDENS")
        && !qEnvironmentVariable("BLD_ENABLE_RENDER_GOLDENS").isEmpty()
        && qEnvironmentVariable("BLD_ENABLE_RENDER_GOLDENS") != QStringLiteral("0");
}

TEST(RenderGoldens, TightCorner) {
    if (!goldensEnabled()) GTEST_SKIP() << "set BLD_ENABLE_RENDER_GOLDENS=1 to run";
    const QString path = corpusDir() + QStringLiteral("/tight-corner.bbm");
    if (!QFile::exists(path)) GTEST_SKIP() << "tight-corner.bbm missing";
    if (!QDir(partsLibRoot()).exists()) GTEST_SKIP() << "BlueBrickParts submodule missing";
    checkGolden(path);
}

TEST(RenderGoldens, Fordyce2026) {
    if (!goldensEnabled()) GTEST_SKIP() << "set BLD_ENABLE_RENDER_GOLDENS=1 to run";
    const QString path = corpusDir() + QStringLiteral("/fordyce-2026.bbm");
    if (!QFile::exists(path)) GTEST_SKIP() << "fordyce-2026.bbm missing";
    if (!QDir(partsLibRoot()).exists()) GTEST_SKIP() << "BlueBrickParts submodule missing";
    checkGolden(path);
}
