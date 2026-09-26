// MapView scene-rebuild behaviour: every undo-stack change rebuilds the
// QGraphicsScene from core::Map, and the rebuild must keep the user's
// selection (by layer + guid) instead of dropping it with the old items.

#include "ui/MapView.h"
#include "ui/MapViewInternal.h"
#include "edit/EditCommands.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"
#include "core/LayerBrick.h"
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsScene>
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

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // Keep MapView's QSettings reads/writes away from the real app's store.
    QCoreApplication::setOrganizationName(QStringLiteral("BrickLayoutDesignerTests"));
    QCoreApplication::setApplicationName(QStringLiteral("bld_ui_tests"));
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
