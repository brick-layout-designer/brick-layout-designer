// Opt-in real-model check for the outlines drawn over imported parts.
//
//   BLD_IMPORT_MODEL=/path/to/model.io   (or .lxf / .lxfml / .ldr / .mpd)
//   BLD_LDRAW_LIBRARY=/path/to/ldraw      (for .io / .ldr; Studio's ldraw folder works)
//   BLD_LDD_ROOT=/path/to/LDD             (for .lxf; the folder with db.lif or Assets.lif)
//   BLD_IMPORT_RENDER_OUT=/some/dir       (optional: writes fill.png and outlined.png there)
//
// The model files are proprietary: they are never committed, so CI skips this.
#include "geom/Mesh.h"
#include "import/ldd/LDDAssets.h"
#include "import/ldd/LDDMaterials.h"
#include "import/ldd/LDDMeshBuilder.h"
#include "import/ldd/LDDReader.h"
#include "import/ldraw/LDrawLibrary.h"
#include "import/ldraw/LDrawMeshBuilder.h"
#include "import/ldraw/LDrawMeshLoader.h"
#include "import/ldraw/LDrawPalette.h"
#include "import/ldraw/LDrawReader.h"
#include "import/mesh/MeshRasterize.h"
#include "import/studio/StudioReader.h"

#include <gtest/gtest.h>

#include <QColor>
#include <QDir>
#include <QImage>
#include <QString>

#include <cstdio>

using namespace bld;

namespace {

QString env(const char* name) {
    return QString::fromLocal8Bit(qgetenv(name));
}

geom::Mesh loadModel(const QString& path, QString& why) {
    const QString lower = path.toLower();
    if (lower.endsWith(QStringLiteral(".lxf")) || lower.endsWith(QStringLiteral(".lxfml"))) {
        const QString root = env("BLD_LDD_ROOT");
        if (root.isEmpty()) {
            why = QStringLiteral("BLD_LDD_ROOT not set");
            return {};
        }
        import::LDDAssets assets;
        if (!assets.open(root)) {
            why = QStringLiteral("BLD_LDD_ROOT has no brick database");
            return {};
        }
        import::LDDMaterials materials;
        materials.loadFromBytes(assets.read(QStringLiteral("Materials.xml")));
        import::LDDMeshBuilder builder;
        builder.setAssets(&assets);
        builder.setMaterials(&materials);
        return builder.bake(import::readLDD(path)).mesh;
    }
    const QString libPath = env("BLD_LDRAW_LIBRARY");
    if (libPath.isEmpty()) {
        why = QStringLiteral("BLD_LDRAW_LIBRARY not set");
        return {};
    }
    const import::LDrawReadResult read =
        lower.endsWith(QStringLiteral(".io")) ? import::readStudioIo(path) : import::readLDraw(path);
    import::LDrawLibrary lib(libPath);
    lib.setOverlayDirs(read.extraPartDirs);
    import::LDrawPalette palette;
    palette.loadFromLDConfig(QDir(libPath).absoluteFilePath(QStringLiteral("LDConfig.ldr")));
    import::LDrawMeshLoader loader(lib, palette);
    return import::bakeMeshFromLDraw(read, loader, palette).mesh;
}

int luma(QRgb c) {
    return (qRed(c) * 299 + qGreen(c) * 587 + qBlue(c) * 114) / 1000;
}

} // namespace

// Aaron, on a big Studio import: "there are a lot of noisy lines". The
// outlines may darken the brick tops and stud rims, which is a small share
// of the sprite; long lines through whole bricks pushed it far higher.
TEST(ImportEdgeNoise, RealModelOutlinesStayOnTheVisibleTop) {
    const QString model = env("BLD_IMPORT_MODEL");
    if (model.isEmpty()) GTEST_SKIP() << "BLD_IMPORT_MODEL not set";
    QString why;
    const geom::Mesh mesh = loadModel(model, why);
    if (!why.isEmpty()) GTEST_SKIP() << why.toStdString();
    ASSERT_FALSE(mesh.tris.empty()) << "no geometry in " << model.toStdString();

    // As the importer renders it (ImportPipeline's geometryRasterOptions).
    import::RasterizeOptions opt;
    opt.pxPerStud = 32;
    opt.marginPx = 0;
    opt.ssaa = 4;
    const QImage fill = import::rasterizeMeshTopDown(mesh, opt).image.convertToFormat(QImage::Format_ARGB32);
    opt.wireframe = true;
    const QImage outlined =
        import::rasterizeMeshTopDown(mesh, opt).image.convertToFormat(QImage::Format_ARGB32);
    ASSERT_EQ(fill.size(), outlined.size());

    qint64 solid = 0, darkened = 0;
    for (int y = 0; y < fill.height(); ++y) {
        const auto* a = reinterpret_cast<const QRgb*>(fill.constScanLine(y));
        const auto* b = reinterpret_cast<const QRgb*>(outlined.constScanLine(y));
        for (int x = 0; x < fill.width(); ++x) {
            if (qAlpha(a[x]) < 128) continue;
            ++solid;
            if (luma(a[x]) - luma(b[x]) > 40) ++darkened;
        }
    }
    ASSERT_GT(solid, 0);
    const double share = double(darkened) / double(solid);
    std::printf(
        "[ImportEdgeNoise] %s: %d x %d px, %lld tris, %lld edges, outlines darken %.2f%% of the sprite\n",
        qPrintable(QDir(model).dirName()), fill.width(), fill.height(),
        static_cast<long long>(mesh.tris.size()), static_cast<long long>(mesh.edges.size()), share * 100.0);

    const QString out = env("BLD_IMPORT_RENDER_OUT");
    if (!out.isEmpty()) {
        QDir().mkpath(out);
        fill.save(QDir(out).filePath(QStringLiteral("fill.png")));
        outlined.save(QDir(out).filePath(QStringLiteral("outlined.png")));
    }
    const QString maxShare = env("BLD_IMPORT_MAX_EDGE_SHARE");
    EXPECT_LT(share, maxShare.isEmpty() ? 0.12 : maxShare.toDouble());
}
