// BlueBrick's brick geometry (parts/BrickPlacement.h): displayArea sizes
// as vanilla recomputes them on load, rotation around the sprite centre,
// and set subparts placed so their connections meet.

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "parts/BrickPlacement.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"

#include <gtest/gtest.h>

#include <QDir>

#include <cmath>
#include <string>

using namespace bld;
namespace placement = bld::parts::placement;

namespace {

class BrickPlacementTest : public ::testing::Test {
protected:
    void SetUp() override {
        const QString root = QStringLiteral(BLD_SOURCE_DIR "/parts/BlueBrickParts/parts");
        if (!QDir(root).exists()) GTEST_SKIP() << "BlueBrickParts submodule missing";
        lib_.addSearchPath(root);
        lib_.scan();
    }
    parts::PartsLibrary lib_;
};

}  // namespace

TEST_F(BrickPlacementTest, FootprintMatchesVanillaSizes) {
    // Vanilla recomputes every brick's displayArea size from its part and
    // orientation when it saves; ours must agree, or vanilla shifts bricks
    // we saved (it keeps the stored corner and resizes).
    int checked = 0, hulls = 0;
    for (const char* name : { "fourdbrix.bbm", "tight-corner.from-ldr.bbm", "tight-corner.from-tdl.bbm" }) {
        SCOPED_TRACE(name);
        const auto map = saveload::readBbm(QStringLiteral(BLD_SOURCE_DIR "/fixtures/bluebrick-oracle/") + QLatin1String(name)).map;
        ASSERT_TRUE(map);
        for (const auto& layer : map->layers()) {
            if (layer->kind() != core::LayerKind::Brick) continue;
            for (const auto& b : static_cast<const core::LayerBrick&>(*layer).bricks) {
                const auto fp = lib_.footprint(b.partNumber, b.orientation);
                if (!fp) continue;  // only in vanilla's installed library
                ++checked;
                if (!lib_.metadata(b.partNumber)->xmlHullPx.isEmpty()) ++hulls;
                EXPECT_NEAR(fp->size.width(), b.displayArea.width(), 0.01) << b.partNumber.toStdString() << " @" << b.orientation;
                EXPECT_NEAR(fp->size.height(), b.displayArea.height(), 0.01) << b.partNumber.toStdString() << " @" << b.orientation;
            }
        }
    }
    EXPECT_GT(checked, 300);
    EXPECT_GT(hulls, 0);
}

TEST_F(BrickPlacementTest, RotatingKeepsTheSpriteCentre) {
    // A 9V switch (left) has an XML hull, so its sprite sits off its displayArea centre.
    core::Brick b;
    b.partNumber = QStringLiteral("2861.8");
    ASSERT_TRUE(lib_.metadata(b.partNumber));
    ASSERT_FALSE(lib_.metadata(b.partNumber)->xmlHullPx.isEmpty());
    placement::placeByImageCentre(b, QPointF(100, 50), lib_);
    for (float o : { 37.0f, 90.0f, 215.5f, -12.0f }) {
        placement::rotateAroundImageCentre(b, o, lib_);
        const QPointF c = placement::imageCentre(b, lib_);
        EXPECT_NEAR(c.x(), 100.0, 1e-4) << o;
        EXPECT_NEAR(c.y(), 50.0, 1e-4) << o;
        EXPECT_NEAR(b.displayArea.width(), lib_.footprint(b.partNumber, o)->size.width(), 1e-6) << o;
    }
    EXPECT_GT(QLineF(b.displayArea.center(), placement::imageCentre(b, lib_)).length(), 0.1);
}

TEST_F(BrickPlacementTest, SetSubpartsMeetAtTheirConnections) {
    // Place every set in the library as BlueBrick does and look for
    // connections that almost meet: facing each other, same type, a
    // fraction of a stud apart. Misplaced subparts show up as those.
    int sets = 0, nearMisses = 0;
    std::string detail;
    for (const QString& key : lib_.keys()) {
        const auto meta = lib_.metadata(key);
        if (!meta || meta->kind != parts::PartKind::Group) continue;
        struct Conn { QString type; QPointF pos; double angle; int part; };
        std::vector<Conn> conns;
        int part = 0;
        for (const auto& sp : meta->subparts) {
            const auto sub = lib_.metadata(sp.subKey);
            if (!sub || sub->kind != parts::PartKind::Leaf) continue;
            core::Brick b;
            b.partNumber = sp.subKey;
            b.orientation = static_cast<float>(sp.angleDegrees);
            placement::placeByAreaCentre(b, sp.position, lib_);
            for (int i = 0; i < sub->connections.size(); ++i) {
                const auto& c = sub->connections[i];
                if (c.type.isEmpty()) continue;
                conns.push_back({ c.type, placement::connectionWorld(b, i, lib_), c.angleDegrees + b.orientation, part });
            }
            ++part;
        }
        ++sets;
        for (size_t i = 0; i < conns.size(); ++i) {
            for (size_t j = i + 1; j < conns.size(); ++j) {
                const Conn& a = conns[i];
                const Conn& c = conns[j];
                if (a.part == c.part || a.type != c.type) continue;
                const double d = QLineF(a.pos, c.pos).length();
                const double facing = std::abs(std::remainder(a.angle - c.angle - 180.0, 360.0));
                if (d > 0.1 && d < 1.5 && facing < 5.0) {
                    ++nearMisses;
                    if (detail.size() < 2000)
                        detail += "\n  " + key.toStdString() + ": " + std::to_string(d) + " studs apart";
                }
            }
        }
    }
    EXPECT_GT(sets, 20);
    EXPECT_EQ(nearMisses, 0) << detail;
}

TEST_F(BrickPlacementTest, StaleAreasAreResizedAroundTheSprite) {
    // Maps BlueBrick saved already match and stay untouched.
    for (const char* name : { "fourdbrix.bbm", "tight-corner.from-tdl.bbm" }) {
        auto map = saveload::readBbm(QStringLiteral(BLD_SOURCE_DIR "/fixtures/bluebrick-oracle/") + QLatin1String(name)).map;
        ASSERT_TRUE(map);
        EXPECT_EQ(placement::fixStaleAreas(*map, lib_), 0) << name;
    }
    // A straight turned by an earlier version kept its 17x8 box: it
    // becomes 8x17 around the same sprite centre.
    std::vector<core::Brick> bricks(1);
    core::Brick& b = bricks.front();
    b.partNumber = QStringLiteral("2865.8");
    b.orientation = 90.0f;
    b.displayArea = QRectF(10, 20, 17, 8);
    EXPECT_EQ(placement::fixStaleAreas(bricks, lib_), 1);
    EXPECT_NEAR(b.displayArea.width(), 8.0, 0.01);
    EXPECT_NEAR(b.displayArea.height(), 17.0, 0.01);
    EXPECT_NEAR(b.displayArea.center().x(), 18.5, 1e-6);
    EXPECT_NEAR(b.displayArea.center().y(), 24.0, 1e-6);
}
