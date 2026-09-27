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
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsScene>
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
    QApplication app(argc, argv);
    // Keep MapView's QSettings reads/writes away from the real app's store.
    QCoreApplication::setOrganizationName(QStringLiteral("BrickLayoutDesignerTests"));
    QCoreApplication::setApplicationName(QStringLiteral("bld_ui_tests"));
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
