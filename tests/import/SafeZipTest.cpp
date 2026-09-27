// SafeZip: ZIP reading for untrusted archives.

#include "import/zip/SafeZip.h"

#include <gtest/gtest.h>

#include <QBuffer>
#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QtEndian>

#include <private/qzipwriter_p.h>

using namespace bld::import;

namespace {

QByteArray makeZip(bool compress) {
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    QZipWriter w(&buf);
    w.setCompressionPolicy(compress ? QZipWriter::AlwaysCompress : QZipWriter::NeverCompress);
    w.addDirectory(QStringLiteral("dir"));
    w.addFile(QStringLiteral("dir/model.ldr"), QByteArray("0 model\n1 4 0 0 0 1 0 0 0 1 0 0 0 1 3001.dat\n").repeated(50));
    w.addFile(QStringLiteral("empty.txt"), QByteArray());
    w.close();
    return buf.data();
}

}  // namespace

TEST(SafeZip, ReadsStoredAndDeflatedEntries) {
    for (bool compress : { false, true }) {
        SCOPED_TRACE(compress ? "deflated" : "stored");
        const SafeZip zip(makeZip(compress));
        ASSERT_TRUE(zip.isValid());
        ASSERT_EQ(zip.entries().size(), 3);
        EXPECT_TRUE(zip.entries()[0].isDir);
        const auto* model = zip.find(QStringLiteral("DIR/MODEL.LDR"), Qt::CaseInsensitive);
        ASSERT_TRUE(model);
        const auto data = zip.read(*model);
        ASSERT_TRUE(data);
        EXPECT_TRUE(data->startsWith("0 model\n"));
        EXPECT_EQ(data->size(), 45 * 50);
        EXPECT_EQ(zip.read(*zip.find(QStringLiteral("empty.txt"))).value_or("x"), QByteArray());
        EXPECT_FALSE(zip.read(*model, 10)) << "bigger than the caller allows";
    }
}

TEST(SafeZip, RejectsDamage) {
    const QByteArray good = makeZip(true);
    // Truncated: the directory is gone.
    EXPECT_FALSE(SafeZip(good.left(good.size() / 2)).isValid());
    // A flipped byte in the compressed data fails inflation or the CRC.
    QByteArray bad = good;
    const qsizetype at = bad.indexOf("dir/model.ldr") + 13 + 5;  // inside the first entry's data
    bad[at] = static_cast<char>(bad[at] ^ 0x55);
    const SafeZip zip(bad);
    ASSERT_TRUE(zip.isValid());
    EXPECT_FALSE(zip.read(*zip.find(QStringLiteral("dir/model.ldr"))));
    // A directory entry claiming 4 GB of output is refused up front.
    QByteArray huge = good;
    const qsizetype cd = huge.indexOf("PK\x01\x02");
    qToLittleEndian<quint32>(0xFFFFFFF0u, huge.data() + cd + 24);
    EXPECT_FALSE(SafeZip(huge).isValid());
}

TEST(SafeZip, TruncatedDeflateStreamFromFuzzing) {
    // Qt's QZipReader looped doubling its buffer on this until memory ran out.
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/fixtures/fuzz-regressions/studio-truncated-deflate.io"));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    QElapsedTimer t;
    t.start();
    const SafeZip zip(f.readAll());
    for (const auto& e : zip.entries()) zip.read(e);
    EXPECT_LT(t.elapsed(), 2000);
}
