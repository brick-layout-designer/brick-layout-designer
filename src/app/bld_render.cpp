#include "parts/PartsLibrary.h"
#include "rendering/SceneBuilder.h"
#include "saveload/BbmReader.h"
#include "saveload/SidecarIO.h"
#include "core/Map.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QString>

#include <cstdio>
#include <cstdlib>

// Headless `.bbm` → `.png` renderer. Useful for generating preview images,
// golden-image CI fixtures, and batch thumbnail generation. Requires the
// Qt offscreen platform: prepend QT_QPA_PLATFORM=offscreen when invoking.
int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    if (argc < 3) {
        std::fprintf(stderr,
            "Usage: bld-render <input.bbm> <output.png> [width] [parts-dir]\n"
            "  width defaults to 1600 px.\n");
        return 1;
    }
    const QString inputPath  = QString::fromLocal8Bit(argv[1]);
    const QString outputPath = QString::fromLocal8Bit(argv[2]);
    const int width = (argc >= 4) ? std::atoi(argv[3]) : 1600;

    QString partsRoot;
    if (argc >= 5) partsRoot = QString::fromLocal8Bit(argv[4]);
    else {
        const QString exe = QCoreApplication::applicationDirPath();
        for (const QString& rel : { QStringLiteral("/../../../parts/BlueBrickParts/parts"),
                                     QStringLiteral("/parts/BlueBrickParts/parts") }) {
            if (QDir(exe + rel).exists()) { partsRoot = exe + rel; break; }
        }
    }

    bld::parts::PartsLibrary lib;
    if (!partsRoot.isEmpty()) {
        lib.addSearchPath(partsRoot);
        lib.scan();
    }

    auto result = bld::saveload::readBbm(inputPath);
    if (!result.ok()) {
        std::fprintf(stderr, "Load failed: %s\n", result.error.toUtf8().constData());
        return 2;
    }

    // Its sidecar too (modules, labels, venue), when it sits next to the
    // file: "<file>.bld", or ".cld" as older builds named it.
    for (const QString& sidecar : { bld::saveload::sidecarPathFor(inputPath), inputPath + QStringLiteral(".cld") }) {
        if (!QFile::exists(sidecar)) continue;
        bld::saveload::readSidecar(sidecar, {}, result.map->sidecar);
        break;
    }

    QGraphicsScene scene;
    scene.setBackgroundBrush(result.map->backgroundColor.color);
    bld::rendering::SceneBuilder builder(scene, lib);
    builder.build(*result.map);

    const QRectF bounds = scene.itemsBoundingRect().adjusted(-20, -20, 20, 20);
    if (bounds.isEmpty()) {
        std::fprintf(stderr, "Scene is empty (no items); output would be blank.\n");
        return 3;
    }

    const double aspect = bounds.height() / bounds.width();
    const int height = std::max(100, static_cast<int>(width * aspect));

    QImage img(width, height, QImage::Format_ARGB32);
    img.fill(result.map->backgroundColor.color);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        scene.render(&p, QRectF(0, 0, width, height), bounds, Qt::KeepAspectRatio);
    }

    if (!img.save(outputPath, "PNG")) {
        std::fprintf(stderr, "Failed to write %s\n", outputPath.toUtf8().constData());
        return 4;
    }
    std::printf("Rendered %s -> %s (%dx%d)\n",
                inputPath.toUtf8().constData(),
                outputPath.toUtf8().constData(),
                width, height);
    return 0;
}
