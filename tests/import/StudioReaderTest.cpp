// Tests for StudioReader. Builds a synthetic `.io` (ZIP) in a temp
// directory using QZipWriter, then runs it through readStudioIo.
//
// Uses Qt's private QZipWriter — same API we already use for the
// reader side, so no extra deps.

#include "import/studio/StudioReader.h"

#include "ZipCryptoTestZip.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>

#include <iostream>

#include <gtest/gtest.h>

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QTextStream>

#include <private/qzipwriter_p.h>

#include "core/Map.h"
#include "core/LayerBrick.h"

using namespace bld;

namespace {

QString writeIo(QTemporaryDir& dir, const QString& name,
                const QString& entryName, const QByteArray& entryData) {
    const QString path = dir.filePath(name);
    {
        QZipWriter zw(path);
        zw.setCompressionPolicy(QZipWriter::AlwaysCompress);
        zw.addFile(entryName, entryData);
    }
    return path;
}

}  // namespace

TEST(StudioReader, ReadsModelLdrFromValidIo) {
    QTemporaryDir dir;
    const QByteArray body =
        "0 Studio test\n"
        "1 16 0 0 0 1 0 0 0 1 0 0 0 1 3001.dat\n"
        "1 4 40 0 0 1 0 0 0 1 0 0 0 1 3002.dat\n";
    const QString io = writeIo(dir, "test.io", "model.ldr", body);
    auto r = import::readStudioIo(io);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(r.title, QStringLiteral("Studio test"));
    ASSERT_EQ(r.parts.size(), 2u);
    EXPECT_EQ(r.parts[0].filename, QStringLiteral("3001.dat"));
    EXPECT_EQ(r.parts[1].filename, QStringLiteral("3002.dat"));
    EXPECT_DOUBLE_EQ(r.parts[1].x, 40.0);
}

TEST(StudioReader, AcceptsMixedCaseEntryName) {
    // Real Studio 2.0 files sometimes capitalise "Model.ldr" or put it
    // under a nested path. Our reader should find it regardless.
    QTemporaryDir dir;
    const QString io = writeIo(dir, "capcase.io", "Model.LDR",
        QByteArray("0 mixed case\n"
                   "1 16 0 0 0 1 0 0 0 1 0 0 0 1 3001.dat\n"));
    auto r = import::readStudioIo(io);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(r.title, QStringLiteral("mixed case"));
    ASSERT_EQ(r.parts.size(), 1u);
}

TEST(StudioReader, MissingModelLdrFails) {
    QTemporaryDir dir;
    const QString io = writeIo(dir, "no-model.io", "other.txt",
                               QByteArray("not the model"));
    auto r = import::readStudioIo(io);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.isEmpty());
}

TEST(StudioReader, InvalidZipFails) {
    QTemporaryDir dir;
    const QString path = dir.filePath("bogus.io");
    {
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("this is not a zip");
    }
    auto r = import::readStudioIo(path);
    EXPECT_FALSE(r.ok);
}

TEST(StudioReader, ProducesPlaceableBrickMap) {
    // End-to-end: io → readStudioIo → toBlueBrickMap gives us a
    // usable map with one brick layer. This is the path the UI uses
    // when the user imports a .io as a library part.
    QTemporaryDir dir;
    const QString io = writeIo(dir, "e2e.io", "model.ldr",
        QByteArray("0 end to end\n"
                   "1 4 20 0 0 1 0 0 0 1 0 0 0 1 3001.dat\n"));
    auto r = import::readStudioIo(io);
    ASSERT_TRUE(r.ok);
    auto m = import::toBlueBrickMap(r);
    ASSERT_NE(m, nullptr);
    ASSERT_FALSE(m->layers().empty());
    const auto* L = static_cast<const core::LayerBrick*>(m->layers()[0].get());
    ASSERT_EQ(L->bricks.size(), 1u);
    // 20 LDU / 20 = 1 stud on the X axis.
    EXPECT_NEAR(L->bricks[0].displayArea.center().x(), 1.0, 1e-6);
}

// ---- Encrypted (ZipCrypto) .io, as Studio 2.1+ writes them ----

namespace {

QString writeBytes(QTemporaryDir& dir, const QString& name, const QByteArray& bytes) {
    const QString path = dir.filePath(name);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(bytes);
    return path;
}

const QByteArray kOneBrick = "0 FILE x.io\n1 4 0 0 0 1 0 0 0 1 0 0 0 1 3001.dat\n";

}  // namespace

