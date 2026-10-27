// A set's version (parts/SetVersion.h): the same text and digest the web's
// server makes for the same set (apps/server/src/utils/setVersion.test.ts
// has these vectors), changing with its parts, where they sit, or its name.

#include "parts/SetVersion.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QTemporaryDir>

using namespace bld;

namespace {

parts::PartMetadata yard() {
    parts::PartMetadata m;
    m.kind = parts::PartKind::Group;
    m.descriptions = { { QStringLiteral("fr"), QStringLiteral("Gare") },
                       { QStringLiteral("en"), QStringLiteral(" Yard ") } };
    m.subparts = { { QStringLiteral("Shed"), QPointF(0, -11.5), 0.0 },
                   { QStringLiteral("lamp.1"), QPointF(8.0004, 0.25), -90.0 } };
    return m;
}

} // namespace

TEST(SetVersion, IsTheWebsForTheSameSet) {
    EXPECT_EQ(parts::setVersionText(yard()),
              QStringLiteral("set-v1\nYard\nshed|0|-11500|0\nlamp.1|8000|250|-90000\n"));
    EXPECT_EQ(parts::setVersion(yard()), QStringLiteral("1572ba9befec980c"));
    // A part isn't a set: no version.
    EXPECT_TRUE(parts::setVersion(parts::PartMetadata{}).isEmpty());
}

TEST(SetVersion, ChangesWithAPartAPlaceOrTheNameOnly) {
    const QString v = parts::setVersion(yard());
    auto upper = yard();
    for (auto& sp : upper.subparts) sp.subKey = sp.subKey.toUpper();
    EXPECT_EQ(parts::setVersion(upper), v);
    auto fewer = yard();
    fewer.subparts.removeLast();
    EXPECT_NE(parts::setVersion(fewer), v);
    auto moved = yard();
    moved.subparts[1].position.setX(9);
    EXPECT_NE(parts::setVersion(moved), v);
    auto renamed = yard();
    renamed.descriptions[1].text = QStringLiteral("Yard sidings");
    EXPECT_NE(parts::setVersion(renamed), v);
}

// Read from a set file, as the library reads one: the same version the
// server gives the same XML.
TEST(SetVersion, FromASetFile) {
    QTemporaryDir dir;
    QFile f(dir.filePath(QStringLiteral("Yard.set.xml")));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<group><Description><en> Yard "
            "</en><fr>Gare</fr></Description>"
            "<SubPartList><SubPart "
            "id=\"Shed\"><position><x>0</x><y>-11.5</y></position><angle>0</angle></SubPart>"
            "<SubPart id=\"lamp.1\"><position><x>8.0004</x><y>0.25</y></position><angle>-90</angle></SubPart>"
            "</SubPartList></group>");
    f.close();
    parts::PartsLibrary lib;
    lib.addSearchPath(dir.path());
    lib.scan();
    const auto meta = lib.metadata(QStringLiteral("Yard"));
    ASSERT_TRUE(meta);
    EXPECT_EQ(parts::setVersion(*meta), QStringLiteral("1572ba9befec980c"));
}

// A custom set inside a set: Yard holds the custom set Shed. The same
// vectors as the web's setVersion.test.ts ("a set holding custom sets").
TEST(SetVersion, NestedIsTheWebsForTheSameSets) {
    parts::PartMetadata shed;
    shed.kind = parts::PartKind::Group;
    shed.descriptions = { { QStringLiteral("en"), QStringLiteral("Shed") } };
    shed.subparts = { { QStringLiteral("wall.1"), QPointF(0, 0), 0.0 },
                      { QStringLiteral("door.1"), QPointF(2, 0), 90.0 } };
    EXPECT_EQ(parts::setVersion(shed), QStringLiteral("e37a455ec528f2a9"));
    EXPECT_EQ(parts::nestedSetVersion(QStringLiteral("1572ba9befec980c"),
                                      { { QStringLiteral("Shed"), parts::setVersion(shed) } }),
              QStringLiteral("b24401c61c9daf89"));
    // The inner set loses its door: the outer one changes too.
    shed.subparts.removeLast();
    EXPECT_EQ(parts::nestedSetVersion(QStringLiteral("1572ba9befec980c"),
                                      { { QStringLiteral("SHED"), parts::setVersion(shed) } }),
              QStringLiteral("d281492859861154"));
    // No custom set inside: its own version.
    EXPECT_EQ(parts::nestedSetVersion(QStringLiteral("1572ba9befec980c"), {}), QStringLiteral("1572ba9befec980c"));
}
