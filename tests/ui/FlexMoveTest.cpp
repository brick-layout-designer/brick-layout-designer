// Flex move against vanilla BlueBrick's own (fixtures/bluebrick-oracle/
// flex-*.bbm, made with scripts/bluebrick-oracle/bbflex.cs from
// flex-in.bbm: a straight, a PFS flex track end, eight middles, an end).

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "edit/Connectivity.h"
#include "edit/FlexMove.h"
#include "edit/Sets.h"
#include "core/Ids.h"
#include "parts/BrickPlacement.h"
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

TEST_F(FlexMoveTest, JointsAtTheirHingeLimitAreReported) {
    auto map = saveload::readBbm(oracle(QStringLiteral("flex-in.bbm"))).map;
    ASSERT_TRUE(map);
    edit::rebuildConnectivity(*map, lib_);
    auto& layer = brickLayer(*map);
    QSet<QString> all;
    for (const auto& b : layer.bricks) all.insert(b.guid);
    auto flex = edit::FlexMove::start(layer, all, QStringLiteral("6887642994309303552"), { 68, 40 }, lib_);
    ASSERT_TRUE(flex);
    EXPECT_TRUE(flex->hingesAtLimit().empty()) << "straight, nothing bent yet";
    // Far round to the side: more than the hinges allow, so some stop.
    flex->moveTo({ 40, 10 }, 0.0, false);
    const auto limits = flex->hingesAtLimit();
    ASSERT_FALSE(limits.empty());
    QRectF bounds;
    for (const auto& b : layer.bricks)
        bounds = bounds.isNull() ? b.displayArea : bounds.united(b.displayArea);
    for (const QPointF& p : limits)
        EXPECT_TRUE(bounds.adjusted(-1, -1, 1, 1).contains(p)) << p.x() << "," << p.y();
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
    flex->moveTo({ 62, 30 }, 4.0);
    flex->restore();
    for (size_t i = 0; i < before.size(); ++i) {
        EXPECT_EQ(layer.bricks[i].displayArea, before[i].displayArea);
        EXPECT_EQ(layer.bricks[i].orientation, before[i].orientation);
    }
}

namespace {

// `sets` flex track sets end to end from x = 0, linked.
core::Map flexRow(parts::PartsLibrary& lib, int sets) {
    core::Map map;
    auto layer = std::make_unique<core::LayerBrick>();
    for (int k = 0; k < sets; ++k) {
        auto set = edit::expandSet(lib, QStringLiteral("flex.group"), QPointF(4.0 * k, 0.0));
        for (auto& b : set.bricks) layer->bricks.push_back(b);
        for (auto& g : set.groups) layer->groups.push_back(g);
    }
    map.layers().push_back(std::move(layer));
    edit::rebuildConnectivity(map, lib);
    return map;
}

QSet<QString> everyBrick(const core::LayerBrick& layer) {
    QSet<QString> all;
    for (const auto& b : layer.bricks) all.insert(b.guid);
    return all;
}

}  // namespace

TEST_F(FlexMoveTest, LinksGoingRoundInACircleAwayFromTheGrabbedPartEnd) {
    // A stale one-way link from the last set back to the middle one: the
    // chain from the first female went round B, C, B, C... for ever, a
    // vector growing at its front until the app was killed (Aaron's crash).
    auto map = flexRow(lib_, 3);
    auto& layer = brickLayer(map);
    ASSERT_EQ(layer.bricks.size(), 6u);
    layer.bricks[5].connections[0].linkedToId = layer.bricks[2].connections[0].guid;
    auto flex = edit::FlexMove::start(layer, everyBrick(layer), layer.bricks[0].guid, { -1.9, 0.0 }, lib_);
    ASSERT_TRUE(flex);
    EXPECT_LE(flex->initialState().size(), layer.bricks.size());
    flex->moveTo({ -1.5, -1.5 }, 0.0, false);
    flex->restore();
}

