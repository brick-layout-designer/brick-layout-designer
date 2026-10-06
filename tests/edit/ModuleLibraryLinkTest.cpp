// A placed module and its Module library copy (edit/ModuleLibraryLink.h, as the
// web's moduleLibrary.test.ts): the link and older builds' links, where a
// library version goes (turned and moved like the placed copy), whether the
// copy was changed, which sheet updated parts go on, and Update from
// library's command with its undo.

#include "edit/ModuleCommands.h"
#include "edit/ModuleLibraryLink.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "core/Module.h"
#include "core/Sidecar.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QUndoStack>

using namespace bld;

namespace {

parts::PartsLibrary& noParts() {
    static parts::PartsLibrary lib;
    return lib;
}

core::Brick brick(const QString& part, double x, double y, float orientation = 0) {
    core::Brick b;
    b.partNumber = part;
    b.displayArea = QRectF(x - 1, y - 1, 2, 2);
    b.orientation = orientation;
    return b;
}

edit::LayerBatches version(std::vector<core::Brick> bricks, const QString& sheet = QStringLiteral("Track")) {
    edit::ImportBbmAsModuleCommand::LayerBatch b;
    b.layerName = sheet;
    b.bricks = std::move(bricks);
    return { b };
}

std::vector<core::Brick> flat(const edit::LayerBatches& batches) {
    std::vector<core::Brick> out;
    for (const auto& b : batches) out.insert(out.end(), b.bricks.begin(), b.bricks.end());
    return out;
}

edit::LayerBatches v1() { return version({ brick("a", 0, 0), brick("b", 10, 0), brick("b", 10, 10) }); }

}  // namespace

TEST(ModuleLibraryLink, ReadsTheLinkAndTheServerPageOlderBuildsKept) {
    core::Module m;
    EXPECT_FALSE(edit::libraryLink(m));
    m.sourceFile = QStringLiteral("C:/modules/yard.bbm");
    EXPECT_FALSE(edit::libraryLink(m));
    m.sourceFile = QStringLiteral("https://bricks.example/modules/m1");
    ASSERT_TRUE(edit::libraryLink(m));
    EXPECT_EQ(edit::libraryLink(m)->id, QStringLiteral("m1"));
    EXPECT_EQ(edit::libraryLink(m)->version, 0);
    m.libraryModuleId = QStringLiteral("m2");
    m.libraryVersion = 4;
    EXPECT_EQ(edit::libraryLink(m)->id, QStringLiteral("m2"));
    EXPECT_EQ(edit::libraryLink(m)->version, 4);
}

TEST(ModuleLibraryLink, SaysWhereTheLibraryCopyIs) {
    EXPECT_EQ(edit::libraryNote(3, 3), QStringLiteral("in the Module library v3"));
    EXPECT_EQ(edit::libraryNote(3, 5), QStringLiteral("in the Module library v3 · v5 is newer"));
    EXPECT_EQ(edit::libraryNote(0, 1), QStringLiteral("in the Module library · v1 is newer"));
    EXPECT_EQ(edit::libraryNote(2, -1), QStringLiteral("its Module library copy isn't on this server"));
}

TEST(ModuleLibraryLink, FindsTheTurnAndShiftOfThePlacedCopyAndItMatches) {
    const auto lib = v1();
    const auto placed = flat(edit::placeVersion(lib, { 90.0, QPointF(100.0123, 50.0371), 0 }, noParts()));
    const auto p = edit::alignToPlaced(lib, placed, noParts());
    EXPECT_DOUBLE_EQ(p.degrees, 90.0);
    EXPECT_NEAR(p.to.x(), 100.0123, 1e-6);
    EXPECT_NEAR(p.to.y(), 50.0371, 1e-6);
    EXPECT_EQ(p.matched, 3);
    EXPECT_TRUE(edit::matchesVersion(lib, placed, noParts()));
}

TEST(ModuleLibraryLink, StillFindsItWhenAPartChangedAndThenSaysTheCopyChanged) {
    const auto lib = v1();
    auto placed = flat(edit::placeVersion(lib, { 180.0, QPointF(-20, 7), 0 }, noParts()));
    placed[2].displayArea.translate(3, 0);
    const auto p = edit::alignToPlaced(lib, placed, noParts());
    EXPECT_DOUBLE_EQ(p.degrees, 180.0);
    EXPECT_EQ(p.matched, 2);
    EXPECT_NEAR(p.to.x(), -20, 1e-6);
    EXPECT_NEAR(p.to.y(), 7, 1e-6);
    EXPECT_FALSE(edit::matchesVersion(lib, placed, noParts()));
    placed.pop_back();
    EXPECT_FALSE(edit::matchesVersion(lib, placed, noParts()));
}