TEST(StudioReader, ReadsEncryptedStudioFile) {
    // fixtures/studio/zipcrypto-small.io, written by scripts/make-zipcrypto-io.py.
    const auto r = import::readStudioIo(QStringLiteral(BLD_SOURCE_DIR "/fixtures/studio/zipcrypto-small.io"));
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(r.title, QStringLiteral("zipcrypto-small"));
    // model.ldr, not model2.ldr (BrickLink colours: 5 for 4) or modelv1.ldr.
    ASSERT_EQ(r.parts.size(), 4u);
    EXPECT_EQ(r.parts[0].filename, QStringLiteral("3001.dat"));
    EXPECT_EQ(r.parts[0].colorCode, 4);
    EXPECT_EQ(r.parts[1].filename, QStringLiteral("testcustom.dat"));
    // The submodel is placed: rotated a quarter turn, at z 100, its colour-16 brick in the submodel's colour 2.
    EXPECT_EQ(r.parts[2].filename, QStringLiteral("3023.dat"));
    EXPECT_EQ(r.parts[2].colorCode, 14);
    EXPECT_EQ(r.parts[3].colorCode, 2);
    EXPECT_DOUBLE_EQ(r.parts[3].x, 0.0);
    EXPECT_DOUBLE_EQ(r.parts[3].y, -8.0);
    EXPECT_DOUBLE_EQ(r.parts[3].z, 60.0);
    EXPECT_DOUBLE_EQ(r.parts[3].m[2], 1.0);
    EXPECT_DOUBLE_EQ(r.parts[3].m[6], -1.0);
    // The custom part is unpacked for the renderer; Studio's .conn / .col are not.
    ASSERT_EQ(r.extraPartDirs.size(), 1);
    EXPECT_TRUE(QFileInfo::exists(QDir(r.extraPartDirs[0]).filePath(QStringLiteral("testcustom.dat"))));
    EXPECT_EQ(QDir(r.extraPartDirs[0]).entryList(QDir::Files).size(), 1);
    EXPECT_TRUE(r.extracted);
}

TEST(StudioReader, UnpackedPartsGoAwayWithTheResult) {
    QString dir;
    {
        const auto r = import::readStudioIo(QStringLiteral(BLD_SOURCE_DIR "/fixtures/studio/zipcrypto-small.io"));
        ASSERT_EQ(r.extraPartDirs.size(), 1);
        dir = r.extraPartDirs[0];
        EXPECT_TRUE(QFileInfo(dir).isDir());
    }
    EXPECT_FALSE(QFileInfo::exists(dir));
}

TEST(StudioReader, ExplainsWrongPasswordAndAes) {
    QTemporaryDir dir;
    bld::test::CryptEntry other{ QStringLiteral("model.ldr"), kOneBrick };
    other.password = QByteArrayLiteral("someone else's");
    auto r = import::readStudioIo(writeBytes(dir, QStringLiteral("pw.io"), bld::test::buildZip({ other })));
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.error.contains(QStringLiteral("encrypted in a way we don't recognise"))) << r.error.toStdString();

    bld::test::CryptEntry aes{ QStringLiteral("model.ldr"), kOneBrick };
    aes.aes = true;
    r = import::readStudioIo(writeBytes(dir, QStringLiteral("aes.io"), bld::test::buildZip({ aes })));
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.error.contains(QStringLiteral("AES"))) << r.error.toStdString();

    r = import::readStudioIo(writeBytes(dir, QStringLiteral("none.io"),
                                        bld::test::buildZip({ { QStringLiteral("thumbnail.png"), "png" } })));
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.error.contains(QStringLiteral("no model"))) << r.error.toStdString();

    r = import::readStudioIo(writeBytes(dir, QStringLiteral("text.io"), "not a zip at all"));
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.error.contains(QStringLiteral("isn't a Studio model"))) << r.error.toStdString();
}

TEST(StudioReader, EmptyModelFallsBackButNotToTheLegacyFormat) {
    QTemporaryDir dir;
    const QByteArray bytes = bld::test::buildZip({
        { QStringLiteral("model.ldr"), "0 FILE x.io\n0 Name: x\n" },
        { QStringLiteral("modelv1.ldr"), "10 4 False 0 0 0 0 1 0 0 0 1 0 0 0 1 3001.dat\n" },
        { QStringLiteral("model2.ldr"), "1 5 0 0 0 1 0 0 0 1 0 0 0 1 3002.dat\n" },
    });
    const auto r = import::readStudioIo(writeBytes(dir, QStringLiteral("fb.io"), bytes));
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    ASSERT_EQ(r.parts.size(), 1u);
    EXPECT_EQ(r.parts[0].filename, QStringLiteral("3002.dat"));
}

