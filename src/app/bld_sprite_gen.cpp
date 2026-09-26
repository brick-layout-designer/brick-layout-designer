// Headless sprite generator — reads an external 3D model file
// (.ldr / .dat / .mpd / .io / .lxf / .lxfml) and emits a top-down
// sprite at BlueBrick's 8 px/stud convention.
//
// Two render paths, same as the in-app import flow in MainWindow
// Tools → Import:
//
//   1. If the file references parts that resolve in BlueBrickParts,
//      composite them via rendering::SceneBuilder (the full fidelity
//      path — gets real GIFs, colours, orientations).
//
//   2. Otherwise (or alongside), rasterize the file's inline
//      LDraw primitives (type 2/3/4) with import::rasterizeTopDown
//      for a tinted silhouette.
//
// Writes the result as PNG (or GIF when the output extension is .gif).
//
// Intended for CI pipelines, batch conversions, and headless part-
// library enrichment — "all the same sprite the UI would show, but
// without launching the UI".

#include "core/Map.h"
#include "edit/Connectivity.h"
#include "import/ldd/LDDReader.h"
#include "import/ldraw/LDrawRasterize.h"
#include "import/ldraw/LDrawReader.h"
#include "import/studio/StudioReader.h"
#include "parts/PartsLibrary.h"
#include "rendering/SceneBuilder.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QString>
#include <QStringList>

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

void usage() {
    std::fprintf(stderr,
        "Usage: bld-sprite-gen <input> <output> [--parts <dir>] [--size <max-px>]\n"
        "\n"
        "  input    .ldr / .dat / .mpd / .io / .lxf / .lxfml\n"
        "  output   .png or .gif (extension selects the encoder)\n"
        "  --parts  override the parts library search path\n"
        "           (default: auto-locate BlueBrickParts submodule)\n"
        "  --size   cap the longer sprite edge at N pixels — does NOT\n"
        "           upscale, just trims fluff margin from large models\n"
        "           (default: no cap)\n");
}

enum class Kind { LDraw, Studio, LDD };

Kind kindFromExt(const QString& path) {
    const QString lower = path.toLower();
    if (lower.endsWith(QStringLiteral(".io")))    return Kind::Studio;
    if (lower.endsWith(QStringLiteral(".lxf")))   return Kind::LDD;
    if (lower.endsWith(QStringLiteral(".lxfml"))) return Kind::LDD;
    return Kind::LDraw;  // .ldr / .dat / .mpd / anything else
}

