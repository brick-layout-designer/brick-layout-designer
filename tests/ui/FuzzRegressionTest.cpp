// Inputs the parser fuzzers (fuzz/README.md) once broke, replayed on
// every build. Each must be handled quickly without undefined behaviour
// (the sanitizer job runs these too).

#include "import/mapformats/TrackDesignerMap.h"
#include "import/studio/StudioReader.h"
#include "parts/PartsLibrary.h"
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>

using namespace bld;

namespace {
QStringList regressions(const QString& pattern) {
    const QDir dir(QStringLiteral(BLD_SOURCE_DIR "/fixtures/fuzz-regressions"));
    QStringList out;
    for (const QString& f : dir.entryList({ pattern }, QDir::Files)) out << dir.filePath(f);
    return out;
}
}  // namespace

TEST(FuzzRegressions, TrackDesignerMapsWithAbsurdCoordinates) {
    parts::PartsLibrary lib;
    lib.addSearchPath(QStringLiteral(BLD_SOURCE_DIR "/parts/BlueBrickParts/parts"));
    lib.scan();
    QTemporaryDir tmp;
    const QStringList files = regressions(QStringLiteral("*.tdl"));
    ASSERT_FALSE(files.isEmpty());
    for (const QString& f : files) {
        SCOPED_TRACE(f.toStdString());
        auto r = import::readTrackDesignerMap(f, lib);
        if (r.map) import::writeTrackDesignerMap(*r.map, tmp.filePath(QStringLiteral("out.tdl")), lib);
    }
}

TEST(FuzzRegressions, StudioArchiveClaimingAHugeModel) {
    const QStringList files = regressions(QStringLiteral("*.io"));
    ASSERT_FALSE(files.isEmpty());
    for (const QString& f : files) {
        QElapsedTimer t;
        t.start();
        import::readStudioIo(f);
        EXPECT_LT(t.elapsed(), 5000) << f.toStdString();
    }
}
