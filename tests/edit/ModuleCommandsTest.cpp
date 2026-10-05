#include "edit/ModuleCommands.h"
#include "core/Brick.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "core/Sidecar.h"

#include <gtest/gtest.h>

#include <QUndoStack>
#include <QUuid>

using namespace bld;

namespace {

core::Map makeMapWithBrickLayer() {
    core::Map m;
    auto L = std::make_unique<core::LayerBrick>();
    L->guid = QStringLiteral("L1");
    m.layers().push_back(std::move(L));
    return m;
}

core::Brick makeBrick(const QString& guid, QRectF area = QRectF(0, 0, 16, 16)) {
    core::Brick b;
    b.guid = guid;
    b.partNumber = QStringLiteral("3001.1");
    b.displayArea = area;
    return b;
}

}

TEST(ModuleCommands, CreateAddsToSidecarAndUndo) {
    core::Map m = makeMapWithBrickLayer();
    auto* L = static_cast<core::LayerBrick*>(m.layers()[0].get());
    L->bricks.push_back(makeBrick(QStringLiteral("a")));
    L->bricks.push_back(makeBrick(QStringLiteral("b")));

    QUndoStack stack;
    stack.push(new edit::CreateModuleCommand(
        m, QStringLiteral("Station A"),
        { { 0, QStringLiteral("a") }, { 0, QStringLiteral("b") } }));

    ASSERT_EQ(m.sidecar.modules.size(), 1u);
    EXPECT_EQ(m.sidecar.modules[0].name, QStringLiteral("Station A"));
    EXPECT_EQ(m.sidecar.modules[0].memberIds.size(), 2);

    stack.undo();
    EXPECT_EQ(m.sidecar.modules.size(), 0u);
}

TEST(ModuleCommands, DeleteRestoresOnUndo) {
    core::Map m;
    core::Module mod;
    mod.id = QStringLiteral("mod-1");
    mod.name = QStringLiteral("X");
    mod.memberIds.insert(QStringLiteral("a"));
    m.sidecar.modules.push_back(mod);

    QUndoStack stack;
    stack.push(new edit::DeleteModuleCommand(m, QStringLiteral("mod-1")));
    EXPECT_TRUE(m.sidecar.modules.empty());
    stack.undo();
    ASSERT_EQ(m.sidecar.modules.size(), 1u);
    EXPECT_EQ(m.sidecar.modules[0].id, QStringLiteral("mod-1"));
}

TEST(ModuleCommands, MoveAppliesDeltaToAllMemberBricks) {
    core::Map m = makeMapWithBrickLayer();
    auto* L = static_cast<core::LayerBrick*>(m.layers()[0].get());
    L->bricks.push_back(makeBrick(QStringLiteral("a"), QRectF(10, 10, 16, 16)));
    L->bricks.push_back(makeBrick(QStringLiteral("b"), QRectF(50, 50, 16, 16)));
    L->bricks.push_back(makeBrick(QStringLiteral("c"), QRectF(100, 100, 16, 16)));

    core::Module mod;
    mod.id = QStringLiteral("mod-1");
    mod.memberIds.insert(QStringLiteral("a"));
    mod.memberIds.insert(QStringLiteral("b"));
    m.sidecar.modules.push_back(mod);

    QUndoStack stack;
    stack.push(new edit::MoveModuleCommand(m, QStringLiteral("mod-1"), QPointF(5, 7)));

    EXPECT_EQ(L->bricks[0].displayArea.topLeft(), QPointF(15, 17));
    EXPECT_EQ(L->bricks[1].displayArea.topLeft(), QPointF(55, 57));
    EXPECT_EQ(L->bricks[2].displayArea.topLeft(), QPointF(100, 100)); // not in module

    stack.undo();
    EXPECT_EQ(L->bricks[0].displayArea.topLeft(), QPointF(10, 10));
    EXPECT_EQ(L->bricks[1].displayArea.topLeft(), QPointF(50, 50));
}

TEST(ModuleCommands, ImportBbmAsModuleInsertsAndUndo) {
    core::Map m = makeMapWithBrickLayer();
    auto* L = static_cast<core::LayerBrick*>(m.layers()[0].get());
    L->bricks.push_back(makeBrick(QStringLiteral("existing")));

    std::vector<core::Brick> imports;
    imports.push_back(makeBrick(QStringLiteral(""), QRectF(0, 0, 16, 16)));
    imports.push_back(makeBrick(QStringLiteral(""), QRectF(16, 0, 16, 16)));

    QUndoStack stack;
    stack.push(new edit::ImportBbmAsModuleCommand(
        m, 0, QStringLiteral("/tmp/src.bbm"), QStringLiteral("Imported"),
        imports));

    ASSERT_EQ(L->bricks.size(), 3u);
    EXPECT_EQ(L->bricks[0].guid, QStringLiteral("existing"));
    ASSERT_EQ(m.sidecar.modules.size(), 1u);
    EXPECT_EQ(m.sidecar.modules[0].sourceFile, QStringLiteral("/tmp/src.bbm"));
    EXPECT_EQ(m.sidecar.modules[0].memberIds.size(), 2);
    // Imported bricks should have been assigned fresh GUIDs.
    for (const auto& id : m.sidecar.modules[0].memberIds) {
        EXPECT_FALSE(id.isEmpty());
        EXPECT_NE(id, QStringLiteral("existing"));
    }

    stack.undo();
    EXPECT_EQ(L->bricks.size(), 1u);
    EXPECT_EQ(L->bricks[0].guid, QStringLiteral("existing"));
    EXPECT_TRUE(m.sidecar.modules.empty());
}

