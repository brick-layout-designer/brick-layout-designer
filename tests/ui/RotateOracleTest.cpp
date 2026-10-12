// Rotate against vanilla BlueBrick's own (fixtures/bluebrick-oracle/rot-*.bbm,
// made with scripts/bluebrick-oracle/bbrotate.cs from flex-in.bbm):
//   rot-all1     every part, one step (RotateBrick +1): about their box centre
//   rot-all2     the same twice in a row
//   rot-one-ccw  the straight alone (+1): joined to the flex end, it steps
//   rot-one-cw   round that join to its other connection, so it flips (-1)
//   rot-45x1/2   every part by a 45-degree step, once and twice in a row: the
//                box grows, and the second turn is about the new box's centre
//                (RotateItems' sLastCenter is cleared when redo re-selects)
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "edit/Connectivity.h"
#include "edit/RotateTurn.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"
#include "ui/MapView.h"
#include "ui/MapViewInternal.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QTest>
#include <QUndoStack>

using namespace bld;

namespace {

QString oracle(const QString& name) { return QStringLiteral(BLD_SOURCE_DIR "/fixtures/bluebrick-oracle/") + name; }

const core::LayerBrick& brickLayer(const core::Map& map) {
    for (const auto& layer : map.layers())
        if (layer->kind() == core::LayerKind::Brick) return static_cast<const core::LayerBrick&>(*layer);
    throw std::runtime_error("no brick layer");
}

class RotateOracle : public ::testing::Test {
protected:
    void SetUp() override {
        const QString root = QStringLiteral(BLD_SOURCE_DIR "/parts/BlueBrickParts/parts");
        if (!QDir(root).exists()) GTEST_SKIP() << "BlueBrickParts submodule missing";
        lib_.addSearchPath(root);
        lib_.scan();
        auto map = saveload::readBbm(oracle(QStringLiteral("flex-in.bbm"))).map;
        ASSERT_TRUE(map);
        view_ = std::make_unique<ui::MapView>(lib_);
        view_->resize(900, 700);
        view_->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(view_.get()));
        view_->loadMap(std::move(map));
    }
    void pick(const QString& only = {}) {
        for (QGraphicsItem* it : view_->scene()->items())
            it->setSelected(ui::detail::isBrickItem(it)
                            && (only.isEmpty() || it->data(ui::detail::kBrickDataGuid).toString() == only));
    }
    void expectLikeVanilla(const char* expected) {
        SCOPED_TRACE(expected);
        auto vanilla = saveload::readBbm(oracle(QLatin1String(expected))).map;
        ASSERT_TRUE(vanilla);
        const auto& want = brickLayer(*vanilla).bricks;
        const auto& got = brickLayer(*view_->currentMap()).bricks;
        ASSERT_EQ(got.size(), want.size());
        for (std::size_t i = 0; i < got.size(); ++i) {
            SCOPED_TRACE(got[i].guid.toStdString());
            EXPECT_NEAR(std::remainder(got[i].orientation - want[i].orientation, 360.0), 0, 1e-3);
            EXPECT_NEAR(got[i].displayArea.x(), want[i].displayArea.x(), 2e-3);
            EXPECT_NEAR(got[i].displayArea.y(), want[i].displayArea.y(), 2e-3);
            EXPECT_NEAR(got[i].displayArea.width(), want[i].displayArea.width(), 2e-3);
            EXPECT_NEAR(got[i].displayArea.height(), want[i].displayArea.height(), 2e-3);
        }
    }
    void pressR(Qt::KeyboardModifiers mods = {}) { QTest::keyClick(view_.get(), Qt::Key_R, mods); }

    parts::PartsLibrary lib_;
    std::unique_ptr<ui::MapView> view_;
};

}  // namespace

TEST_F(RotateOracle, SeveralPartsTurnAboutTheirBoxCentre) {
    pick();
    pressR();
    expectLikeVanilla("rot-all1.bbm");
}

TEST_F(RotateOracle, TurningAgainTurnsAboutTheNewBox) {
    pick();
    pressR();
    pressR();
    expectLikeVanilla("rot-all2.bbm");
}

TEST_F(RotateOracle, A45DegreeStepTurnsAboutTheBoxCentre) {
    view_->setRotationStepDegrees(45);
    pick();
    pressR();
    expectLikeVanilla("rot-45x1.bbm");
}

TEST_F(RotateOracle, A45DegreeStepTwiceTurnsAboutTheNewBox) {
    view_->setRotationStepDegrees(45);
    pick();
    pressR();
    pressR();
    expectLikeVanilla("rot-45x2.bbm");
}

TEST_F(RotateOracle, AJoinedPartStepsRoundItsJoin) {
    pick(QStringLiteral("186617953428535140"));
    pressR();
    expectLikeVanilla("rot-one-ccw.bbm");
}

TEST_F(RotateOracle, AJoinedPartStepsBackRoundItsJoin) {
    pick(QStringLiteral("186617953428535140"));
    pressR(Qt::ShiftModifier);
    expectLikeVanilla("rot-one-cw.bbm");
}

TEST(RotateTurn, NothingJoinedOutsideIsAPlainTurn) {
    const QString root = QStringLiteral(BLD_SOURCE_DIR "/parts/BlueBrickParts/parts");
    if (!QDir(root).exists()) GTEST_SKIP() << "BlueBrickParts submodule missing";
    parts::PartsLibrary lib;
    lib.addSearchPath(root);
    lib.scan();
    auto map = saveload::readBbm(oracle(QStringLiteral("flex-in.bbm"))).map;
    ASSERT_TRUE(map);
    edit::rebuildConnectivity(*map, lib);
    const auto& bricks = brickLayer(*map).bricks;
    std::vector<const core::Brick*> all;
    for (const auto& b : bricks) all.push_back(&b);
    EXPECT_FALSE(edit::connectedTurn(*map, all, 1, lib));
    const auto turn = edit::connectedTurn(*map, { &bricks.front() }, 1, lib);
    ASSERT_TRUE(turn);
    EXPECT_NEAR(std::remainder(turn->degrees, 360.0), 180, 1e-6);
    EXPECT_EQ(turn->brickGuid, bricks.front().guid);
    EXPECT_EQ(turn->connection, 0);
}