QString defaultPartsDir(const QString& argv0) {
    // Prefer the repo-layout sibling when run from inside a dev tree:
    //   build/src/app/bld_sprite_gen  →  ../../parts/BlueBrickParts/parts
    const QFileInfo argvInfo(argv0);
    const QDir bin = argvInfo.absoluteDir();
    const QString candidate = bin.absoluteFilePath(
        QStringLiteral("../../parts/BlueBrickParts/parts"));
    if (QDir(candidate).exists()) return QDir(candidate).absolutePath();
    return {};
}

}  // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    // Crude arg parse — two positionals + two named flags.
    QString inPath, outPath, partsOverride;
    int sizeCap = 0;
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args[i];
        if (a == QStringLiteral("--parts") && i + 1 < args.size()) {
            partsOverride = args[++i];
        } else if (a == QStringLiteral("--size") && i + 1 < args.size()) {
            sizeCap = args[++i].toInt();
        } else if (a == QStringLiteral("-h") || a == QStringLiteral("--help")) {
            usage();
            return 0;
        } else if (inPath.isEmpty()) {
            inPath = a;
        } else if (outPath.isEmpty()) {
            outPath = a;
        }
    }
    if (inPath.isEmpty() || outPath.isEmpty()) { usage(); return 1; }

    // Load parts library so library-resolvable refs get composited
    // at full fidelity. Missing library is fine — we fall back to
    // primitive raster only.
    bld::parts::PartsLibrary parts;
    const QString partsDir = partsOverride.isEmpty()
        ? defaultPartsDir(QString::fromLocal8Bit(argv[0]))
        : partsOverride;
    if (!partsDir.isEmpty() && QDir(partsDir).exists()) {
        parts.addSearchPath(partsDir);
        const int found = parts.scan();
        std::printf("parts library: %s (%d parts)\n",
                    partsDir.toUtf8().constData(), found);
    } else {
        std::printf("parts library: none — primitive raster only\n");
    }

    // Dispatch to the right reader.
    bld::import::LDrawReadResult read;
    switch (kindFromExt(inPath)) {
        case Kind::LDraw:  read = bld::import::readLDraw(inPath);    break;
        case Kind::Studio: read = bld::import::readStudioIo(inPath); break;
        case Kind::LDD:    read = bld::import::readLDD(inPath);      break;
    }
    if (!read.ok) {
        std::fprintf(stderr, "parse failed: %s\n",
                     read.error.toUtf8().constData());
        return 2;
    }
    std::printf("parsed: %zu part refs, %zu primitives, title=%s\n",
                read.parts.size(), read.primitives.size(),
                read.title.toUtf8().constData());

    // Primary render path: composite via SceneBuilder if there are
    // part references and any of them resolve in the library.
    auto modelMap = bld::import::toBlueBrickMap(read, &parts);
    const bool hasRefs = modelMap && !modelMap->layers().empty();
    QImage sprite;
    if (hasRefs) {
        bld::edit::rebuildConnectivity(*modelMap, parts);
        QGraphicsScene scene;
        scene.setBackgroundBrush(Qt::transparent);
        bld::rendering::SceneBuilder b(scene, parts);
        b.build(*modelMap);
        // Only composite when the scene actually got items —
        // toBlueBrickMap always produces a layer even when no refs
        // exist, and the empty .itemsBoundingRect() inflated by
        // our margin would otherwise produce an 8×8 placeholder we
        // then ship instead of the primitive raster.
        if (!scene.items().isEmpty()) {
            const QRectF bounds = scene.itemsBoundingRect().adjusted(-4, -4, 4, 4);
            const int wPx = std::max(8,
                static_cast<int>(std::ceil(bounds.width())));
            const int hPx = std::max(8,
                static_cast<int>(std::ceil(bounds.height())));
            sprite = QImage(wPx, hPx, QImage::Format_ARGB32);
            sprite.fill(Qt::transparent);
            QPainter p(&sprite);
            p.setRenderHint(QPainter::Antialiasing);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            scene.render(&p, QRectF(0, 0, wPx, hPx), bounds,
                          Qt::KeepAspectRatio);
        }
    }

    // Fallback path: primitive raster. Also triggers when the
    // composite was empty (unknown parts with no inline primitives).
    if (sprite.isNull() && !read.primitives.empty()) {
        sprite = bld::import::rasterizeTopDown(read);
        std::printf("composed via primitive raster (%d×%d)\n",
                    sprite.width(), sprite.height());
    } else if (!sprite.isNull()) {
        std::printf("composed via parts library (%d×%d)\n",
                    sprite.width(), sprite.height());
    }

    if (sprite.isNull()) {
        std::fprintf(stderr,
            "nothing to render — no library parts resolved and no "
            "inline primitives in %s\n",
            inPath.toUtf8().constData());
        return 3;
    }

    // Optional max-edge cap. Downscale-only (no upscaling — we never
    // manufacture pixels that didn't exist).
    if (sizeCap > 0) {
        const int longest = std::max(sprite.width(), sprite.height());
        if (longest > sizeCap) {
            sprite = sprite.scaled(sizeCap, sizeCap,
                                    Qt::KeepAspectRatio,
                                    Qt::SmoothTransformation);
            std::printf("downscaled to %d×%d\n",
                        sprite.width(), sprite.height());
        }
    }

    // Qt infers format from the extension. GIF writer drops alpha;
    // flatten against transparent-white if the user asked for GIF so
    // the output has a consistent solid background.
    const QString lower = outPath.toLower();
    QImage out = sprite;
    if (lower.endsWith(QStringLiteral(".gif"))) {
        QImage flat(sprite.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter p(&flat);
        p.drawImage(0, 0, sprite);
        out = flat;
    }
    if (!out.save(outPath)) {
        std::fprintf(stderr, "could not save %s\n",
                     outPath.toUtf8().constData());
        return 4;
    }
    std::printf("wrote %s\n", outPath.toUtf8().constData());
    return 0;
}
