// MapView scene-rebuild behaviour: every undo-stack change rebuilds the
// QGraphicsScene from core::Map, and the rebuild must keep the user's
// selection (by layer + guid) instead of dropping it with the old items.

#include "ui/MapView.h"
#include "ui/BudgetSession.h"
#include "ui/MapViewInternal.h"
#include "edit/EditCommands.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"
#include "core/LayerBrick.h"
#include "core/LayerGrid.h"
#include "core/Ids.h"
#include "core/Map.h"
#include "edit/Connectivity.h"
#include "edit/Sets.h"
#include "parts/BrickPlacement.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QHash>
#include <QMouseEvent>
#include <QSettings>
#include <QUndoStack>

using namespace bld;

namespace {

QString fixture() {
    return QString::fromUtf8(BLD_BBM_CORPUS_DIR) + QStringLiteral("/tight-corner.bbm");
}

QList<QGraphicsItem*> brickItems(QGraphicsScene& scene) {
    QList<QGraphicsItem*> out;
    for (QGraphicsItem* it : scene.items()) {
        if (ui::detail::isBrickItem(it)) out.append(it);
    }
    return out;
}

QStringList selectedBrickGuids(QGraphicsScene& scene) {
    QStringList out;
    for (QGraphicsItem* it : scene.selectedItems()) {
        if (ui::detail::isBrickItem(it)) out.append(it->data(ui::detail::kBrickDataGuid).toString());
    }
    out.sort();
    return out;
}

class MapViewTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!QFile::exists(fixture())) GTEST_SKIP() << "tight-corner.bbm missing";
        const QString partsRoot = QString::fromUtf8(BLD_PARTS_LIBRARY_ROOT);
        if (QDir(partsRoot).exists()) {
            parts_.addSearchPath(partsRoot);
            parts_.scan();
        }
        auto loaded = saveload::readBbm(fixture());
        ASSERT_TRUE(loaded.ok()) << loaded.error.toStdString();
        view_ = std::make_unique<ui::MapView>(parts_);
        view_->loadMap(std::move(loaded.map));
        ASSERT_FALSE(brickItems(*view_->scene()).isEmpty());
    }

    // Select the first brick item and return its (layer, guid).
    edit::BrickRef selectFirstBrick() {
        QGraphicsItem* it = brickItems(*view_->scene()).first();
        it->setSelected(true);
        return { it->data(ui::detail::kBrickDataLayerIndex).toInt(),
                 it->data(ui::detail::kBrickDataGuid).toString() };
    }

    parts::PartsLibrary parts_;
    std::unique_ptr<ui::MapView> view_;
};

TEST_F(MapViewTest, RebuildKeepsSelectionAndItemCount) {
    const auto ref = selectFirstBrick();
    const auto before = view_->scene()->items().size();

    view_->rebuildScene();

    EXPECT_EQ(view_->scene()->items().size(), before);
    EXPECT_EQ(selectedBrickGuids(*view_->scene()), QStringList{ ref.guid });
}

TEST_F(MapViewTest, UndoRedoKeepSelection) {
    const auto ref = selectFirstBrick();

    QPointF topLeft;
    for (const auto& layer : view_->currentMap()->layers()) {
        if (layer->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*layer).bricks) {
            if (b.guid == ref.guid) topLeft = b.displayArea.topLeft();
        }
    }
    std::vector<edit::MoveBricksCommand::Entry> entries{
        { ref, topLeft, topLeft + QPointF(8.0, 0.0) } };
    view_->undoStack()->push(new edit::MoveBricksCommand(*view_->currentMap(), std::move(entries)));
    EXPECT_EQ(selectedBrickGuids(*view_->scene()), QStringList{ ref.guid });

    view_->undoStack()->undo();
    EXPECT_EQ(selectedBrickGuids(*view_->scene()), QStringList{ ref.guid });

    view_->undoStack()->redo();
    EXPECT_EQ(selectedBrickGuids(*view_->scene()), QStringList{ ref.guid });
}

}  // namespace

