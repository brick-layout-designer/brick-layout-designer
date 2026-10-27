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

// A Brick Track Designer export: the pieces its set uses are marked only
// used inside sets (<NotListedInLibrary>), the set itself isn't.
TEST(PartsZip, ThePiecesASetUsesAreOnlyUsedInsideIt) {
    QTemporaryDir dest;
    const auto r = addPartsFromZip(QStringLiteral(BLD_SOURCE_DIR "/fixtures/btd-layout-parts/Left switch cut 150.zip"), dest.path());
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    QStringList hidden = r.onlyInSets;
    hidden.sort();
    EXPECT_EQ(hidden, (QStringList{ QStringLiteral("Left_switch_cut_150.switch-base1"),
                                    QStringLiteral("Left_switch_cut_150.switch-base2"),
                                    QStringLiteral("Left_switch_cut_150.switch-base3") }));
    const auto read = [&](const QString& name) {
        QFile f(QDir(dest.path()).filePath(QStringLiteral("Left switch cut 150/") + name));
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    };
    EXPECT_TRUE(read(QStringLiteral("Left_switch_cut_150.switch-base1.xml")).contains(QStringLiteral("<NotListedInLibrary>true</NotListedInLibrary>")));
    EXPECT_FALSE(read(QStringLiteral("Left_switch_cut_150.whole.set.xml")).contains(QStringLiteral("NotListedInLibrary")));
}

// Brick Track Designer marks the pieces itself now (<NotListedInLibrary> on
// each piece, not on the set): adding the zip keeps one tag on each piece,
// lists them as only used inside the set, and leaves the set listed.
TEST(PartsZip, PiecesThatAlreadyCarryTheTagKeepJustOne) {
    const QString zip = QStringLiteral(BLD_SOURCE_DIR "/fixtures/btd-layout-parts/Monorail loop.zip");
    QTemporaryDir dest;
    const auto r = addPartsFromZip(zip, dest.path());
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    const QDir folder(QDir(dest.path()).filePath(QStringLiteral("Monorail loop")));
    const auto read = [&](const QString& name) {
        QFile f(folder.filePath(name));
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    };
    QStringList pieces;
    for (int i = 1; i <= 8; ++i) pieces << QStringLiteral("Monorail_loop.monorail-M-%1").arg(i);
    QStringList hidden = r.onlyInSets;
    hidden.sort();
    EXPECT_EQ(hidden, pieces);
    for (const QString& piece : pieces) {
        const QString xml = read(piece + QStringLiteral(".xml"));
        EXPECT_EQ(xml.count(QStringLiteral("NotListedInLibrary>")), 2) << piece.toStdString();  // one open, one close
        EXPECT_TRUE(xml.contains(QStringLiteral("<NotListedInLibrary>true</NotListedInLibrary>"))) << piece.toStdString();
    }
    EXPECT_FALSE(read(QStringLiteral("Monorail_loop.whole.set.xml")).contains(QStringLiteral("NotListedInLibrary")));

    // Added again over itself: still one each.
    const auto again = addPartsFromZip(zip, dest.path());
    ASSERT_TRUE(again.ok) << again.error.toStdString();
    EXPECT_EQ(read(pieces.front() + QStringLiteral(".xml")).count(QStringLiteral("NotListedInLibrary>")), 2);
}
