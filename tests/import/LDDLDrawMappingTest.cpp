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
    EXPECT_EQ(m.colorFor(23), 1);
    EXPECT_EQ(m.colorFor(24), 14);
    EXPECT_EQ(m.colorFor(99), -1);
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

TEST(LDDLDrawMapping, TrainTrackWithoutAnLdrawXml) {
    // LDD's bundled ldraw.xml has no train track: the built-in table maps it.
    const import::LDDLDrawMapping none;
    EXPECT_EQ(none.partFor(QStringLiteral("64573")), QStringLiteral("88492.dat"));
    EXPECT_EQ(none.partFor(QStringLiteral("53401")), QStringLiteral("53401.dat"));
    EXPECT_TRUE(none.partFor(QStringLiteral("3001")).isEmpty());
    const auto straight = none.transformFor(QStringLiteral("53401.dat"));
    ASSERT_TRUE(straight.exists);
    EXPECT_DOUBLE_EQ(straight.tx, 6.0);
    EXPECT_DOUBLE_EQ(straight.angle, 1.570796);
    EXPECT_FALSE(none.transformFor(QStringLiteral("3001.dat")).exists);

    // A file's own entries win.
    QTemporaryDir dir;
    import::LDDLDrawMapping file;
    ASSERT_TRUE(file.loadFromFile(writeXml(dir,
        "<LDrawMapping><Brick ldraw=\"x.dat\" lego=\"53401\"/>"
        "<Transformation ldraw=\"53401.dat\" tx=\"1\" ty=\"0\" tz=\"0\" ax=\"0\" ay=\"1\" az=\"0\" angle=\"0\"/></LDrawMapping>")));
    EXPECT_EQ(file.partFor(QStringLiteral("53401")), QStringLiteral("x.dat"));
    EXPECT_DOUBLE_EQ(file.transformFor(QStringLiteral("53401.dat")).tx, 1.0);
}