namespace {

void mouse(QWidget* w, QEvent::Type type, QPoint pos, Qt::MouseButtons buttons) {
    const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent ev(type, QPointF(pos), w->mapToGlobal(QPointF(pos)), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(w, &ev);
}

}  // namespace

TEST_F(MapViewTest, DoubleClickDragBendsFlexTrackInOneUndoStep) {
    // The flex chain is found through the parts' connection points, so the
    // track parts must be in the library (an uninitialised submodule leaves
    // an empty folder).
    for (const char* part : { "2865.8", "88492.8", "88493.8" }) {
        if (!parts_.metadata(QString::fromLatin1(part)))
            GTEST_SKIP() << "the BlueBrickParts library isn't checked out (run git submodule update --init); missing " << part;
    }
    auto loaded = saveload::readBbm(QStringLiteral(BLD_SOURCE_DIR "/fixtures/bluebrick-oracle/flex-in.bbm"));
    ASSERT_TRUE(loaded.ok());
    view_->loadMap(std::move(loaded.map));
    view_->resize(800, 600);
    const double px = 8.0;  // SceneBuilder::kPixelsPerStud
    view_->centerOn(QPointF(55, 40) * px);
    for (QGraphicsItem* it : brickItems(*view_->scene())) it->setSelected(true);
    const auto bricks = [&]() -> const std::vector<core::Brick>& {
        for (const auto& l : view_->currentMap()->layers())
            if (l->kind() == core::LayerKind::Brick) return static_cast<const core::LayerBrick&>(*l).bricks;
        throw std::runtime_error("no brick layer");
    };
    const std::vector<core::Brick> before = bricks();

    // Grab the far flex-track end (its free connection is at x = 68).
    QWidget* vp = view_->viewport();
    const QPoint grab = view_->mapFromScene(QPointF(67.5, 40) * px);
    mouse(vp, QEvent::MouseButtonPress, grab, Qt::LeftButton);
    mouse(vp, QEvent::MouseButtonRelease, grab, Qt::NoButton);
    mouse(vp, QEvent::MouseButtonDblClick, grab, Qt::LeftButton);
    ASSERT_EQ(selectedBrickGuids(*view_->scene()).size(), 10) << "the chain after the straight";
    for (QPointF p : { QPointF(66, 37), QPointF(63, 32) })
        mouse(vp, QEvent::MouseMove, view_->mapFromScene(p * px), Qt::LeftButton);
    mouse(vp, QEvent::MouseButtonRelease, view_->mapFromScene(QPointF(63, 32) * px), Qt::NoButton);

    const auto& after = bricks();
    int moved = 0;
    for (size_t i = 0; i < after.size(); ++i)
        if (after[i].displayArea != before[i].displayArea) ++moved;
    EXPECT_GE(moved, 8);
    EXPECT_EQ(after.front().displayArea, before.front().displayArea) << "the straight stays";
    EXPECT_EQ(view_->undoStack()->count(), 1);
    EXPECT_EQ(view_->undoStack()->undoText(), QStringLiteral("Flex move"));

    view_->undoStack()->undo();
    for (size_t i = 0; i < before.size(); ++i) {
        EXPECT_EQ(bricks()[i].displayArea, before[i].displayArea);
        EXPECT_EQ(bricks()[i].orientation, before[i].orientation);
    }
}

TEST_F(MapViewTest, DoubleClickDragBendsAPlacedFlexTrackSet) {
    // A flex track from the library is a set (flex.group: a female and a
    // male half joined by a hinge), placed as one BlueBrick group, never a
    // module. A click picks it whole; a double-click-drag bends it.
    if (!parts_.metadata(QStringLiteral("flex.group")))
        GTEST_SKIP() << "the BlueBrickParts library isn't checked out (run git submodule update --init)";
    auto map = std::make_unique<core::Map>();
    auto layer = std::make_unique<core::LayerBrick>();
    layer->guid = core::newBbmId();
    map->layers().push_back(std::move(layer));
    view_->loadMap(std::move(map));
    view_->resize(800, 600);
    const double px = 8.0;  // SceneBuilder::kPixelsPerStud
    view_->centerOn(QPointF(0, 0));
    view_->addPartAtScenePos(QStringLiteral("flex.group"), QPointF(0, 0));
    EXPECT_TRUE(view_->currentMap()->sidecar.modules.empty()) << "a set is not a module";
    const auto layerOf = [&]() -> const core::LayerBrick& {
        for (const auto& l : view_->currentMap()->layers())
            if (l->kind() == core::LayerKind::Brick) return static_cast<const core::LayerBrick&>(*l);
        throw std::runtime_error("no brick layer");
    };
    const auto bricks = [&]() -> const std::vector<core::Brick>& { return layerOf().bricks; };
    ASSERT_EQ(bricks().size(), 2u);
    ASSERT_EQ(layerOf().groups.size(), 1u);
    EXPECT_EQ(layerOf().groups.front().partNumber, QStringLiteral("FLEX.GROUP"));
    for (const auto& b : bricks()) EXPECT_EQ(b.myGroupId, layerOf().groups.front().guid);
    // Its own joint is linked.
    EXPECT_EQ(bricks()[0].connections[1].linkedToId, bricks()[1].connections[1].guid);
    const std::vector<core::Brick> before = bricks();
    const int undoBefore = view_->undoStack()->count();

    // Grab the male half near its free end (x = 0.8 + 1.25 studs).
    QWidget* vp = view_->viewport();
    const QPoint grab = view_->mapFromScene(QPointF(1.8, 0) * px);
    mouse(vp, QEvent::MouseButtonPress, grab, Qt::LeftButton);
    mouse(vp, QEvent::MouseButtonRelease, grab, Qt::NoButton);
    EXPECT_EQ(selectedBrickGuids(*view_->scene()).size(), 2) << "a click picks the whole set";
    mouse(vp, QEvent::MouseButtonDblClick, grab, Qt::LeftButton);
    for (QPointF p : { QPointF(1.8, -0.2), QPointF(1.7, -0.6) })
        mouse(vp, QEvent::MouseMove, view_->mapFromScene(p * px), Qt::LeftButton);
    mouse(vp, QEvent::MouseButtonRelease, view_->mapFromScene(QPointF(1.7, -0.6) * px), Qt::NoButton);

    EXPECT_EQ(view_->undoStack()->count(), undoBefore + 1);
    EXPECT_EQ(view_->undoStack()->undoText(), QStringLiteral("Flex move"));
    float turned = 0.0f;
    for (size_t i = 0; i < bricks().size(); ++i)
        turned = std::max(turned, std::abs(bricks()[i].orientation - before[i].orientation));
    EXPECT_GT(turned, 1.0f);
    EXPECT_LE(turned, 10.01f) << "the flex pivot bends 10 degrees at most";

}

TEST_F(MapViewTest, ADuplicatedSetStaysASet) {
    if (!parts_.metadata(QStringLiteral("flex.group")))
        GTEST_SKIP() << "the BlueBrickParts library isn't checked out (run git submodule update --init)";
    auto map = std::make_unique<core::Map>();
    auto layer = std::make_unique<core::LayerBrick>();
    layer->name = QStringLiteral("Tracks");
    layer->guid = core::newBbmId();
    map->layers().push_back(std::move(layer));
    view_->loadMap(std::move(map));
    view_->addPartAtScenePos(QStringLiteral("flex.group"), QPointF(0, 0));
    ASSERT_EQ(selectedBrickGuids(*view_->scene()).size(), 2) << "the placed set is selected";
    view_->duplicateSelection();
    const auto& L = static_cast<const core::LayerBrick&>(*view_->currentMap()->layers().front());
    ASSERT_EQ(L.bricks.size(), 4u);
    ASSERT_EQ(L.groups.size(), 2u);
    EXPECT_NE(L.groups[0].guid, L.groups[1].guid);
    for (const auto& g : L.groups) EXPECT_EQ(g.partNumber, QStringLiteral("FLEX.GROUP"));
    EXPECT_EQ(L.bricks[2].myGroupId, L.groups[1].guid);
    EXPECT_EQ(L.bricks[3].myGroupId, L.groups[1].guid);
    view_->undoStack()->undo();
    EXPECT_EQ(L.groups.size(), 1u);
}

TEST_F(MapViewTest, APartAddedBesideASelectedSetJoinsItsPreferredEnd) {
    // BlueBrick's GroupConnectionPreferenceList: flex.group's next part goes
    // on rail end 0, then 2 (the female's, then the male's).
    if (!parts_.metadata(QStringLiteral("flex.group")))
        GTEST_SKIP() << "the BlueBrickParts library isn't checked out (run git submodule update --init)";
    EXPECT_EQ(parts_.metadata(QStringLiteral("flex.group"))->groupNextPreferred,
              (QHash<int, int>{ { 0, 2 }, { 2, 0 } }));
    auto map = std::make_unique<core::Map>();
    auto layer = std::make_unique<core::LayerBrick>();
    layer->guid = core::newBbmId();
    map->layers().push_back(std::move(layer));
    view_->loadMap(std::move(map));
    const double px = 8.0; // SceneBuilder::kPixelsPerStud
    const auto& L = static_cast<const core::LayerBrick&>(*view_->currentMap()->layers().front());
    const auto selectSet = [&] {
        view_->scene()->clearSelection();
        for (QGraphicsItem* it : brickItems(*view_->scene())) {
            const QString g = it->data(ui::detail::kBrickDataGuid).toString();
            if (g == L.bricks[0].guid || g == L.bricks[1].guid) it->setSelected(true);
        }
    };
    view_->addPartAtScenePos(QStringLiteral("flex.group"), QPointF(0, 0));
    ASSERT_EQ(L.bricks.size(), 2u);
    // Far from the set: it goes on the set's end, not where it was put.
    selectSet();
    view_->addPartAtScenePos(QStringLiteral("88493.8"), QPointF(100, 100) * px);
    ASSERT_EQ(L.bricks.size(), 3u);
    EXPECT_FALSE(L.bricks[0].connections[0].linkedToId.isEmpty()) << "on the female's rail end";
    EXPECT_TRUE(L.bricks[1].connections[0].linkedToId.isEmpty());
    selectSet();
    view_->addPartAtScenePos(QStringLiteral("88492.8"), QPointF(100, 100) * px);
    ASSERT_EQ(L.bricks.size(), 4u);
    EXPECT_FALSE(L.bricks[1].connections[0].linkedToId.isEmpty()) << "then on the male's";
}

TEST_F(MapViewTest, DraggingASetFromThePartsListShowsWhereItWillSnap) {
    if (!parts_.metadata(QStringLiteral("flex.group")))
        GTEST_SKIP() << "the BlueBrickParts library isn't checked out (run git submodule update --init)";
    auto map = std::make_unique<core::Map>();
    auto layer = std::make_unique<core::LayerBrick>();
    layer->guid = core::newBbmId();
    map->layers().push_back(std::move(layer));
    view_->loadMap(std::move(map));
    view_->resize(800, 600);
    const double px = 8.0; // SceneBuilder::kPixelsPerStud
    view_->centerOn(QPointF(2, 0) * px);
    view_->addPartAtScenePos(QStringLiteral("flex.group"), QPointF(0, 0));
    view_->scene()->clearSelection();
    // A second flex track held just past the first's free end (2.05, 0):
    // its own free end (-1.95 from its origin) is a little way off.
    const QPoint at = view_->viewport()->mapToGlobal(view_->mapFromScene(QPointF(4.3, 0.2) * px));
    view_->touchPartDragTo(QStringLiteral("flex.group"), at);
    ASSERT_TRUE(view_->connectionSnapShown()) << "the ghost shows the snap";
    EXPECT_NEAR(view_->connectionSnapPoint().x(), 2.05 * px, 0.05 * px);
    EXPECT_NEAR(view_->connectionSnapPoint().y(), 0.0, 0.05 * px);
    // And the drop lands where the ghost showed.
    ASSERT_TRUE(view_->touchPartDropAt(QStringLiteral("flex.group"), at));
    const auto& L = static_cast<const core::LayerBrick&>(*view_->currentMap()->layers().front());
    ASSERT_EQ(L.bricks.size(), 4u);
    EXPECT_FALSE(L.bricks[1].connections[0].linkedToId.isEmpty()) << "joined to the first set";
}

TEST_F(MapViewTest, ABendHandleOnTheFreeEndBendsTheWholeRun) {
    if (!parts_.metadata(QStringLiteral("flex.group")))
        GTEST_SKIP() << "the BlueBrickParts library isn't checked out (run git submodule update --init)";
    auto map = std::make_unique<core::Map>();
    auto layer = std::make_unique<core::LayerBrick>();
    layer->guid = core::newBbmId();
    map->layers().push_back(std::move(layer));
    view_->loadMap(std::move(map));
    view_->resize(800, 600);
    const double px = 8.0;  // SceneBuilder::kPixelsPerStud
    view_->centerOn(QPointF(2, 0) * px);
    // Two flex track sets end to end: one run of four halves, two hinges.
    view_->addPartAtScenePos(QStringLiteral("flex.group"), QPointF(0, 0));
    view_->addPartAtScenePos(QStringLiteral("flex.group"), QPointF(4, 0) * px);
    const auto& L = static_cast<const core::LayerBrick&>(*view_->currentMap()->layers().front());
    ASSERT_EQ(L.bricks.size(), 4u);
    ASSERT_EQ(L.bricks[1].connections[0].linkedToId, L.bricks[2].connections[0].guid) << "the sets are joined";

    // Selecting the first set shows a handle on each free end of the run.
    view_->scene()->clearSelection();
    for (QGraphicsItem* it : brickItems(*view_->scene()))
        if (it->data(ui::detail::kBrickDataGuid).toString() == L.bricks[0].guid) it->setSelected(true);
    auto handles = view_->bendHandlePositions();
    ASSERT_EQ(handles.size(), 2u);
    std::sort(handles.begin(), handles.end(), [](QPointF a, QPointF b) { return a.x() < b.x(); });
    EXPECT_NEAR(handles[0].x(), -1.95, 0.01);
    EXPECT_NEAR(handles[1].x(), 6.05, 0.01);

    const std::vector<core::Brick> before = L.bricks;
    const int undoBefore = view_->undoStack()->count();
    QWidget* vp = view_->viewport();
    const QPoint grab = view_->mapFromScene(handles[1] * px);
    // A click without a drag changes nothing (and opens nothing).
    mouse(vp, QEvent::MouseButtonPress, grab, Qt::LeftButton);
    mouse(vp, QEvent::MouseButtonRelease, grab, Qt::NoButton);
    EXPECT_EQ(view_->undoStack()->count(), undoBefore);
    // A drag bends the run; the far end stays put.
    mouse(vp, QEvent::MouseButtonPress, grab, Qt::LeftButton);
    for (QPointF p : { QPointF(6, -0.5), QPointF(5.8, -1.5) })
        mouse(vp, QEvent::MouseMove, view_->mapFromScene(p * px), Qt::LeftButton);
    mouse(vp, QEvent::MouseButtonRelease, view_->mapFromScene(QPointF(5.8, -1.5) * px), Qt::NoButton);
    EXPECT_EQ(view_->undoStack()->count(), undoBefore + 1);
    EXPECT_EQ(view_->undoStack()->undoText(), QStringLiteral("Bend flex track"));
    EXPECT_EQ(L.bricks[0].displayArea, before[0].displayArea) << "the other end stays";
    EXPECT_NE(L.bricks[3].orientation, before[3].orientation);
    EXPECT_EQ(L.groups.size(), 2u) << "still two sets";
}

namespace {

// A brick layer holding one flex track set, selected, for the bend handle.
void oneSelectedFlexSet(ui::MapView& view, QGraphicsScene& scene) {
    auto map = std::make_unique<core::Map>();
    auto layer = std::make_unique<core::LayerBrick>();
    layer->guid = core::newBbmId();
    map->layers().push_back(std::move(layer));
    view.loadMap(std::move(map));
    view.resize(800, 600);
    view.addPartAtScenePos(QStringLiteral("flex.group"), QPointF(0, 0));
    for (QGraphicsItem* it : brickItems(scene)) it->setSelected(true);
}

}  // namespace

TEST_F(MapViewTest, ABendHandleKeepsItsScreenSizeAtEveryZoom) {
    if (!parts_.metadata(QStringLiteral("flex.group")))
        GTEST_SKIP() << "the BlueBrickParts library isn't checked out (run git submodule update --init)";
    oneSelectedFlexSet(*view_, *view_->scene());
    ASSERT_EQ(view_->bendHandlePositions().size(), 2u);
    const double px = 8.0;  // SceneBuilder::kPixelsPerStud
    // The left end's handle: grabbed from the side away from the other one.
    const auto ends = view_->bendHandlePositions();
    const int left = ends[0].x() < ends[1].x() ? 0 : 1;
    const QPointF handle = ends[left] * px;
    // Zoomed far out, as drawn, and far in: the same 9 px ring, grabbed
    // within 12 px, frame after frame.
    for (double zoom : { 0.25, 1.0, 6.0 }) {
        view_->setTransform(QTransform::fromScale(zoom, zoom));
        view_->centerOn(handle);
        QCoreApplication::processEvents();
        EXPECT_NEAR(view_->bendHandleRadiusScenePx() * zoom, 9.0, 1e-9) << "zoom " << zoom;
        EXPECT_NEAR(view_->bendHandleRadiusScenePx(true) * zoom, 12.0, 1e-9) << "zoom " << zoom;
        const QPoint at = view_->mapFromScene(handle);
        EXPECT_EQ(view_->bendHandleAt(at + QPoint(-10, 0)), left) << "zoom " << zoom;
        EXPECT_EQ(view_->bendHandleAt(at + QPoint(-16, 0)), -1) << "zoom " << zoom;
    }
}

TEST_F(MapViewTest, BendingTheEndOntoTrackAtALargeAngleLinksIt) {
    if (!parts_.metadata(QStringLiteral("flex.group")) || !parts_.metadata(QStringLiteral("2865.8")))
        GTEST_SKIP() << "the BlueBrickParts library isn't checked out (run git submodule update --init)";
    // A straight, six flex track sets, and a straight where the run ends
    // when each set bends 8 degrees: 48 degrees in all.
    const int sets = 6;
    const float bend = 8.0f;
    QPointF at(-1.95, 0.0);
    float turn = 0.0f;
    for (int k = 0; k < sets; ++k) {
        core::Brick female, male;
        female.partNumber = QStringLiteral("88492.8");
        male.partNumber = QStringLiteral("88493.8");
        female.orientation = turn;
        parts::placement::placeByConnection(female, 0, at, parts_);
        male.orientation = turn + bend;
        parts::placement::placeByConnection(male, 1, parts::placement::connectionWorld(female, 1, parts_), parts_);
        at = parts::placement::connectionWorld(male, 0, parts_);
        turn += bend;
    }
    core::Brick target;
    target.guid = core::newBbmId();
    target.partNumber = QStringLiteral("2865.8");
    target.orientation = turn;
    parts::placement::placeByConnection(target, 0, at, parts_);
    core::Brick start;
    start.guid = core::newBbmId();
    start.partNumber = QStringLiteral("2865.8");
    parts::placement::placeByConnection(start, 1, QPointF(-1.95, 0.0), parts_);

    auto map = std::make_unique<core::Map>();
    auto layer = std::make_unique<core::LayerBrick>();
    layer->guid = core::newBbmId();
    layer->bricks.push_back(start);
    for (int k = 0; k < sets; ++k) {
        auto set = edit::expandSet(parts_, QStringLiteral("flex.group"), QPointF(4.0 * k, 0.0));
        for (auto& b : set.bricks) layer->bricks.push_back(b);
        for (auto& g : set.groups) layer->groups.push_back(g);
    }
    layer->bricks.push_back(target);
    map->layers().push_back(std::move(layer));
    edit::rebuildConnectivity(*map, parts_);
    view_->loadMap(std::move(map));
    view_->resize(800, 600);
    const double px = 8.0;  // SceneBuilder::kPixelsPerStud
    view_->centerOn(QPointF(12, -8) * px);
    const auto& L = static_cast<const core::LayerBrick&>(*view_->currentMap()->layers().front());
    const core::Brick& last = L.bricks[2 * sets];  // the last male half
    ASSERT_TRUE(last.connections[0].linkedToId.isEmpty());
    for (QGraphicsItem* it : brickItems(*view_->scene()))
        if (it->data(ui::detail::kBrickDataGuid).toString() == L.bricks[1].guid) it->setSelected(true);
    ASSERT_EQ(view_->bendHandlePositions().size(), 1u) << "one free end: the other is joined to the straight";
    const QPointF handle = view_->bendHandlePositions().front();
    EXPECT_NEAR(handle.x(), 4.0 * sets - 1.95, 0.01);

    // Drag the handle over, with a wobbly hand, to just short of the target.
    QWidget* vp = view_->viewport();
    mouse(vp, QEvent::MouseButtonPress, view_->mapFromScene(handle * px), Qt::LeftButton);
    const QPointF near = at + QPointF(-0.6, 0.4);
    for (int i = 1; i <= 12; ++i) {
        const double f = i / 12.0;
        const QPointF wobble(0.25 * std::sin(i * 1.7), 0.25 * std::cos(i * 2.3));
        mouse(vp, QEvent::MouseMove, view_->mapFromScene((handle + (near - handle) * f + wobble * f) * px), Qt::LeftButton);
    }
    mouse(vp, QEvent::MouseButtonRelease, view_->mapFromScene(near * px), Qt::NoButton);

    EXPECT_EQ(view_->undoStack()->undoText(), QStringLiteral("Bend flex track"));
    EXPECT_EQ(last.connections[0].linkedToId, L.bricks.back().connections[0].guid) << "the end joined the straight";
    EXPECT_NEAR(last.orientation, turn, 2.0) << "facing the straight (the solver stops within about 2 degrees)";
    EXPECT_EQ(L.bricks[1].connections[0].linkedToId, L.bricks[0].connections[1].guid) << "the run still holds on to the first straight";
}

TEST_F(MapViewTest, BudgetLimitationRefusesPartsOverTheirLimit) {
    QSettings().setValue(QStringLiteral("general/warnBudgetLimitation"), false);  // no modal message
    ui::BudgetSession budget(parts_);
    view_->setBudget(&budget);
    budget.create();
    budget.setLimit(QStringLiteral("3001.1"), 1);
    const int before = view_->undoStack()->count();

    budget.setUseBudgetLimitation(false);
    view_->addPartAtScenePos(QStringLiteral("3001.1"), QPointF(0, 0));
    view_->addPartAtScenePos(QStringLiteral("3001.1"), QPointF(80, 0));
    EXPECT_EQ(view_->undoStack()->count(), before + 2) << "no limitation: both placed";

    budget.setUseBudgetLimitation(true);
    view_->addPartAtScenePos(QStringLiteral("3001.1"), QPointF(160, 0));
    EXPECT_EQ(view_->undoStack()->count(), before + 2) << "over the limit: refused";
    budget.setLimit(QStringLiteral("3001.1"), 3);
    view_->addPartAtScenePos(QStringLiteral("3001.1"), QPointF(160, 0));
    EXPECT_EQ(view_->undoStack()->count(), before + 3) << "raised limit: placed";

    budget.setUseBudgetLimitation(false);
    view_->setBudget(nullptr);
}

TEST_F(MapViewTest, DraggingEmptySpaceMovesTheGridOriginByCells) {
    auto* map = view_->currentMap();
    int gi = -1;
    for (int i = 0; i < static_cast<int>(map->layers().size()); ++i)
        if (map->layers()[i]->kind() == core::LayerKind::Grid) { gi = i; break; }
    ASSERT_GE(gi, 0);
    auto& grid = static_cast<core::LayerGrid&>(*map->layers()[gi]);
    grid.displayCellIndex = true;
    grid.visible = true;
    map->selectedLayerIndex = gi;
    const QPoint before = grid.cellIndexCorner;
    const double px = 8.0, cell = grid.gridSizeInStud;
    view_->resize(800, 600);
    const QPointF start(-40.5 * cell, -40.5 * cell);  // far from any brick
    view_->centerOn(start * px);
    QWidget* vp = view_->viewport();

    // Cancelled with the right button: nothing changes.
    mouse(vp, QEvent::MouseButtonPress, view_->mapFromScene(start * px), Qt::LeftButton);
    mouse(vp, QEvent::MouseMove, view_->mapFromScene((start + QPointF(cell, 0)) * px), Qt::LeftButton);
    QMouseEvent right(QEvent::MouseButtonPress, QPointF(view_->mapFromScene(start * px)), QPointF(), Qt::RightButton,
                      Qt::LeftButton | Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(vp, &right);
    EXPECT_EQ(grid.cellIndexCorner, before);
    mouse(vp, QEvent::MouseButtonRelease, view_->mapFromScene(start * px), Qt::NoButton);
    EXPECT_EQ(view_->undoStack()->count(), 0);

    // Two cells right, one down: one undo step.
    mouse(vp, QEvent::MouseButtonPress, view_->mapFromScene(start * px), Qt::LeftButton);
    mouse(vp, QEvent::MouseMove, view_->mapFromScene((start + QPointF(cell, 0)) * px), Qt::LeftButton);
    mouse(vp, QEvent::MouseMove, view_->mapFromScene((start + QPointF(2 * cell, cell)) * px), Qt::LeftButton);
    mouse(vp, QEvent::MouseButtonRelease, view_->mapFromScene((start + QPointF(2 * cell, cell)) * px), Qt::NoButton);
    auto& after = static_cast<core::LayerGrid&>(*view_->currentMap()->layers()[gi]);
    EXPECT_EQ(after.cellIndexCorner, before + QPoint(2, 1));
    EXPECT_EQ(view_->undoStack()->count(), 1);
    view_->undoStack()->undo();
    EXPECT_EQ(static_cast<core::LayerGrid&>(*view_->currentMap()->layers()[gi]).cellIndexCorner, before);
}

int main(int argc, char** argv) {
    // Run off screen as ctest and CI do, also when started by hand from a
    // desktop session: a real compositor (Wayland) may refuse the popups and
    // focus the tests drive.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    // Keep MapView's QSettings reads/writes away from the real app's store.
    QCoreApplication::setOrganizationName(QStringLiteral("BrickLayoutDesignerTests"));
    QCoreApplication::setApplicationName(QStringLiteral("bld_ui_tests"));
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