// A module's look: colours stored per module, the "Same colour" link and
// Reset to default (core/ModuleLook.h), applied as one undo step each.
#include "core/ModuleLook.h"

TEST(ModuleLook, SameColourLinksTheTwoColours) {
    core::Module m;
    EXPECT_TRUE(core::coloursLinked(m));
    EXPECT_FALSE(core::hasCustomColours(m));
    EXPECT_EQ(core::moduleColour(m, core::ModuleColourPart::Outline), core::kModuleDefaultColour);
    auto a = core::withColour(m, core::ModuleColourPart::Outline, QStringLiteral("#FF8800"));
    EXPECT_EQ(a.outlineColor, QStringLiteral("#ff8800"));
    EXPECT_EQ(a.nameColor, QStringLiteral("#ff8800"));
    a = core::withColour(a, core::ModuleColourPart::Name, QStringLiteral("#00aa00"));
    EXPECT_EQ(a.outlineColor, QStringLiteral("#00aa00"));
    EXPECT_TRUE(core::hasCustomColours(a));
}

TEST(ModuleLook, UnlinkedColoursAreSetSeparately) {
    auto m = core::withSameColour(core::withColour(core::Module{}, core::ModuleColourPart::Outline, QStringLiteral("#ff8800")), false);
    EXPECT_FALSE(m.sameColor);
    m = core::withColour(m, core::ModuleColourPart::Name, QStringLiteral("#112233"));
    EXPECT_EQ(m.outlineColor, QStringLiteral("#ff8800"));
    EXPECT_EQ(m.nameColor, QStringLiteral("#112233"));
    m = core::withColour(m, core::ModuleColourPart::Outline, QStringLiteral("#445566"));
    EXPECT_EQ(m.outlineColor, QStringLiteral("#445566"));
    EXPECT_EQ(m.nameColor, QStringLiteral("#112233"));
    // Linking again gives the name the outline's colour.
    m = core::withSameColour(m, true);
    EXPECT_TRUE(m.sameColor);
    EXPECT_EQ(m.nameColor, QStringLiteral("#445566"));
}

TEST(ModuleLook, ResetToDefaultClearsTheColoursAndTheLink) {
    auto m = core::withColour(core::withSameColour(core::Module{}, false), core::ModuleColourPart::Name, QStringLiteral("#112233"));
    EXPECT_TRUE(core::hasCustomColours(m));
    m = core::withDefaultColours(m);
    EXPECT_TRUE(m.outlineColor.isEmpty());
    EXPECT_TRUE(m.nameColor.isEmpty());
    EXPECT_TRUE(m.sameColor);
    EXPECT_FALSE(core::hasCustomColours(m));
    EXPECT_TRUE(core::hasCustomColours(core::withSameColour(core::Module{}, false)));
}

TEST(ModuleCommands, UpdateModuleChangesTheLookAndUndoes) {
    core::Map map = makeMapWithBrickLayer();
    core::Module m;
    m.id = QStringLiteral("m1");
    m.name = QStringLiteral("Harbour");
    m.memberIds = { QStringLiteral("a") };
    map.sidecar.modules.push_back(m);
    QUndoStack stack;
    stack.push(new edit::UpdateModuleCommand(map, core::withShowName(core::withColour(m, core::ModuleColourPart::Outline, QStringLiteral("#ff8800")), false),
                                             QStringLiteral("look")));
    EXPECT_EQ(map.sidecar.modules[0].outlineColor, QStringLiteral("#ff8800"));
    EXPECT_FALSE(map.sidecar.modules[0].showName);
    // A part added since keeps its place in the module through undo / redo.
    map.sidecar.modules[0].memberIds.insert(QStringLiteral("b"));
    stack.undo();
    EXPECT_TRUE(map.sidecar.modules[0].outlineColor.isEmpty());
    EXPECT_TRUE(map.sidecar.modules[0].showName);
    EXPECT_TRUE(map.sidecar.modules[0].memberIds.contains(QStringLiteral("b")));
    stack.redo();
    EXPECT_EQ(map.sidecar.modules[0].outlineColor, QStringLiteral("#ff8800"));
    EXPECT_TRUE(map.sidecar.modules[0].memberIds.contains(QStringLiteral("b")));
}
