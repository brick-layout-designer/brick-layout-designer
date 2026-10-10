#include "edit/EditCommands.h"
#include "core/Brick.h"
#include "core/LayerBrick.h"
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QUndoStack>

using namespace bld;

namespace {

core::Map makeMapWithBrickLayer() {
    core::Map m;
    auto layer = std::make_unique<core::LayerBrick>();
    layer->guid = QStringLiteral("L1");
    layer->name = QStringLiteral("Track");
    m.layers().push_back(std::move(layer));
    return m;
}

core::Brick makeBrick(const QString& guid, const QString& part, QRectF area, float orient = 0) {
    core::Brick b;
    b.guid = guid;
    b.partNumber = part;
    b.displayArea = area;
    b.orientation = orient;
    return b;
}

}

TEST(EditCommands, AddAndUndoAddBrick) {
    core::Map m = makeMapWithBrickLayer();

    QUndoStack stack;
    auto brick = makeBrick(QStringLiteral("b1"), QStringLiteral("3001.1"), QRectF(0, 0, 16, 16));
    stack.push(new edit::AddBrickCommand(m, 0, brick));

    auto* L = static_cast<core::LayerBrick*>(m.layers()[0].get());
    ASSERT_EQ(L->bricks.size(), 1u);
    EXPECT_EQ(L->bricks[0].guid, QStringLiteral("b1"));

    stack.undo();
    EXPECT_EQ(L->bricks.size(), 0u);

    stack.redo();
    ASSERT_EQ(L->bricks.size(), 1u);
    EXPECT_EQ(L->bricks[0].guid, QStringLiteral("b1"));
}

TEST(EditCommands, MoveUndoRestoresOriginal) {
    core::Map m = makeMapWithBrickLayer();
    auto* L = static_cast<core::LayerBrick*>(m.layers()[0].get());
    L->bricks.push_back(makeBrick(QStringLiteral("b1"), QStringLiteral("3001.1"), QRectF(10, 20, 16, 16)));

    QUndoStack stack;
    edit::MoveBricksCommand::Entry e;
    e.ref.layerIndex = 0;
    e.ref.guid = QStringLiteral("b1");
    e.beforeTopLeft = QPointF(10, 20);
    e.afterTopLeft  = QPointF(50, 70);
    stack.push(new edit::MoveBricksCommand(m, { e }));

    EXPECT_EQ(L->bricks[0].displayArea.topLeft(), QPointF(50, 70));
    stack.undo();
    EXPECT_EQ(L->bricks[0].displayArea.topLeft(), QPointF(10, 20));
    stack.redo();
    EXPECT_EQ(L->bricks[0].displayArea.topLeft(), QPointF(50, 70));
}

TEST(EditCommands, RotateRoundTrip) {
    core::Map m = makeMapWithBrickLayer();
    auto* L = static_cast<core::LayerBrick*>(m.layers()[0].get());
    L->bricks.push_back(makeBrick(QStringLiteral("b1"), QStringLiteral("x"), QRectF(), 0.0f));

    QUndoStack stack;
    edit::RotateBricksCommand::Entry e;
    e.ref.layerIndex = 0;
    e.ref.guid = QStringLiteral("b1");
    e.beforeOrientation = 0.0f;
    e.afterOrientation  = 90.0f;
    stack.push(new edit::RotateBricksCommand(m, { e }));

    EXPECT_FLOAT_EQ(L->bricks[0].orientation, 90.0f);
    stack.undo();
    EXPECT_FLOAT_EQ(L->bricks[0].orientation, 0.0f);
}

