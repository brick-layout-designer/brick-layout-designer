#include "core/Map.h"
#include "edit/ConnectionCheck.h"
#include "import/LayoutFile.h"
#include "import/PartsZip.h"
#include "parts/PartsLibrary.h"
#include "rendering/SceneBuilder.h"
#include "saveload/BbmReader.h"
#include "saveload/SidecarIO.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QString>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

QString bundledPartsRoot() {
    const QString exe = QCoreApplication::applicationDirPath();
    for (const QString& rel : { QStringLiteral("/../../../parts/BlueBrickParts/parts"),
                                QStringLiteral("/parts/BlueBrickParts/parts") }) {
        if (QDir(exe + rel).exists()) return exe + rel;
    }
    return {};
}

// --check-connections <parts zip | set.xml | set key | layout> [parts folder...]:
// which connection points meet another and which are left open
// (edit/ConnectionCheck.h). A parts zip (a Brick Track Designer export) is
// unpacked and each set in it checked; a .set.xml brings its own folder; a
// layout or a bare key uses the parts folders given, else the bundled library.
int checkConnections(const QStringList& args) {
    if (args.isEmpty()) {
        std::fprintf(stderr,
                     "Usage: bld-render --check-connections <parts.zip | set.xml | set key | layout.bbm | "
                     "layout.bld-layout> [parts-dir...]\n");
        return 1;
    }
    const QString& input = args.first();
    QStringList dirs = args.mid(1);
    const QFileInfo info(input);
    if (info.isFile() && input.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive)) {
        QTemporaryDir unpacked;
        const auto added = bld::import::addPartsFromZip(input, unpacked.path());
        if (!added.ok) {
            std::fprintf(stderr, "%s\n", added.error.toUtf8().constData());
            return 2;
        }
        bld::parts::PartsLibrary lib;
        for (const QString& folder : added.folders) lib.addSearchPath(unpacked.filePath(folder));
        for (const QString& d : std::as_const(dirs)) lib.addSearchPath(d);
        lib.scan();
        int sets = 0;
        for (const QString& key : added.partKeys) {
            const auto meta = lib.metadata(key);
            if (!meta || meta->kind != bld::parts::PartKind::Group) continue;
            std::fputs(bld::edit::connectionReportText(key, bld::edit::checkSetConnections(key, lib))
                           .toUtf8()
                           .constData(),
                       stdout);
            ++sets;
        }
        if (sets == 0) {
            std::fprintf(stderr, "There is no set in %s to check.\n", input.toUtf8().constData());
            return 2;
        }
        return 0;
    }
    const bool setFile = info.isFile() && input.endsWith(QLatin1String(".xml"), Qt::CaseInsensitive);
    if (setFile) dirs.prepend(info.absolutePath());
    if (dirs.isEmpty() && !bundledPartsRoot().isEmpty()) dirs << bundledPartsRoot();
    bld::parts::PartsLibrary lib;
    for (const QString& d : std::as_const(dirs)) lib.addSearchPath(d);
    lib.scan();

    bld::edit::ConnectionReport report;
    QString title;
    if (setFile || !info.exists()) {
        QString key = setFile ? info.fileName() : input;
        if (key.endsWith(QLatin1String(".set.xml"), Qt::CaseInsensitive)) key.chop(8);
        else if (key.endsWith(QLatin1String(".xml"), Qt::CaseInsensitive)) key.chop(4);
        const auto meta = lib.metadata(key);
        if (!meta || meta->kind != bld::parts::PartKind::Group) {
            std::fprintf(stderr, "%s is not a set in the parts folders given.\n", key.toUtf8().constData());
            return 2;
        }
        report = bld::edit::checkSetConnections(key, lib);
        title = key;
    } else {
        std::unique_ptr<bld::core::Map> map;
        QTemporaryDir assets;
        if (input.endsWith(QLatin1String(".bld-layout"), Qt::CaseInsensitive)) {
            auto read = bld::import::readLayoutFile(input, assets.path());
            if (!read.map) {
                std::fprintf(stderr, "Load failed: %s\n", read.error.toUtf8().constData());
                return 2;
            }
            map = std::move(read.map);
        } else {
            auto read = bld::saveload::readBbm(input);
            if (!read.ok()) {
                std::fprintf(stderr, "Load failed: %s\n", read.error.toUtf8().constData());
                return 2;
            }
            map = std::move(read.map);
        }
        report = bld::edit::checkMapConnections(*map, lib);
        title = info.fileName();
    }
    std::fputs(bld::edit::connectionReportText(title, report).toUtf8().constData(), stdout);
    return 0;
}

} // namespace

// Headless `.bbm` → `.png` renderer. Useful for generating preview images,
// golden-image CI fixtures, and batch thumbnail generation. Requires the
// Qt offscreen platform: prepend QT_QPA_PLATFORM=offscreen when invoking.
// With --check-connections it reports a set's or layout's open connection
// points instead (see checkConnections above).
int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    if (argc >= 2 && qstrcmp(argv[1], "--check-connections") == 0) {
        QStringList args;
        for (int i = 2; i < argc; ++i) args << QString::fromLocal8Bit(argv[i]);
        return checkConnections(args);
    }

    if (argc < 3) {
        std::fprintf(stderr,
                     "Usage: bld-render <input.bbm> <output.png> [width] [parts-dir]\n"
                     "  width defaults to 1600 px.\n"
                     "       bld-render --check-connections <set.xml | set key | layout> [parts-dir...]\n"
                     "  lists the connection points that meet no other.\n");
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