TEST_F(FlexMoveTest, AClosedLoopOfFlexTrackStartsOrSaysNo) {
    // Two sets joined at both ends (the second turned round onto the
    // first): every link leads back, never to a free end.
    auto map = flexRow(lib_, 2);
    auto& layer = brickLayer(map);
    layer.bricks[0].connections[0].linkedToId = layer.bricks[3].connections[0].guid;
    layer.bricks[3].connections[0].linkedToId = layer.bricks[0].connections[0].guid;
    for (const auto& b : layer.bricks) {
        auto flex = edit::FlexMove::start(layer, everyBrick(layer), b.guid, b.displayArea.center(), lib_);
        if (!flex) continue;
        EXPECT_LE(flex->initialState().size(), layer.bricks.size() + 1);
        flex->moveTo(b.displayArea.center() + QPointF(0.5, 0.5), 0.0, false);
        flex->restore();
    }
}

TEST_F(FlexMoveTest, RepeatedConnectionIdsAreMadeUniqueAndLinkBothWays) {
    // Parts pasted (or a module inserted) twice used to keep their
    // connection ids: links then named the wrong part.
    auto map = flexRow(lib_, 2);
    auto& layer = brickLayer(map);
    for (int i = 0; i < 2; ++i) layer.bricks[2 + i].connections = layer.bricks[i].connections;
    edit::rebuildConnectivity(map, lib_);
    QHash<QString, std::pair<int, int>> owner;
    for (int b = 0; b < static_cast<int>(layer.bricks.size()); ++b)
        for (int c = 0; c < static_cast<int>(layer.bricks[b].connections.size()); ++c) {
            const QString& id = layer.bricks[b].connections[c].guid;
            EXPECT_FALSE(owner.contains(id)) << "repeated id " << id.toStdString();
            owner.insert(id, { b, c });
        }
    int links = 0;
    for (const auto& b : layer.bricks)
        for (const auto& c : b.connections) {
            if (c.linkedToId.isEmpty()) continue;
            ++links;
            const auto [ob, oc] = owner.value(c.linkedToId, { -1, -1 });
            ASSERT_GE(ob, 0);
            EXPECT_EQ(layer.bricks[ob].connections[oc].linkedToId, c.guid) << "both ways";
        }
    EXPECT_EQ(links, 6) << "two hinges and the joint between the sets";
}

TEST_F(FlexMoveTest, TwoHalvesJoinedAtBothEndsEnteredByAStaleLink) {
    // The links in Aaron's crash (from the core dump): a female half turned
    // round on a male one, joined at both ends to each other (a set
    // duplicated and turned onto another), and a stale one-way link into
    // them (a duplicate that kept its connection ids). The chain from the
    // grabbed half went X, Y, X, Y... 124758 links before the app was killed.
    auto map = flexRow(lib_, 1);
    auto& layer = brickLayer(map);
    core::Brick male = layer.bricks[1];
    male.guid = core::newBbmId();
    male.myGroupId.clear();
    parts::placement::placeByAreaCentre(male, QPointF(20, 0), lib_);
    core::Brick female = layer.bricks[0];
    female.guid = core::newBbmId();
    female.myGroupId.clear();
    female.orientation = 180.0f;
    parts::placement::placeByConnection(female, 1, parts::placement::connectionWorld(male, 1, lib_), lib_);
    for (auto* b : { &male, &female })
        for (auto& c : b->connections) { c.guid = core::newBbmId(); c.linkedToId.clear(); }
    layer.bricks.push_back(male);
    layer.bricks.push_back(female);
    edit::rebuildConnectivity(map, lib_);
    auto& X = layer.bricks[2];
    auto& Y = layer.bricks[3];
    ASSERT_EQ(X.connections[0].linkedToId, Y.connections[0].guid) << "joined at both ends";
    ASSERT_EQ(X.connections[1].linkedToId, Y.connections[1].guid);
    // The stale link: the grabbed set's free rail end names the pair's rail.
    layer.bricks[1].connections[0].linkedToId = X.connections[0].guid;
    auto flex = edit::FlexMove::start(layer, everyBrick(layer), layer.bricks[0].guid, { -1.9, 0.0 }, lib_);
    if (flex) {
        EXPECT_LE(flex->initialState().size(), layer.bricks.size());
        flex->moveTo({ -1.5, -1.5 }, 0.0, false);
        flex->restore();
    }
}
