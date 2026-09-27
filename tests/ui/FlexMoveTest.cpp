// Flex move against vanilla BlueBrick's own (fixtures/bluebrick-oracle/
// flex-*.bbm, made with scripts/bluebrick-oracle/bbflex.cs from
// flex-in.bbm: a straight, a PFS flex track end, eight middles, an end).

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "edit/Connectivity.h"
#include "edit/FlexMove.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QHash>

#include <cmath>
#include <string>
#include <vector>

using namespace bld;

namespace {

QString oracle(const QString& name) {
    return QStringLiteral(BLD_SOURCE_DIR "/fixtures/bluebrick-oracle/") + name;
}

core::LayerBrick& brickLayer(core::Map& map) {
    for (auto& layer : map.layers())
        if (layer->kind() == core::LayerKind::Brick) return static_cast<core::LayerBrick&>(*layer);
    throw std::runtime_error("no brick layer");
}

class FlexMoveTest : public ::testing::Test {
protected:
    void SetUp() override {
        const QString root = QStringLiteral(BLD_SOURCE_DIR "/parts/BlueBrickParts/parts");
        if (!QDir(root).exists()) GTEST_SKIP() << "BlueBrickParts submodule missing";
        lib_.addSearchPath(root);
        lib_.scan();
    }

    // Flex-moves `grabbed` from `grab` through `targets` (no snapping) and
    // compares every brick with vanilla's result.
    void expectLikeVanilla(const char* expected, const QString& grabbed, QPointF grab, std::vector<QPointF> targets) {
        SCOPED_TRACE(expected);
        auto map = saveload::readBbm(oracle(QStringLiteral("flex-in.bbm"))).map;
        ASSERT_TRUE(map);
        edit::rebuildConnectivity(*map, lib_);
        auto& layer = brickLayer(*map);
        QSet<QString> all;
        for (const auto& b : layer.bricks) all.insert(b.guid);
        auto flex = edit::FlexMove::start(layer, all, grabbed, grab, lib_);
        ASSERT_TRUE(flex);
        for (QPointF t : targets) flex->moveTo(t, 0.0, false);

        auto vanilla = saveload::readBbm(oracle(QLatin1String(expected))).map;
        ASSERT_TRUE(vanilla);
        QHash<QString, const core::Brick*> theirs;
        for (const auto& b : brickLayer(*vanilla).bricks) theirs.insert(b.guid, &b);
        int moved = 0;
        for (const auto& b : layer.bricks) {
            const core::Brick* v = theirs.value(b.guid);
            ASSERT_TRUE(v) << b.guid.toStdString();
            const std::string what = b.guid.toStdString() + " " + b.partNumber.toStdString();
            EXPECT_NEAR(b.displayArea.center().x(), v->displayArea.center().x(), 0.02) << what;
            EXPECT_NEAR(b.displayArea.center().y(), v->displayArea.center().y(), 0.02) << what;
            EXPECT_NEAR(b.displayArea.width(), v->displayArea.width(), 0.02) << what;
            EXPECT_NEAR(std::remainder(b.orientation - v->orientation, 360.0), 0.0, 0.05) << what;
            if (std::abs(v->orientation) > 0.01f && std::abs(v->orientation - 180.0f) > 0.01f) ++moved;
        }
        EXPECT_GT(moved, 0);
    }

    parts::PartsLibrary lib_;
};

}  // namespace

TEST_F(FlexMoveTest, DragTheFreeEndInOneGo) {
    expectLikeVanilla("flex-a.bbm", QStringLiteral("6887642994309303552"), { 68, 40 }, { { 62, 30 } });
}

TEST_F(FlexMoveTest, DragTheFreeEndAlongAPath) {
    expectLikeVanilla("flex-b.bbm", QStringLiteral("6887642994309303552"), { 68, 40 },
                      { { 67, 38 }, { 65, 34 }, { 62, 30 }, { 58, 28 } });
}

TEST_F(FlexMoveTest, GrabAMiddlePiece) {
    expectLikeVanilla("flex-c.bbm", QStringLiteral("5048093428964172125"), { 61.25, 40 }, { { 61, 35 } });
}

TEST_F(FlexMoveTest, RigidTrackIsNotFlexible) {
    auto map = saveload::readBbm(oracle(QStringLiteral("flex-in.bbm"))).map;
    ASSERT_TRUE(map);
    edit::rebuildConnectivity(*map, lib_);
    auto& layer = brickLayer(*map);
    // Only the straight selected: no hinge in the selection.
    const QString straight = QStringLiteral("186617953428535140");
    EXPECT_FALSE(edit::FlexMove::start(layer, { straight }, straight, { 40, 40 }, lib_));
}

TEST_F(FlexMoveTest, RestorePutsTheChainBack) {
    auto map = saveload::readBbm(oracle(QStringLiteral("flex-in.bbm"))).map;
    ASSERT_TRUE(map);
    edit::rebuildConnectivity(*map, lib_);
    auto& layer = brickLayer(*map);
    const std::vector<core::Brick> before = layer.bricks;
    QSet<QString> all;
    for (const auto& b : layer.bricks) all.insert(b.guid);
    auto flex = edit::FlexMove::start(layer, all, QStringLiteral("6887642994309303552"), { 68, 40 }, lib_);
    ASSERT_TRUE(flex);
    flex->moveTo({ 62, 30 }, 0.0);
    flex->restore();
    for (size_t i = 0; i < before.size(); ++i) {
        EXPECT_EQ(layer.bricks[i].displayArea, before[i].displayArea);
        EXPECT_EQ(layer.bricks[i].orientation, before[i].orientation);
    }
}
