#include "import/ldd/LDDGeomReader.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>

#include <cstdlib>
#include <cstring>

using namespace bld;

namespace {

// Helper: append a little-endian uint32 to a QByteArray.
void appendU32(QByteArray& b, quint32 v) {
    char buf[4];
    qToLittleEndian<quint32>(v, buf);
    b.append(buf, 4);
}
void appendF32(QByteArray& b, float v) {
    quint32 raw;
    std::memcpy(&raw, &v, 4);
    appendU32(b, raw);
}

// Build a minimal valid .g file: 1 triangle, 3 verts, no UVs, no bones.
QByteArray makeMinimalGeom() {
    QByteArray b;
    b.append("10GB", 4);
    appendU32(b, 3);          // vertex count
    appendU32(b, 3);          // index count
    appendU32(b, 0);          // options (no UVs, no bones)
    // Positions
    appendF32(b, 0); appendF32(b, 0); appendF32(b, 0);
    appendF32(b, 1); appendF32(b, 0); appendF32(b, 0);
    appendF32(b, 0); appendF32(b, 0); appendF32(b, 1);
    // Normals (3 verts x 3 floats — content irrelevant to the parser)
    for (int i = 0; i < 9; ++i) appendF32(b, 0.0f);
    // Indices
    appendU32(b, 0); appendU32(b, 1); appendU32(b, 2);
    return b;
}

}

TEST(LDDGeomReader, RejectsBadMagic) {
    QByteArray b(64, 0);
    b.replace(0, 4, "BAAD");
    auto r = import::readLDDGeom(b);
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.error.contains(QStringLiteral("magic")));
}

TEST(LDDGeomReader, ParsesMinimalSingleTriangle) {
    auto r = import::readLDDGeom(makeMinimalGeom());
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    ASSERT_EQ(r.mesh.tris.size(), 1u);
    // LDD-unit-to-LDU scale is 25 (1 LDD unit = 1.25 stud × 20 LDU/stud),
    // so the (1,0,0) source vertex lands at x=25.
    EXPECT_DOUBLE_EQ(r.mesh.tris[0].v[1].x, 25.0);
    EXPECT_DOUBLE_EQ(r.mesh.tris[0].v[2].z, 25.0);
}

TEST(LDDGeomReader, RejectsTruncatedHeader) {
    auto r = import::readLDDGeom(QByteArray("10GB", 4));
    EXPECT_FALSE(r.ok);
}

TEST(LDDGeomReader, RejectsIndexOutOfRange) {
    QByteArray b;
    b.append("10GB", 4);
    appendU32(b, 3);   // vc
    appendU32(b, 3);   // ic
    appendU32(b, 0);   // options
    for (int i = 0; i < 9; ++i) appendF32(b, 0.0f);   // positions
    for (int i = 0; i < 9; ++i) appendF32(b, 0.0f);   // normals
    appendU32(b, 0); appendU32(b, 1); appendU32(b, 99);  // bad index
    auto r = import::readLDDGeom(b);
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.error.contains(QStringLiteral("out of range")));
}

// Smoke test against a real .g shipped with LU or LDD. Gated by env
// var so CI without those installs skips. Run locally via:
//   BLD_LDD_GEOM_FILE=/path/to/3001.g ctest -R LDDGeomReader.RealFile
TEST(LDDGeomReader, RealFile) {
    const QByteArray envValue = qgetenv("BLD_LDD_GEOM_FILE");
    const char* env = envValue.constData();
    if (!env || !*env) GTEST_SKIP() << "BLD_LDD_GEOM_FILE not set";
    if (!QFileInfo::exists(QString::fromLocal8Bit(env)))
        GTEST_SKIP() << "no file at " << env;

    QFile f(QString::fromLocal8Bit(env));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    auto r = import::readLDDGeom(f.readAll());
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_GT(r.mesh.tris.size(), 0u);
}