TEST(StudioReader, CustomPartsCannotEscapeTheirFolder) {
    QTemporaryDir dir;
    const QByteArray bytes = bld::test::buildZip({
        { QStringLiteral("model.ldr"), kOneBrick },
        { QStringLiteral("CustomParts/../evil.dat"), "3 16 0 0 0 1 0 0 0 0 1\n" },
        { QStringLiteral("CustomParts/connectivity/x.dat"), "0 nested\n" },
        { QStringLiteral("CustomParts//abs.dat"), "0 abs\n" },
        { QStringLiteral("CustomParts/ok.dat"), "0 ok\n" },
    });
    const auto r = import::readStudioIo(writeBytes(dir, QStringLiteral("trav.io"), bytes));
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    ASSERT_EQ(r.extraPartDirs.size(), 1);
    EXPECT_EQ(QDir(r.extraPartDirs[0]).entryList(QDir::Files), QStringList{ QStringLiteral("ok.dat") });
    EXPECT_FALSE(QFileInfo::exists(QDir(r.extraPartDirs[0]).filePath(QStringLiteral("../evil.dat"))));
}

TEST(StudioReader, BombsAreRefusedQuickly) {
    QTemporaryDir dir;
    // A model of 200 MB of spaces (well past the 128 MB cap) in a few hundred KB.
    const QByteArray bytes = bld::test::buildZip({ { QStringLiteral("model.ldr"), QByteArray(200 << 20, ' ') } });
    QElapsedTimer t;
    t.start();
    const auto r = import::readStudioIo(writeBytes(dir, QStringLiteral("bomb.io"), bytes));
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.error.contains(QStringLiteral("too large"))) << r.error.toStdString();
    EXPECT_LT(t.elapsed(), 1000);
}

TEST(StudioReader, NestedSubmodelBombIsRefused) {
    // Ten levels, each placing the next ten times: 10^10 bricks if flattened.
    QByteArray model;
    for (int i = 0; i < 10; ++i) {
        model += "0 FILE m" + QByteArray::number(i) + "\n";
        for (int k = 0; k < 10; ++k) model += "1 16 0 0 0 1 0 0 0 1 0 0 0 1 m" + QByteArray::number(i + 1) + "\n";
    }
    model += "0 FILE m10\n1 4 0 0 0 1 0 0 0 1 0 0 0 1 3001.dat\n";
    QTemporaryDir dir;
    QElapsedTimer t;
    t.start();
    const auto r = import::readStudioIo(writeBytes(dir, QStringLiteral("nest.io"),
                                                   bld::test::buildZip({ { QStringLiteral("model.ldr"), model } })));
    EXPECT_FALSE(r.ok);
    EXPECT_LT(t.elapsed(), 10000);
}

// Opt-in: a real Studio file (they're proprietary, never committed).
//   BLD_STUDIO_IO=/path/to/set.io
TEST(StudioReader, RealStudioFile) {
    const QString path = qEnvironmentVariable("BLD_STUDIO_IO");
    if (path.isEmpty()) GTEST_SKIP() << "set BLD_STUDIO_IO to a Studio .io file";
    QElapsedTimer t;
    t.start();
    const auto r = import::readStudioIo(path);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_GT(r.parts.size(), 0u);
    std::cout << "[ read ] " << r.parts.size() << " parts, " << r.primitives.size() << " primitives, "
              << r.extraPartDirs.size() << " custom-part folder(s), title \"" << r.title.toStdString()
              << "\" in " << t.elapsed() << " ms\n";
    for (const QString& w : r.warnings) std::cout << "[ warn ] " << w.toStdString() << '\n';
}

TEST(StudioReader, DeepSubmodelChainsAreRefused) {
    // Forty submodels, each placing the next once: deeper than any real model.
    QByteArray model;
    for (int i = 0; i < 40; ++i)
        model += "0 FILE m" + QByteArray::number(i) + "\n1 16 0 0 0 1 0 0 0 1 0 0 0 1 m" + QByteArray::number(i + 1) + "\n";
    model += "0 FILE m40\n1 4 0 0 0 1 0 0 0 1 0 0 0 1 3001.dat\n";
    QTemporaryDir dir;
    auto r = import::readStudioIo(writeBytes(dir, QStringLiteral("deep.io"),
                                             bld::test::buildZip({ { QStringLiteral("model.ldr"), model } })));
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.error.contains(QStringLiteral("nest too deeply"))) << r.error.toStdString();
    // Ten deep is fine.
    model.clear();
    for (int i = 0; i < 10; ++i)
        model += "0 FILE m" + QByteArray::number(i) + "\n1 16 0 0 0 1 0 0 0 1 0 0 0 1 m" + QByteArray::number(i + 1) + "\n";
    model += "0 FILE m10\n1 4 0 0 0 1 0 0 0 1 0 0 0 1 3001.dat\n";
    r = import::readStudioIo(writeBytes(dir, QStringLiteral("ten.io"),
                                        bld::test::buildZip({ { QStringLiteral("model.ldr"), model } })));
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(r.parts.size(), 1u);
}