TEST(EditCommands, DeleteUndoRestoresIndex) {
    core::Map m = makeMapWithBrickLayer();
    auto* L = static_cast<core::LayerBrick*>(m.layers()[0].get());
    L->bricks.push_back(makeBrick(QStringLiteral("a"), QStringLiteral("p"), QRectF()));
    L->bricks.push_back(makeBrick(QStringLiteral("b"), QStringLiteral("p"), QRectF()));
    L->bricks.push_back(makeBrick(QStringLiteral("c"), QStringLiteral("p"), QRectF()));

    QUndoStack stack;
    edit::DeleteBricksCommand::Entry e;
    e.layerIndex = 0;
    e.indexInLayer = 1;
    e.brick = L->bricks[1];
    stack.push(new edit::DeleteBricksCommand(m, { e }));

    ASSERT_EQ(L->bricks.size(), 2u);
    EXPECT_EQ(L->bricks[0].guid, QStringLiteral("a"));
    EXPECT_EQ(L->bricks[1].guid, QStringLiteral("c"));

    stack.undo();
    ASSERT_EQ(L->bricks.size(), 3u);
    EXPECT_EQ(L->bricks[1].guid, QStringLiteral("b"));
}

TEST(EditCommands, ReorderBringToFrontSendToBack) {
    core::Map m = makeMapWithBrickLayer();
    auto* L = static_cast<core::LayerBrick*>(m.layers()[0].get());
    for (const auto& g : { "a", "b", "c", "d" }) {
        L->bricks.push_back(makeBrick(QString::fromUtf8(g), QStringLiteral("x"), QRectF()));
    }

    QUndoStack stack;
    // Send "b" and "d" to back → order becomes b, d, a, c.
    std::vector<edit::ReorderBricksCommand::Target> toBack = {
        { 0, QStringLiteral("b") }, { 0, QStringLiteral("d") }
    };
    stack.push(new edit::ReorderBricksCommand(m, toBack, edit::ReorderBricksCommand::ToBack));
    ASSERT_EQ(L->bricks.size(), 4u);
    EXPECT_EQ(L->bricks[0].guid, QStringLiteral("b"));
    EXPECT_EQ(L->bricks[1].guid, QStringLiteral("d"));
    EXPECT_EQ(L->bricks[2].guid, QStringLiteral("a"));
    EXPECT_EQ(L->bricks[3].guid, QStringLiteral("c"));

    stack.undo();
    EXPECT_EQ(L->bricks[0].guid, QStringLiteral("a"));
    EXPECT_EQ(L->bricks[3].guid, QStringLiteral("d"));

    // Bring "a" to front → order becomes b, c, d, a.
    std::vector<edit::ReorderBricksCommand::Target> toFront = {
        { 0, QStringLiteral("a") }
    };
    stack.push(new edit::ReorderBricksCommand(m, toFront, edit::ReorderBricksCommand::ToFront));
    EXPECT_EQ(L->bricks[3].guid, QStringLiteral("a"));
    stack.undo();
    EXPECT_EQ(L->bricks[0].guid, QStringLiteral("a"));
}

TEST(EditCommands, AddBricksBatchAtomicUndo) {
    core::Map m = makeMapWithBrickLayer();
    auto* L = static_cast<core::LayerBrick*>(m.layers()[0].get());
    L->bricks.push_back(makeBrick(QStringLiteral("existing"), QStringLiteral("x"), QRectF()));

    QUndoStack stack;
    std::vector<core::Brick> batch = {
        makeBrick(QStringLiteral("n1"), QStringLiteral("a"), QRectF()),
        makeBrick(QStringLiteral("n2"), QStringLiteral("b"), QRectF()),
        makeBrick(QStringLiteral("n3"), QStringLiteral("c"), QRectF()),
    };
    stack.push(new edit::AddBricksCommand(m, 0, batch));
    EXPECT_EQ(L->bricks.size(), 4u);

    // Single undo removes all three at once.
    stack.undo();
    ASSERT_EQ(L->bricks.size(), 1u);
    EXPECT_EQ(L->bricks[0].guid, QStringLiteral("existing"));

    stack.redo();
    EXPECT_EQ(L->bricks.size(), 4u);
}

