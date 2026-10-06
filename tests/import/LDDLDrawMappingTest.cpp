#include "import/ldd/LDDLDrawMapping.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstdlib>

using namespace bld;

namespace {

QString writeXml(const QTemporaryDir& dir, const QByteArray& body) {
    const QString p = QDir(dir.path()).absoluteFilePath(QStringLiteral("ldraw.xml"));
    QFile f(p);
    EXPECT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(body);
    return p;
}

}

TEST(LDDLDrawMapping, ParsesAllThreeElementKinds) {
    QTemporaryDir tmp;
    const QString p = writeXml(tmp, QByteArray(R"XML(
<LDrawMapping>
<Material ldraw="1" lego="23" />
<Material ldraw="14" lego="24" />
<Brick ldraw="3001.dat" lego="3001" />
<Brick ldraw="30237.dat" lego="95820" />
<Transformation ldraw="3001.dat" tx="0.4" ty="-0.96" tz="0" ax="0" ay="1" az="0" angle="0" />
</LDrawMapping>
)XML"));
    import::LDDLDrawMapping m;
    ASSERT_TRUE(m.loadFromFile(p));
    EXPECT_EQ(m.materialCount(), 2);
    EXPECT_EQ(m.brickCount(), 2);
    EXPECT_EQ(m.transformCount(), 1);
    EXPECT_EQ(m.colourFor(23), 1);
    EXPECT_EQ(m.colourFor(24), 14);
    EXPECT_EQ(m.colourFor(99), -1);
    EXPECT_EQ(m.partFor(QStringLiteral("3001")),  QStringLiteral("3001.dat"));
    EXPECT_EQ(m.partFor(QStringLiteral("95820")), QStringLiteral("30237.dat"));
    EXPECT_TRUE(m.partFor(QStringLiteral("0000")).isEmpty());
    const auto t = m.transformFor(QStringLiteral("3001.dat"));
    EXPECT_TRUE(t.exists);
    EXPECT_DOUBLE_EQ(t.tx, 0.4);
    EXPECT_DOUBLE_EQ(t.ty, -0.96);
}

TEST(LDDLDrawMapping, IgnoresMalformedEntries) {
    QTemporaryDir tmp;
    const QString p = writeXml(tmp, QByteArray(R"XML(
<LDrawMapping>
<Material ldraw="not-a-number" lego="23" />
<Brick lego="3001" />
<Brick ldraw="" lego="3002" />
<Material ldraw="14" lego="24" />
</LDrawMapping>
)XML"));
    import::LDDLDrawMapping m;
    ASSERT_TRUE(m.loadFromFile(p));
    EXPECT_EQ(m.materialCount(), 1);
    EXPECT_EQ(m.brickCount(), 0);
}

// End-to-end smoke test against the ldraw.xml that ships with LDD,
// gated behind BLD_LDD_LDRAW_XML so CI without an LDD install skips.
TEST(LDDLDrawMapping, RealLDDFileSmoke) {
    const QByteArray envValue = qgetenv("BLD_LDD_LDRAW_XML");
    const char* env = envValue.constData();
    if (!env || !*env) GTEST_SKIP() << "BLD_LDD_LDRAW_XML not set";
    if (!QFileInfo::exists(QString::fromLocal8Bit(env)))
        GTEST_SKIP() << "no file at " << env;

    import::LDDLDrawMapping m;
    ASSERT_TRUE(m.loadFromFile(QString::fromLocal8Bit(env)));
    // The LDD-bundled ldraw.xml has ~100 brick + ~20 assembly entries
    // and ~110 material entries. Numbers vary across LDD versions but
    // are always in the low hundreds — anything below 50 indicates a
    // parsing failure.
    EXPECT_GT(m.brickCount(), 50);
    EXPECT_GT(m.materialCount(), 50);
    EXPECT_GT(m.transformCount(), 100);
}
