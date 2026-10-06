#include "saveload/BbmReader.h"
#include "saveload/BbmWriter.h"

#include "core/LayerBrick.h"
#include "core/LayerGrid.h"
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>

using namespace bld;

namespace {

QString corpusDir() { return QString::fromUtf8(BLD_BBM_CORPUS_DIR); }

bool corpusAvailable() { return QDir(corpusDir()).exists(); }

}

TEST(RealFixture, TightCornerLoads) {
    if (!corpusAvailable()) GTEST_SKIP() << "corpus dir missing: " << corpusDir().toStdString();

    const QString path = corpusDir() + QStringLiteral("/tight-corner.bbm");
    if (!QFile::exists(path)) GTEST_SKIP() << "tight-corner.bbm not present";

    auto result = saveload::readBbm(path);
    ASSERT_TRUE(result.ok()) << result.error.toStdString();
    EXPECT_EQ(result.map->dataVersion, core::Map::kCurrentDataVersion);
    EXPECT_FALSE(result.map->layers().empty()) << "real project should have layers";

    // Count kinds to make sure dispatch actually populated subclasses.
    int nGrid = 0, nBrick = 0;
    for (const auto& L : result.map->layers()) {
        if (L->kind() == core::LayerKind::Grid) ++nGrid;
        else if (L->kind() == core::LayerKind::Brick) ++nBrick;
    }
    // A typical train-club layout has at least a grid + a brick layer.
    EXPECT_GE(nGrid,  1);
    EXPECT_GE(nBrick, 1);
}

namespace {

void assertByteExactRoundTrip(const QString& path) {
    QFile src(path);
    ASSERT_TRUE(src.open(QIODevice::ReadOnly)) << path.toStdString();
    const QByteArray original = src.readAll();

    auto loaded = saveload::readBbm(path);
    ASSERT_TRUE(loaded.ok()) << loaded.error.toStdString();

    QByteArray written;
    {
        QBuffer out(&written);
        ASSERT_TRUE(out.open(QIODevice::WriteOnly));
        ASSERT_TRUE(saveload::writeBbm(*loaded.map, out).ok);
    }

    ASSERT_EQ(written.size(), original.size())
        << "byte-count mismatch for " << path.toStdString();
    EXPECT_EQ(written, original) << "bytes differ for " << path.toStdString();
}

}

TEST(RealFixture, CorpusByteExactRoundTrip) {
    // Gold-standard Phase 1 gate: every real .bbm in fixtures/bbm-corpus/ must
    // load + re-save byte-for-byte identical. Iterates the directory so any
    // fixture dropped in gets coverage automatically.
    if (!corpusAvailable()) GTEST_SKIP();
    QDir dir(corpusDir());
    const auto files = dir.entryList({ QStringLiteral("*.bbm") }, QDir::Files, QDir::Name);
    if (files.isEmpty()) GTEST_SKIP() << "no .bbm fixtures present";

    for (const QString& f : files) {
        SCOPED_TRACE(f.toStdString());
        assertByteExactRoundTrip(dir.absoluteFilePath(f));
    }
}

TEST(RealFixture, TightCornerSemanticRoundTrip) {
    // Load -> save to an in-memory buffer -> load again -> compare field-level
    // identity (not byte-exact; byte-exact is a later gate).
    if (!corpusAvailable()) GTEST_SKIP();
    const QString path = corpusDir() + QStringLiteral("/tight-corner.bbm");
    if (!QFile::exists(path)) GTEST_SKIP();

    auto first = saveload::readBbm(path);
    ASSERT_TRUE(first.ok()) << first.error.toStdString();

    QByteArray roundBuf;
    {
        QBuffer out(&roundBuf);
        ASSERT_TRUE(out.open(QIODevice::WriteOnly));
        ASSERT_TRUE(saveload::writeBbm(*first.map, out).ok);
    }

    QBuffer in(&roundBuf);
    ASSERT_TRUE(in.open(QIODevice::ReadOnly));
    auto second = saveload::readBbm(in);
    ASSERT_TRUE(second.ok()) << second.error.toStdString();

    // Basic semantic equality at map-header + layer-count level.
    EXPECT_EQ(second.map->author, first.map->author);
    EXPECT_EQ(second.map->lug,    first.map->lug);
    EXPECT_EQ(second.map->event,  first.map->event);
    EXPECT_EQ(second.map->date,   first.map->date);
    EXPECT_EQ(second.map->backgroundColor, first.map->backgroundColor);
    ASSERT_EQ(second.map->layers().size(), first.map->layers().size());

    // Compare brick counts per layer — catches item-list truncation.
    for (size_t i = 0; i < first.map->layers().size(); ++i) {
        if (first.map->layers()[i]->kind() != core::LayerKind::Brick) continue;
        const auto* a = static_cast<const core::LayerBrick*>(first.map->layers()[i].get());
        const auto* b = static_cast<const core::LayerBrick*>(second.map->layers()[i].get());
        EXPECT_EQ(a->bricks.size(), b->bricks.size()) << "brick count changed on round-trip (layer " << i << ")";
        EXPECT_EQ(a->groups.size(), b->groups.size()) << "group count changed (layer " << i << ")";
    }
}