TEST(EditCommands, MultiBrickDelete) {
    core::Map m = makeMapWithBrickLayer();
    auto* L = static_cast<core::LayerBrick*>(m.layers()[0].get());
    for (const auto& g : { "a", "b", "c", "d" }) {
        L->bricks.push_back(makeBrick(QString::fromUtf8(g), QStringLiteral("p"), QRectF()));
    }
    QUndoStack stack;
    std::vector<edit::DeleteBricksCommand::Entry> entries;
    for (int i = 0; i < static_cast<int>(L->bricks.size()); ++i) {
        if (L->bricks[i].guid == QStringLiteral("b") || L->bricks[i].guid == QStringLiteral("d")) {
            edit::DeleteBricksCommand::Entry e;
            e.layerIndex = 0;
            e.indexInLayer = i;
            e.brick = L->bricks[i];
            entries.push_back(std::move(e));
        }
    }
    stack.push(new edit::DeleteBricksCommand(m, std::move(entries)));
    ASSERT_EQ(L->bricks.size(), 2u);
    EXPECT_EQ(L->bricks[0].guid, QStringLiteral("a"));
    EXPECT_EQ(L->bricks[1].guid, QStringLiteral("c"));
    stack.undo();
    ASSERT_EQ(L->bricks.size(), 4u);
}

TEST(EditCommands, MoveBricksToAnotherSheetTakesWholeGroupsAndUndoes) {
    core::Map m = makeMapWithBrickLayer();
    m.layers().push_back(std::make_unique<core::LayerBrick>());
    auto* A = static_cast<core::LayerBrick*>(m.layers()[0].get());
    auto* B = static_cast<core::LayerBrick*>(m.layers()[1].get());
    for (const auto& g : { "g1", "g2", "h1", "h2", "solo" })
        A->bricks.push_back(makeBrick(QString::fromUtf8(g), QStringLiteral("x"), QRectF()));
    B->bricks.push_back(makeBrick(QStringLiteral("there"), QStringLiteral("x"), QRectF()));
    core::Group whole; whole.guid = QStringLiteral("W");
    core::Group split; split.guid = QStringLiteral("S");
    A->groups = { whole, split };
    A->bricks[0].myGroupId = A->bricks[1].myGroupId = QStringLiteral("W");
    A->bricks[2].myGroupId = A->bricks[3].myGroupId = QStringLiteral("S");
    A->bricks[0].connections.push_back({});
    A->bricks[0].connections[0].linkedToId = QStringLiteral("h2");

    QUndoStack stack;
    std::vector<edit::MoveBricksToLayerCommand::Target> t = {
        { 0, QStringLiteral("g1") }, { 0, QStringLiteral("g2") }, { 0, QStringLiteral("h1") } };
    stack.push(new edit::MoveBricksToLayerCommand(m, t, 1));
    ASSERT_EQ(A->bricks.size(), 2u);
    EXPECT_EQ(A->bricks[0].guid, QStringLiteral("h2"));
    EXPECT_EQ(A->bricks[0].myGroupId, QStringLiteral("S"));
    ASSERT_EQ(B->bricks.size(), 4u);
    EXPECT_EQ(B->bricks[0].guid, QStringLiteral("there"));  // on top of what was there
    EXPECT_EQ(B->bricks[1].guid, QStringLiteral("g1"));
    EXPECT_EQ(B->bricks[1].myGroupId, QStringLiteral("W"));
    EXPECT_TRUE(B->bricks[1].connections[0].linkedToId.isEmpty());
    EXPECT_EQ(B->bricks[3].guid, QStringLiteral("h1"));
    EXPECT_TRUE(B->bricks[3].myGroupId.isEmpty());  // its group stayed behind
    ASSERT_EQ(B->groups.size(), 1u);
    EXPECT_EQ(B->groups[0].guid, QStringLiteral("W"));
    ASSERT_EQ(A->groups.size(), 1u);
    EXPECT_EQ(A->groups[0].guid, QStringLiteral("S"));

    stack.undo();
    EXPECT_EQ(A->bricks.size(), 5u);
    EXPECT_EQ(B->bricks.size(), 1u);
    EXPECT_EQ(A->groups.size(), 2u);
    EXPECT_EQ(A->bricks[0].myGroupId, QStringLiteral("W"));
    stack.redo();
    EXPECT_EQ(B->bricks.size(), 4u);
}