TEST(ModuleLibraryLink, WithNothingAlikePutsTheMiddleOnTheMiddle) {
    const auto lib = v1();
    const auto p = edit::alignToPlaced(lib, { brick("zzz", 50, 50, 30) }, noParts());
    EXPECT_EQ(p.matched, 0);
    EXPECT_DOUBLE_EQ(p.degrees, 0.0);
    EXPECT_NEAR(p.to.x() + 20.0 / 3.0, 50, 1e-6);
    EXPECT_NEAR(p.to.y() + 10.0 / 3.0, 50, 1e-6);
}

TEST(ModuleLibraryLink, KeepsEachPartOnItsSheet) {
    core::Map map;
    auto add = [&](const QString& guid, const QString& name, std::vector<QString> parts) {
        auto l = std::make_unique<core::LayerBrick>();
        l->guid = guid;
        l->name = name;
        for (const auto& p : parts) {
            core::Brick b = brick("a", 0, 0);
            b.guid = p;
            l->bricks.push_back(b);
        }
        map.layers().push_back(std::move(l));
    };
    add(QStringLiteral("T"), QStringLiteral("Track"), { QStringLiteral("t1") });
    add(QStringLiteral("S"), QStringLiteral("Scenery"), { QStringLiteral("s1"), QStringLiteral("s2") });
    const auto pick = edit::sheetForUpdate(map, { QStringLiteral("t1"), QStringLiteral("s1"), QStringLiteral("s2") });
    EXPECT_EQ(pick(QStringLiteral(" track ")), QStringLiteral("T"));
    EXPECT_EQ(pick(QStringLiteral("Scenery")), QStringLiteral("S"));
    EXPECT_EQ(pick(QStringLiteral("Buildings")), QStringLiteral("S"));
}

TEST(ModuleLibraryLink, ReplacingTheModulesPartsIsOneStepThatUndoes) {
    core::Map map;
    auto l = std::make_unique<core::LayerBrick>();
    l->guid = QStringLiteral("L");
    for (const char* g : { "x", "old1", "y", "old2" }) {
        core::Brick b = brick(QString::fromLatin1(g), 0, 0);
        b.guid = QString::fromLatin1(g);
        l->bricks.push_back(b);
    }
    map.layers().push_back(std::move(l));
    core::Module m;
    m.id = QStringLiteral("M");
    m.name = QStringLiteral("Yard");
    m.pinned = true;
    m.memberIds = { QStringLiteral("old1"), QStringLiteral("old2") };
    map.sidecar.modules.push_back(m);
    auto batches = version({ brick("new1", 5, 5), brick("new2", 7, 5), brick("new3", 9, 5) });
    batches.front().targetLayerGuid = QStringLiteral("L");
    QUndoStack stack;
    auto* cmd = new edit::ReplaceModulePartsCommand(map, QStringLiteral("M"), batches, QStringLiteral("lib"), 4);
    stack.push(cmd);
    const auto& bricks = static_cast<core::LayerBrick&>(*map.layers()[0]).bricks;
    ASSERT_EQ(bricks.size(), 5u);
    EXPECT_EQ(bricks[0].guid, QStringLiteral("x"));
    EXPECT_EQ(bricks[1].guid, QStringLiteral("y"));
    const core::Module& after = map.sidecar.modules.front();
    EXPECT_EQ(after.memberIds.size(), 3);
    EXPECT_EQ(after.memberIds, cmd->newMembers());
    EXPECT_TRUE(after.pinned);
    EXPECT_EQ(after.name, QStringLiteral("Yard"));
    EXPECT_EQ(after.libraryModuleId, QStringLiteral("lib"));
    EXPECT_EQ(after.libraryVersion, 4);
    stack.undo();
    QStringList order;
    for (const auto& b : bricks) order << b.guid;
    EXPECT_EQ(order, (QStringList{ "x", "old1", "y", "old2" }));
    EXPECT_EQ(map.sidecar.modules.front().memberIds, (QSet<QString>{ "old1", "old2" }));
    EXPECT_TRUE(map.sidecar.modules.front().libraryModuleId.isEmpty());
    stack.redo();
    EXPECT_EQ(bricks.size(), 5u);
    EXPECT_EQ(map.sidecar.modules.front().memberIds, cmd->newMembers());
}
