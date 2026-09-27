// Headless map conversion and comparison, as the app does it:
//
//   bld-convert [-p <parts dir>] <input> <output>
//       Read .bbm / .ldr / .mpd / .tdl / .ncp and write any of them (by
//       extension). Like opening the file in the app, bricks are relinked
//       and stale sizes fixed before writing.
//
//   bld-convert [-p <parts dir>] --compare <a> <b> [--no-links] [--known-only]
//       Compare two maps brick by brick (part, position, size and angle
//       within 0.02 studs / 0.05 degrees, and how many of each brick's
//       connections are linked). --known-only skips parts the library
//       lacks (BlueBrick redraws their placeholders at another size).
//       Exit status 1 and a list when they differ.
//
// Used by scripts/bluebrick-oracle/compat.sh to check files against
// vanilla BlueBrick.

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "edit/Connectivity.h"
#include "import/mapformats/FourDBrixMap.h"
#include "import/mapformats/LDrawMap.h"
#include "import/mapformats/TrackDesignerMap.h"
#include "parts/BrickPlacement.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"
#include "saveload/BbmWriter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

using namespace bld;

namespace {

QString suffixOf(const QString& path) { return QFileInfo(path).suffix().toLower(); }

std::unique_ptr<core::Map> readMap(const QString& path, parts::PartsLibrary& lib, QString* error) {
    const QString s = suffixOf(path);
    if (s == QLatin1String("bbm")) {
        auto r = saveload::readBbm(path);
        if (!r.ok()) { *error = r.error; return nullptr; }
        return std::move(r.map);
    }
    import::MapReadResult r;
    if (s == QLatin1String("ldr") || s == QLatin1String("mpd")) r = import::readLDrawMap(path, lib);
    else if (s == QLatin1String("tdl")) r = import::readTrackDesignerMap(path, lib);
    else if (s == QLatin1String("ncp")) r = import::readFourDBrixMap(path, lib);
    else { *error = QStringLiteral("unknown map format: %1").arg(path); return nullptr; }
    if (!r.ok()) *error = r.error;
    return std::move(r.map);
}

bool writeMap(const core::Map& map, const QString& path, parts::PartsLibrary& lib, QString* error) {
    const QString s = suffixOf(path);
    if (s == QLatin1String("bbm")) {
        const auto r = saveload::writeBbm(map, path);
        if (!r.ok) *error = r.error;
        return r.ok;
    }
    if (s == QLatin1String("ldr") || s == QLatin1String("mpd")) return import::writeLDrawMap(map, path, lib, error);
    if (s == QLatin1String("tdl")) return import::writeTrackDesignerMap(map, path, lib, error);
    if (s == QLatin1String("ncp")) return import::writeFourDBrixMap(map, path, lib, error);
    *error = QStringLiteral("unknown map format: %1").arg(path);
    return false;
}

struct Key {
    QString part;
    double x, y, w, h, angle;
    int links;
    QString text() const {
        return QStringLiteral("%1 at (%2, %3) %4x%5 @%6, %7 linked")
            .arg(part).arg(x, 0, 'f', 2).arg(y, 0, 'f', 2).arg(w, 0, 'f', 2).arg(h, 0, 'f', 2)
            .arg(angle, 0, 'f', 2).arg(links);
    }
};

std::vector<Key> keysOf(const core::Map& map, const parts::PartsLibrary* knownOnly) {
    std::vector<Key> out;
    for (const auto& layer : map.layers()) {
        if (layer->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*layer).bricks) {
            if (knownOnly && !knownOnly->metadata(b.partNumber)) continue;
            int links = 0;
            for (const auto& c : b.connections) links += !c.linkedToId.isEmpty();
            const QPointF c = b.displayArea.center();
            out.push_back({ b.partNumber.toUpper(), c.x(), c.y(), b.displayArea.width(), b.displayArea.height(),
                            std::remainder(static_cast<double>(b.orientation), 360.0), links });
        }
    }
    return out;
}

int compare(const core::Map& a, const core::Map& b, bool links, const parts::PartsLibrary* knownOnly) {
    auto ka = keysOf(a, knownOnly), kb = keysOf(b, knownOnly);
    const auto same = [&](const Key& p, const Key& q) {
        return p.part == q.part && std::abs(p.x - q.x) < 0.02 && std::abs(p.y - q.y) < 0.02
            && std::abs(p.w - q.w) < 0.02 && std::abs(p.h - q.h) < 0.02
            && std::abs(std::remainder(p.angle - q.angle, 360.0)) < 0.05 && (!links || p.links == q.links);
    };
    const size_t total = ka.size();
    for (auto it = ka.begin(); it != ka.end();) {
        const auto m = std::find_if(kb.begin(), kb.end(), [&](const Key& k) { return same(*it, k); });
        if (m != kb.end()) { kb.erase(m); it = ka.erase(it); } else { ++it; }
    }
    if (ka.empty() && kb.empty()) {
        std::printf("same: %zu bricks\n", total);
        return 0;
    }
    std::printf("DIFFERENT: %zu brick(s) only in the first, %zu only in the second\n", ka.size(), kb.size());
    for (size_t i = 0; i < ka.size() && i < 10; ++i) std::printf("  first only:  %s\n", qPrintable(ka[i].text()));
    for (size_t i = 0; i < kb.size() && i < 10; ++i) std::printf("  second only: %s\n", qPrintable(kb[i].text()));
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);  // part sprites give the footprints

    QStringList args = app.arguments().mid(1);
    QString partsDir;
    const int p = args.indexOf(QStringLiteral("-p"));
    if (p >= 0 && p + 1 < args.size()) {
        partsDir = args[p + 1];
        args.remove(p, 2);
    } else {
        const QString exe = QCoreApplication::applicationDirPath();
        for (const QString& rel : { QStringLiteral("/../../../parts/BlueBrickParts/parts"),
                                     QStringLiteral("/../share/brick-layout-designer/parts/BlueBrickParts/parts"),
                                     QStringLiteral("/parts/BlueBrickParts/parts") })
            if (QDir(exe + rel).exists()) { partsDir = exe + rel; break; }
    }
    const bool compareMode = args.removeAll(QStringLiteral("--compare")) > 0;
    const bool links = args.removeAll(QStringLiteral("--no-links")) == 0;
    const bool knownOnly = args.removeAll(QStringLiteral("--known-only")) > 0;
    if (args.size() != 2) {
        std::fprintf(stderr, "Usage: bld-convert [-p <parts dir>] <input> <output>\n"
                             "       bld-convert [-p <parts dir>] --compare <a> <b> [--no-links] [--known-only]\n");
        return 2;
    }
    parts::PartsLibrary lib;
    if (!partsDir.isEmpty()) {
        lib.addSearchPath(partsDir);
        lib.scan();
    }

    QString error;
    auto first = readMap(args[0], lib, &error);
    if (!first) { std::fprintf(stderr, "%s: %s\n", qPrintable(args[0]), qPrintable(error)); return 2; }
    if (compareMode) {
        auto second = readMap(args[1], lib, &error);
        if (!second) { std::fprintf(stderr, "%s: %s\n", qPrintable(args[1]), qPrintable(error)); return 2; }
        return compare(*first, *second, links, knownOnly ? &lib : nullptr);
    }
    // As the app does on opening a map.
    parts::placement::fixStaleAreas(*first, lib);
    edit::rebuildConnectivity(*first, lib);
    if (!writeMap(*first, args[1], lib, &error)) {
        std::fprintf(stderr, "%s: %s\n", qPrintable(args[1]), qPrintable(error));
        return 2;
    }
    return 0;
}
