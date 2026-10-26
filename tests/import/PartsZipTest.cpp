// import::addPartsFromZip's caps: an archive over its limits writes nothing.

#include "import/PartsZip.h"
#include "import/zip/ZipWriter.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace bld::import;

namespace {

QString writeZip(const QTemporaryDir& dir, int files, int bytesEach) {
    ZipWriter w;
    for (int i = 0; i < files; ++i)
        w.add(QStringLiteral("Lot/P.%1.xml").arg(i), QByteArray(bytesEach, 'x'), ZipWriter::Method::Stored);
    const QString path = dir.filePath(QStringLiteral("lot.zip"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return {};
    f.write(w.finish());
    return path;
}

} // namespace

TEST(PartsZip, TooManyEntriesWritesNothing) {
    QTemporaryDir src, dest;
    PartsZipLimits limits;
    limits.maxEntries = 5;
    const auto r = addPartsFromZip(writeZip(src, 6, 10), dest.path(), limits);
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.error.contains(QStringLiteral("too many files")));
    EXPECT_TRUE(QDir(dest.path()).isEmpty());
}

TEST(PartsZip, TooBigUnpackedWritesNothing) {
    QTemporaryDir src, dest;
    PartsZipLimits limits;
    limits.maxTotalBytes = 250;
    const auto r = addPartsFromZip(writeZip(src, 3, 100), dest.path(), limits);
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.error.contains(QStringLiteral("more than")));
    EXPECT_TRUE(QDir(dest.path()).isEmpty());
}

TEST(PartsZip, AnEntryTooBigWritesNothing) {
    QTemporaryDir src, dest;
    PartsZipLimits limits;
    limits.maxEntryBytes = 50;
    const auto r = addPartsFromZip(writeZip(src, 2, 100), dest.path(), limits);
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(QDir(dest.path()).isEmpty());
}

TEST(PartsZip, WithinTheLimitsListsTheFolderAndParts) {
    QTemporaryDir src, dest;
    const auto r = addPartsFromZip(writeZip(src, 2, 10), dest.path());
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(r.folders, QStringList{ QStringLiteral("Lot") });
    EXPECT_EQ(r.partKeys, (QStringList{ QStringLiteral("P.0"), QStringLiteral("P.1") }));
    EXPECT_TRUE(QFile::exists(dest.filePath(QStringLiteral("Lot/P.1.xml"))));
}

TEST(PartsZip, NotAZipSaysSo) {
    QTemporaryDir src, dest;
    QFile f(src.filePath(QStringLiteral("x.zip")));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("not a zip");
    f.close();
    const auto r = addPartsFromZip(f.fileName(), dest.path());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.error.contains(QStringLiteral("isn't a zip")));
}
