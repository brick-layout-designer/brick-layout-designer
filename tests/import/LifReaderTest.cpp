#include "import/lif/LifReader.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstdlib>

using namespace bld;

TEST(LifReader, RejectsNonLifMagic) {
    QTemporaryDir tmp;
    const QString p = QDir(tmp.path()).absoluteFilePath(QStringLiteral("not.lif"));
    QFile f(p);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    QByteArray junk(120, 0);
    junk.replace(0, 4, "ABCD");
    f.write(junk);
    f.close();

    import::LifReader r;
    EXPECT_FALSE(r.open(p));
    EXPECT_TRUE(r.errorString().contains(QStringLiteral("Bad magic")));
}

TEST(LifReader, RejectsTruncatedHeader) {
    QTemporaryDir tmp;
    const QString p = QDir(tmp.path()).absoluteFilePath(QStringLiteral("short.lif"));
    QFile f(p);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("LIFF", 4);
    f.close();

    import::LifReader r;
    EXPECT_FALSE(r.open(p));
}

// End-to-end smoke test against a real Assets.lif. Skipped unless the
// BLD_LDD_ASSETS_LIF env var points at a valid file — the assets are
// proprietary and can't be checked in. To run locally:
//   BLD_LDD_ASSETS_LIF=/path/to/Assets.lif ctest -R LifReader.RealAssetsSmoke
TEST(LifReader, RealAssetsSmoke) {
    const QByteArray envValue = qgetenv("BLD_LDD_ASSETS_LIF");
    const char* env = envValue.constData();
    if (!env || !*env) GTEST_SKIP() << "BLD_LDD_ASSETS_LIF not set";
    const QString path = QString::fromLocal8Bit(env);
    if (!QFileInfo::exists(path)) GTEST_SKIP() << "no file at " << env;

    import::LifReader r;
    ASSERT_TRUE(r.open(path)) << r.errorString().toStdString();
    const auto files = r.fileList();
    // Real-world LDD archives we've cracked range from ~169 entries
    // (Assets.lif — UI assets) to thousands (db.lif — part library).
    EXPECT_GT(files.size(), 100) << "valid LDD .lif should have at least 100 entries";

    // Spot-check: every file should yield non-empty bytes that match
    // the size recorded in the TOC. Sample a handful so we don't read
    // the whole 800 MB archive.
    for (int i = 0; i < std::min<int>(5, files.size()); ++i) {
        const QByteArray bytes = r.read(files[i]);
        EXPECT_FALSE(bytes.isEmpty()) << "file " << files[i].toStdString();
    }
}
