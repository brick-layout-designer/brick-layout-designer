// SafeZip: ZIP reading for untrusted archives.

#include "import/zip/SafeZip.h"

#include "ZipCryptoTestZip.h"

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

// ---- ZipCrypto (BrickLink Studio .io) ----

namespace {

const QList<QByteArray> kStudio = { QByteArrayLiteral("soho0909"), QByteArray() };
const QByteArray kModel = QByteArray("0 model\n1 4 0 0 0 1 0 0 0 1 0 0 0 1 3001.dat\n").repeated(40);

}  // namespace

TEST(SafeZip, ReadsZipCryptoEntries) {
    for (bool deflate : { false, true }) {
        for (bool descriptor : { false, true }) {
            SCOPED_TRACE(std::string(deflate ? "deflated" : "stored") + (descriptor ? ", data descriptor" : ""));
            bld::test::CryptEntry e{ QStringLiteral("model.ldr"), kModel, deflate };
            e.dataDescriptor = descriptor;
            const SafeZip zip(bld::test::buildZip({ e }), kStudio);
            ASSERT_TRUE(zip.isValid());
            ASSERT_TRUE(zip.entries()[0].encrypted);
            SafeZip::ReadError why = SafeZip::ReadError::Damaged;
            EXPECT_EQ(zip.read(zip.entries()[0], qint64(1) << 30, &why).value_or("x"), kModel);
            EXPECT_EQ(why, SafeZip::ReadError::None);
        }
    }
}

TEST(SafeZip, TriesEachPassword) {
    bld::test::CryptEntry empty{ QStringLiteral("a.ldr"), kModel };
    empty.password = QByteArray();
    const QByteArray bytes = bld::test::buildZip({ empty });
    EXPECT_EQ(SafeZip(bytes, kStudio).read(SafeZip(bytes).entries()[0]).value_or("x"), kModel);
    const QByteArray studio = bld::test::buildZip({ { QStringLiteral("a.ldr"), kModel } });
    const SafeZip later(studio, { QByteArrayLiteral("nope"), QByteArrayLiteral("soho0909") });
    EXPECT_EQ(later.read(later.entries()[0]).value_or("x"), kModel);
}

TEST(SafeZip, WrongPasswordIsRefused) {
    const QByteArray bytes = bld::test::buildZip({ { QStringLiteral("a.ldr"), kModel } });
    for (const QList<QByteArray>& passwords : { QList<QByteArray>{}, QList<QByteArray>{ "wrong", "" } }) {
        const SafeZip zip(bytes, passwords);
        ASSERT_TRUE(zip.isValid());
        SafeZip::ReadError why = SafeZip::ReadError::None;
        EXPECT_FALSE(zip.read(zip.entries()[0], qint64(1) << 30, &why));
        EXPECT_TRUE(why == SafeZip::ReadError::WrongPassword || why == SafeZip::ReadError::Damaged);
    }
    // "wrong" fails the header check byte outright.
    SafeZip::ReadError why = SafeZip::ReadError::None;
    const SafeZip zip(bytes, { QByteArrayLiteral("wrong") });
    EXPECT_FALSE(zip.read(zip.entries()[0], qint64(1) << 30, &why));
    EXPECT_EQ(why, SafeZip::ReadError::WrongPassword);
}

TEST(SafeZip, AesIsRefused) {
    bld::test::CryptEntry e{ QStringLiteral("model.ldr"), kModel, false };
    e.aes = true;
    const SafeZip zip(bld::test::buildZip({ e }), kStudio);
    ASSERT_TRUE(zip.isValid());
    EXPECT_TRUE(zip.entries()[0].aes);
    SafeZip::ReadError why = SafeZip::ReadError::None;
    EXPECT_FALSE(zip.read(zip.entries()[0], qint64(1) << 30, &why));
    EXPECT_EQ(why, SafeZip::ReadError::AesEncrypted);
}

TEST(SafeZip, EncryptedDamageAndBombsAreRefused) {
    QByteArray bytes = bld::test::buildZip({ { QStringLiteral("model.ldr"), kModel } });
    // A flipped byte after the encryption header: decrypts to garbage, fails inflation or the CRC.
    bytes[30 + 9 + 20] = static_cast<char>(bytes[30 + 9 + 20] ^ 0x21);
    SafeZip::ReadError why = SafeZip::ReadError::None;
    const SafeZip damaged(bytes, kStudio);
    EXPECT_FALSE(damaged.read(damaged.entries()[0], qint64(1) << 30, &why));
    EXPECT_EQ(why, SafeZip::ReadError::Damaged);

    // A real bomb: 64 MB of zeros in a few KB, encrypted. Over the caller's cap: refused before inflating.
    const QByteArray zeros(64 << 20, '\0');
    const SafeZip bomb(bld::test::buildZip({ { QStringLiteral("model.ldr"), zeros } }), kStudio);
    ASSERT_TRUE(bomb.isValid());
    QElapsedTimer t;
    t.start();
    EXPECT_FALSE(bomb.read(bomb.entries()[0], qint64(16) << 20, &why));
    EXPECT_EQ(why, SafeZip::ReadError::TooBig);
    EXPECT_LT(t.elapsed(), 200);

    // Claiming more than 1100x the archive: the whole archive is refused.
    bld::test::CryptEntry huge{ QStringLiteral("model.ldr"), kModel };
    huge.claimedSize = 0xFFFFFF00u;
    EXPECT_FALSE(SafeZip(bld::test::buildZip({ huge }), kStudio).isValid());

    // Claiming less than the stream holds: inflation stops at the claim and fails.
    bld::test::CryptEntry shortClaim{ QStringLiteral("model.ldr"), kModel };
    shortClaim.claimedSize = 100;
    const SafeZip lying(bld::test::buildZip({ shortClaim }), kStudio);
    EXPECT_FALSE(lying.read(lying.entries()[0], qint64(1) << 30, &why));
    EXPECT_EQ(why, SafeZip::ReadError::Damaged);
}

TEST(SafeZip, ReadsTheGeneratedStudioFixture) {
    // fixtures/studio/zipcrypto-small.io, written by scripts/make-zipcrypto-io.py.
    const auto zip = SafeZip::open(QStringLiteral(BLD_SOURCE_DIR "/fixtures/studio/zipcrypto-small.io"), kStudio);
    ASSERT_TRUE(zip);
    ASSERT_EQ(zip->entries().size(), 9);
    for (const auto& e : zip->entries()) {
        SCOPED_TRACE(e.name.toStdString());
        EXPECT_TRUE(e.encrypted);
        EXPECT_TRUE(zip->read(e));
    }
    EXPECT_TRUE(zip->read(*zip->find(QStringLiteral("model.ldr")))->contains("3001.dat"));
}
