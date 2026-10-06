#include "import/ldd/LDDMaterials.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstdlib>

using namespace bld;

TEST(LDDMaterials, ParsesBasicEntries) {
    import::LDDMaterials m;
    ASSERT_TRUE(m.loadFromBytes(QByteArray(R"XML(<?xml version="1.0"?>
<Materials>
  <Material MatID="1" Red="244" Green="244" Blue="244" Alpha="255" MaterialType="shinyPlastic"/>
  <Material MatID="10" Red="255" Green="255" Blue="189" Alpha="150" MaterialType="shinyPlastic"/>
</Materials>
)XML")));
    EXPECT_EQ(m.count(), 2);
    EXPECT_EQ(m.color(1), QColor::fromRgb(244, 244, 244, 255));
    const auto trans = m.color(10);
    EXPECT_EQ(trans.alpha(), 150);
    EXPECT_TRUE(m.contains(10));
    EXPECT_FALSE(m.contains(999));
    EXPECT_FALSE(m.color(999).isValid());
}

TEST(LDDMaterials, AlphaDefaultsTo255WhenAbsent) {
    import::LDDMaterials m;
    ASSERT_TRUE(m.loadFromBytes(QByteArray(R"XML(<?xml version="1.0"?>
<Materials>
  <Material MatID="42" Red="100" Green="100" Blue="100"/>
</Materials>
)XML")));
    EXPECT_EQ(m.color(42).alpha(), 255);
}

TEST(LDDMaterials, RealLDDFileSmoke) {
    const QByteArray envValue = qgetenv("BLD_LDD_MATERIALS_XML");
    const char* env = envValue.constData();
    if (!env || !*env) GTEST_SKIP() << "BLD_LDD_MATERIALS_XML not set";
    if (!QFileInfo::exists(QString::fromLocal8Bit(env)))
        GTEST_SKIP() << "no file at " << env;

    import::LDDMaterials m;
    ASSERT_TRUE(m.loadFromFile(QString::fromLocal8Bit(env)));
    // Real LDD palette has ~150 entries.
    EXPECT_GT(m.count(), 100);
    EXPECT_TRUE(m.color(1).isValid());
}
