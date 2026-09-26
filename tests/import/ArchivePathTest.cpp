#include "import/ArchivePath.h"

#include <gtest/gtest.h>

#include <QDir>

using bld::import::resolveArchiveEntryPath;

namespace {

const QString kRoot = QDir::cleanPath(QDir::tempPath() + QStringLiteral("/bld-archive-root"));

}  // namespace

TEST(ArchivePath, ResolvesNestedEntriesUnderRoot) {
    EXPECT_EQ(resolveArchiveEntryPath(kRoot, QStringLiteral("ldraw/parts/3001.dat")),
              kRoot + QStringLiteral("/ldraw/parts/3001.dat"));
    EXPECT_EQ(resolveArchiveEntryPath(kRoot, QStringLiteral("a/./b/../c.txt")),
              kRoot + QStringLiteral("/a/c.txt"));
    EXPECT_EQ(resolveArchiveEntryPath(kRoot, QStringLiteral("dir\\file.gif")),
              kRoot + QStringLiteral("/dir/file.gif"));
}

TEST(ArchivePath, RejectsEntriesEscapingRoot) {
    EXPECT_TRUE(resolveArchiveEntryPath(kRoot, QStringLiteral("../evil")).isEmpty());
    EXPECT_TRUE(resolveArchiveEntryPath(kRoot, QStringLiteral("a/../../evil")).isEmpty());
    EXPECT_TRUE(resolveArchiveEntryPath(kRoot, QStringLiteral("..\\evil")).isEmpty());
    // Sibling directory sharing the root's name as a prefix.
    EXPECT_TRUE(resolveArchiveEntryPath(kRoot, QStringLiteral("../bld-archive-root-x/f")).isEmpty());
}

TEST(ArchivePath, RejectsAbsoluteAndDegenerateEntries) {
    EXPECT_TRUE(resolveArchiveEntryPath(kRoot, QStringLiteral("/etc/passwd")).isEmpty());
    EXPECT_TRUE(resolveArchiveEntryPath(kRoot, QStringLiteral("C:/Windows/evil.dll")).isEmpty());
    EXPECT_TRUE(resolveArchiveEntryPath(kRoot, QStringLiteral("C:evil.dll")).isEmpty());
    EXPECT_TRUE(resolveArchiveEntryPath(kRoot, QString()).isEmpty());
    EXPECT_TRUE(resolveArchiveEntryPath(kRoot, QStringLiteral(".")).isEmpty());
    EXPECT_TRUE(resolveArchiveEntryPath(kRoot, QStringLiteral("a/..")).isEmpty());
}
