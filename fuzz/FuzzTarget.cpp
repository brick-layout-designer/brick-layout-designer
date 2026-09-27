// libFuzzer harnesses for every file parser (docs in fuzz/README.md).
// One source, built once per target with BLD_FUZZ_TARGET set to the
// target's name; each feeds the input to that parser through a file of
// the right extension (most readers take a path).

#include "core/Map.h"
#include "core/Sidecar.h"
#include "edit/Budget.h"
#include "import/ldd/LDDReader.h"
#include "import/ldraw/LDrawReader.h"
#include "import/mapformats/FourDBrixMap.h"
#include "import/mapformats/LDrawMap.h"
#include "import/mapformats/TrackDesignerMap.h"
#include "import/studio/StudioReader.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"
#include "saveload/SidecarIO.h"
#include "saveload/VenueIO.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QStandardPaths>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

using namespace bld;

namespace {

std::unique_ptr<parts::PartsLibrary> library;
QString scratch;

QString writeInput(const uint8_t* data, size_t size, const char* extension) {
    const QString path = scratch + QStringLiteral("/input.") + QLatin1String(extension);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(reinterpret_cast<const char*>(data), static_cast<qint64>(size));
    return path;
}

bool is(const char* name) { return std::strcmp(BLD_FUZZ_TARGET, name) == 0; }

}  // namespace

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // QPixmap needs it. Never destroyed: libFuzzer exits through exit(),
    // after Qt's own statics are gone.
    new QGuiApplication(*argc, *argv);
    scratch = QDir::temp().filePath(QStringLiteral("bld-fuzz-%1-%2")).arg(QLatin1String(BLD_FUZZ_TARGET)).arg(QCoreApplication::applicationPid());
    QDir().mkpath(scratch);
    library = std::make_unique<parts::PartsLibrary>();
    // The map formats place parts through the real library.
    if (is("ldraw_map") || is("tdl") || is("ncp")) {
        library->addSearchPath(QStringLiteral(BLD_PARTS_DIR));
        library->scan();
    }
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 1 << 20) return 0;  // parsers read whole files; keep runs quick
    const QByteArray bytes(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size));
    if (is("bbm")) {
        QBuffer buf;
        buf.setData(bytes);
        buf.open(QIODevice::ReadOnly);
        saveload::readBbm(buf);
    } else if (is("sidecar")) {
        core::Sidecar sidecar;
        saveload::readSidecar(writeInput(data, size, "bbm.bld"), QByteArray("x"), sidecar);
    } else if (is("venue")) {
        saveload::readVenueFile(writeInput(data, size, "bldvenue"));
    } else if (is("part_xml")) {
        // A fresh library each time so the same key can be scanned again.
        parts::PartsLibrary lib;
        const QString key = lib.scanFile(writeInput(data, size, "xml"));
        if (!key.isEmpty()) {
            lib.metadata(key);
            lib.footprint(key, 33.0);
        }
    } else if (is("budget")) {
        if (auto b = edit::Budget::read(writeInput(data, size, "bbb"))) b->write(scratch + QStringLiteral("/out.bbb"));
    } else if (is("ldraw_map")) {
        // Submodels make it an .mpd (the two parse differently).
        const bool mpd = bytes.contains("0 FILE");
        auto r = import::readLDrawMap(writeInput(data, size, mpd ? "mpd" : "ldr"), *library);
        if (r.map) import::writeLDrawMap(*r.map, scratch + (mpd ? QStringLiteral("/out.mpd") : QStringLiteral("/out.ldr")), *library);
    } else if (is("tdl")) {
        auto r = import::readTrackDesignerMap(writeInput(data, size, "tdl"), *library);
        if (r.map) import::writeTrackDesignerMap(*r.map, scratch + QStringLiteral("/out.tdl"), *library);
    } else if (is("ncp")) {
        auto r = import::readFourDBrixMap(writeInput(data, size, "ncp"), *library);
        if (r.map) import::writeFourDBrixMap(*r.map, scratch + QStringLiteral("/out.ncp"), *library);
    } else if (is("ldraw_import")) {
        import::readLDraw(writeInput(data, size, "ldr"));
    } else if (is("lxfml")) {
        import::readLDD(writeInput(data, size, "lxfml"));
    } else if (is("studio")) {
        import::readStudioIo(writeInput(data, size, "io"));
    } else {
        std::abort();  // unknown BLD_FUZZ_TARGET
    }
    return 0;
}
