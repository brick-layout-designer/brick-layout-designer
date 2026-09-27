#include "edit/LayerCommands.h"
#include "core/ColorSpec.h"
#include "core/LayerBrick.h"
#include "core/LayerGrid.h"
#include "core/LayerText.h"
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QUndoStack>

using namespace bld;

TEST(LayerCommands, AddAndUndo) {
    core::Map m;
    QUndoStack stack;
    stack.push(new edit::AddLayerCommand(m, core::LayerKind::Brick, -1, QStringLiteral("Tracks")));
    ASSERT_EQ(m.layers().size(), 1u);
    EXPECT_EQ(m.layers()[0]->kind(), core::LayerKind::Brick);
    EXPECT_EQ(m.layers()[0]->name, QStringLiteral("Tracks"));
    stack.undo();
    EXPECT_EQ(m.layers().size(), 0u);
    stack.redo();
    EXPECT_EQ(m.layers().size(), 1u);
    EXPECT_EQ(m.layers()[0]->name, QStringLiteral("Tracks"));
}

TEST(LayerCommands, DeleteRestoresOnUndo) {
    core::Map m;
    auto g = std::make_unique<core::LayerGrid>();
    g->name = QStringLiteral("G");
    m.layers().push_back(std::move(g));
    auto t = std::make_unique<core::LayerText>();
    t->name = QStringLiteral("T");
    m.layers().push_back(std::move(t));

    QUndoStack stack;
    stack.push(new edit::DeleteLayerCommand(m, 0));
    ASSERT_EQ(m.layers().size(), 1u);
    EXPECT_EQ(m.layers()[0]->kind(), core::LayerKind::Text);
    stack.undo();
    ASSERT_EQ(m.layers().size(), 2u);
    EXPECT_EQ(m.layers()[0]->kind(), core::LayerKind::Grid);
    EXPECT_EQ(m.layers()[0]->name, QStringLiteral("G"));
}

TEST(LayerCommands, MoveAndUndoSwapsWithNeighbour) {
    core::Map m;
    for (const auto* n : { "A", "B", "C" }) {
        auto L = std::make_unique<core::LayerBrick>();
        L->name = QString::fromUtf8(n);
        m.layers().push_back(std::move(L));
    }
    QUndoStack stack;
    stack.push(new edit::MoveLayerCommand(m, 1, +1));  // B swaps with C → A, C, B
    EXPECT_EQ(m.layers()[1]->name, QStringLiteral("C"));
    EXPECT_EQ(m.layers()[2]->name, QStringLiteral("B"));
    stack.undo();
    EXPECT_EQ(m.layers()[1]->name, QStringLiteral("B"));
    EXPECT_EQ(m.layers()[2]->name, QStringLiteral("C"));
}

TEST(LayerCommands, RenameRoundTrip) {
    core::Map m;
    auto L = std::make_unique<core::LayerText>();
    L->name = QStringLiteral("Before");
    m.layers().push_back(std::move(L));

    QUndoStack stack;
    stack.push(new edit::RenameLayerCommand(m, 0, QStringLiteral("After")));
    EXPECT_EQ(m.layers()[0]->name, QStringLiteral("After"));
    stack.undo();
    EXPECT_EQ(m.layers()[0]->name, QStringLiteral("Before"));
}

TEST(LayerCommands, MapMetadataRoundTrip) {
    core::Map m;
    m.author = QStringLiteral("Ada");
    m.lug = QStringLiteral("Old LUG");
    m.event = QStringLiteral("Old Event");
    m.date = QDate(2020, 1, 1);
    m.comment = QStringLiteral("Old comment");

    edit::ChangeGeneralInfoCommand::Info next{
        QStringLiteral("Grace"), QStringLiteral("New LUG"), QStringLiteral("New Event"),
        QDate(2026, 6, 1), QStringLiteral("Updated")
    };
    QUndoStack stack;
    stack.push(new edit::ChangeGeneralInfoCommand(m, next));
    EXPECT_EQ(m.author, QStringLiteral("Grace"));
    EXPECT_EQ(m.date, QDate(2026, 6, 1));
    stack.undo();
    EXPECT_EQ(m.author, QStringLiteral("Ada"));
    EXPECT_EQ(m.date, QDate(2020, 1, 1));
}

TEST(LayerCommands, BackgroundColorRoundTrip) {
    core::Map m;
    m.backgroundColor = core::ColorSpec::fromArgb(QColor(255, 255, 255));
    QUndoStack stack;
    stack.push(new edit::ChangeBackgroundColorCommand(
        m, core::ColorSpec::fromArgb(QColor(100, 149, 237))));
    EXPECT_EQ(m.backgroundColor.color.rgba(), QColor(100, 149, 237).rgba());
    stack.undo();
    EXPECT_EQ(m.backgroundColor.color.rgba(), QColor(255, 255, 255).rgba());
}

TEST(LayerCommands, MoveGridOriginShiftsTheIndexCorner) {
    core::Map m;
    auto grid = std::make_unique<core::LayerGrid>();
    grid->cellIndexCorner = QPoint(2, -1);
    m.layers().push_back(std::move(grid));
    QUndoStack stack;
    stack.push(new edit::MoveGridOriginCommand(m, 0, 3, 4));
    auto& g = static_cast<core::LayerGrid&>(*m.layers()[0]);
    EXPECT_EQ(g.cellIndexCorner, QPoint(5, 3));
    stack.undo();
    EXPECT_EQ(g.cellIndexCorner, QPoint(2, -1));
}

TEST(LayerCommands, CellIndexLabelsLikeBlueBrick) {
    using core::LayerGrid;
    EXPECT_EQ(LayerGrid::cellIndexLabel(0, true), QString());
    EXPECT_EQ(LayerGrid::cellIndexLabel(-3, false), QString());
    EXPECT_EQ(LayerGrid::cellIndexLabel(1, true), QStringLiteral("A"));
    EXPECT_EQ(LayerGrid::cellIndexLabel(26, true), QStringLiteral("Z"));
    EXPECT_EQ(LayerGrid::cellIndexLabel(27, true), QStringLiteral("AA"));
    EXPECT_EQ(LayerGrid::cellIndexLabel(52, true), QStringLiteral("AZ"));
    EXPECT_EQ(LayerGrid::cellIndexLabel(53, true), QStringLiteral("BA"));
    EXPECT_EQ(LayerGrid::cellIndexLabel(702, true), QStringLiteral("ZZ"));
    EXPECT_EQ(LayerGrid::cellIndexLabel(703, true), QStringLiteral("AAA"));
    EXPECT_EQ(LayerGrid::cellIndexLabel(12, false), QStringLiteral("12"));
}
