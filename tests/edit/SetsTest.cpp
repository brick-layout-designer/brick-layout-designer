// Library sets as BlueBrick groups (edit/Sets.h, core/Groups.h): placing
// one, grouping and ungrouping around it, counting it, saving it, and
// turning the modules older builds made of placed sets back into sets.

#include "core/Groups.h"
#include "core/Ids.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "edit/Connectivity.h"
#include "edit/EditCommands.h"
#include "edit/PartList.h"
#include "edit/Sets.h"
#include "parts/BrickPlacement.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"
#include "saveload/BbmWriter.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QUndoStack>

#include <memory>

using namespace bld;

namespace {

class SetsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Part pictures (the sets' footprints) need a GUI application.
        if (!QCoreApplication::instance()) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
            static int argc = 1;
            static char name[] = "bld_edit_tests";
            static char* argv[] = { name, nullptr };
            new QGuiApplication(argc, argv);
        }
        const QString root = QStringLiteral(BLD_SOURCE_DIR "/parts/BlueBrickParts/parts");
        if (!QDir(root).exists()) GTEST_SKIP() << "BlueBrickParts submodule missing";
        lib_.addSearchPath(root);
        lib_.scan();
        if (!lib_.metadata(QStringLiteral("flex.group"))) GTEST_SKIP() << "flex.group missing";
        auto layer = std::make_unique<core::LayerBrick>();
        layer->guid = core::newBbmId();
        map_.layers().push_back(std::move(layer));
    }

    core::LayerBrick& layer() { return static_cast<core::LayerBrick&>(*map_.layers().front()); }

    // Places `key` as the editor does, linking its joints.
    void place(const QString& key, QPointF at, double angle = 0.0) {
        auto set = edit::expandSet(lib_, key, at, angle);
        ASSERT_FALSE(set.bricks.empty()) << key.toStdString();
        stack_.push(new edit::AddBricksCommand(map_, 0, std::move(set.bricks), std::move(set.groups)));
        edit::rebuildConnectivity(map_, lib_);
    }

    // What an older build made of a placed set: loose parts in a module.
    void placeAsModule(const QString& key, QPointF at, const QString& name) {
        auto set = edit::expandSet(lib_, key, at);
        core::Module m;
        m.id = core::newBbmId();
        m.name = name;
        for (auto& b : set.bricks) {
            b.myGroupId.clear();
            m.memberIds.insert(b.guid);
            layer().bricks.push_back(b);
        }
        map_.sidecar.modules.push_back(m);
        edit::rebuildConnectivity(map_, lib_);
    }

    std::vector<edit::BrickRef> refs() {
        std::vector<edit::BrickRef> out;
        for (const auto& b : layer().bricks) out.push_back({ 0, b.guid });
        return out;
    }

    parts::PartsLibrary lib_;
    core::Map map_;
    QUndoStack stack_;
};

}  // namespace

TEST_F(SetsTest, AFlexTrackIsOneNamedGroupOfItsTwoHalves) {
    place(QStringLiteral("flex.group"), { 10, 10 });
    ASSERT_EQ(layer().groups.size(), 1u);
    EXPECT_EQ(layer().groups[0].partNumber, QStringLiteral("FLEX.GROUP"));
    ASSERT_EQ(layer().bricks.size(), 2u);
    EXPECT_EQ(layer().bricks[0].partNumber, QStringLiteral("88492.8"));
    EXPECT_EQ(layer().bricks[1].partNumber, QStringLiteral("88493.8"));
    for (const auto& b : layer().bricks) EXPECT_EQ(b.myGroupId, layer().groups[0].guid);
    // The XML's positions are the halves' box centres.
    EXPECT_NEAR(layer().bricks[0].displayArea.center().x(), 10 - 0.8, 1e-6);
    EXPECT_NEAR(layer().bricks[1].displayArea.center().x(), 10 + 0.8, 1e-6);
    // The hinge joint between them is linked.
    EXPECT_EQ(layer().bricks[0].connections[1].linkedToId, layer().bricks[1].connections[1].guid);
    stack_.undo();
    EXPECT_TRUE(layer().bricks.empty());
    EXPECT_TRUE(layer().groups.empty());
}

TEST_F(SetsTest, ANestedSetIsAChildGroup) {
    if (!lib_.metadata(QStringLiteral("10184-1"))) GTEST_SKIP() << "10184-1 missing";
    place(QStringLiteral("10184-1"), { 0, 0 });
    const core::Group* inner = nullptr;
    const core::Group* outer = nullptr;
    for (const auto& g : layer().groups) (g.myGroupId.isEmpty() ? outer : inner) = &g;
    ASSERT_TRUE(outer && inner);
    EXPECT_EQ(outer->partNumber, QStringLiteral("10184-1"));
    EXPECT_EQ(inner->partNumber, QStringLiteral("10184-1.GARAGE"));
    EXPECT_EQ(inner->myGroupId, outer->guid);
    for (const auto& b : layer().bricks) {
        EXPECT_NE(b.partNumber.toLower(), QStringLiteral("10184-1.garage")) << "a set is never a brick";
        EXPECT_EQ(core::topGroup(layer(), b.myGroupId), outer->guid);
    }
    // The part list counts the set once, as BlueBrick (TopNamedItem).
    EXPECT_EQ(core::libraryItems(layer()), QStringList{ QStringLiteral("10184-1") });
}

TEST_F(SetsTest, ThePartListCountsSetsNotTheirHalves) {
    place(QStringLiteral("flex.group"), { 0, 0 });
    place(QStringLiteral("flex.group"), { 0, 20 });
    const auto list = edit::buildPartList(map_, lib_, {});
    ASSERT_EQ(list.size(), 1u);
    ASSERT_EQ(list[0].rows.size(), 1u);
    EXPECT_EQ(list[0].rows[0].partNumber, QStringLiteral("FLEX.GROUP"));
    EXPECT_EQ(list[0].rows[0].count, 2);
    EXPECT_EQ(list[0].rows[0].description, QStringLiteral("Flex Track"));
}

TEST_F(SetsTest, GroupingSetsKeepsThemSetsAndAFlexTrackNeverUngroups) {
    place(QStringLiteral("flex.group"), { 0, 0 });
    place(QStringLiteral("flex.group"), { 0, 20 });
    auto* group = new edit::GroupBricksCommand(map_, refs());
    ASSERT_TRUE(group->changes());
    stack_.push(group);
    ASSERT_EQ(layer().groups.size(), 3u);
    const QString top = core::topGroup(layer(), layer().bricks[0].myGroupId);
    for (const auto& b : layer().bricks) {
        EXPECT_EQ(core::topGroup(layer(), b.myGroupId), top);
        EXPECT_EQ(core::findGroup(layer(), b.myGroupId)->partNumber, QStringLiteral("FLEX.GROUP")) << "still in its set";
    }
    const auto canUngroup = [&](const core::Group& g) {
        const auto meta = lib_.metadata(g.partNumber);
        return g.partNumber.isEmpty() || !meta || meta->canUngroup;
    };
    // Ungroup splits the user's group, back to the two sets...
    auto* ungroup = new edit::UngroupBricksCommand(map_, refs(), canUngroup);
    ASSERT_TRUE(ungroup->changes());
    stack_.push(ungroup);
    EXPECT_EQ(layer().groups.size(), 2u);
    for (const auto& g : layer().groups) EXPECT_TRUE(g.myGroupId.isEmpty());
    // ...but a flex track is always used whole.
    const edit::UngroupBricksCommand again(map_, refs(), canUngroup);
    EXPECT_FALSE(again.changes());
    EXPECT_EQ(again.refused(), 2);
    stack_.undo();
    stack_.undo();
    EXPECT_EQ(layer().groups.size(), 2u);
}

TEST_F(SetsTest, SavingLeavesOutAGroupWhosePartsWereDeleted) {
    place(QStringLiteral("flex.group"), { 0, 0 });
    place(QStringLiteral("flex.group"), { 0, 20 });
    const QString kept = layer().groups[1].guid;
    layer().bricks.erase(layer().bricks.begin(), layer().bricks.begin() + 2);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("sets.bbm"));
    ASSERT_TRUE(saveload::writeBbm(map_, path).ok);
    auto back = saveload::readBbm(path);
    ASSERT_TRUE(back.ok());
    const auto& L = static_cast<const core::LayerBrick&>(*back.map->layers().front());
    ASSERT_EQ(L.groups.size(), 1u);
    EXPECT_EQ(L.groups[0].guid, kept);
    EXPECT_EQ(L.groups[0].partNumber, QStringLiteral("FLEX.GROUP"));
}

TEST_F(SetsTest, AModuleThatIsExactlyASetBecomesASet) {
    placeAsModule(QStringLiteral("flex.group"), { 3, 4 }, QStringLiteral("Flex Track"));
    // Turned as a whole: still the set's layout.
    placeAsModule(QStringLiteral("flex.group"), { 30, 4 }, QStringLiteral("Flex Track"));
    for (size_t i = 2; i < 4; ++i) {
        auto& b = layer().bricks[i];
        const QPointF centre = QPointF(30, 4) + parts::placement::rotated(parts::placement::imageCentre(b, lib_) - QPointF(30, 4), 90);
        parts::placement::rotateAroundImageCentre(b, b.orientation + 90, lib_);
        parts::placement::placeByImageCentre(b, centre, lib_);
    }
    edit::rebuildConnectivity(map_, lib_);
    const auto found = edit::findSetModules(map_, lib_);
    ASSERT_EQ(found.size(), 2u);
    stack_.push(edit::makeSetsCommand(map_, found));
    EXPECT_TRUE(map_.sidecar.modules.empty());
    EXPECT_EQ(layer().groups.size(), 2u);
    EXPECT_EQ(core::libraryItems(layer()), (QStringList{ QStringLiteral("FLEX.GROUP"), QStringLiteral("FLEX.GROUP") }));
    stack_.undo();
    EXPECT_EQ(map_.sidecar.modules.size(), 2u);
    EXPECT_TRUE(layer().groups.empty());
    for (const auto& b : layer().bricks) EXPECT_TRUE(b.myGroupId.isEmpty());
}

TEST_F(SetsTest, ABentFlexTrackModuleBecomesASet) {
    placeAsModule(QStringLiteral("flex.group"), { 3, 4 }, QStringLiteral("Flex Track"));
    // Bend the male half 8 degrees around the hinge, as a flex move does.
    auto& female = layer().bricks[0];
    auto& male = layer().bricks[1];
    const QPointF hinge = parts::placement::connectionWorld(female, 1, lib_);
    male.orientation = 8.0f;
    parts::placement::placeByConnection(male, 1, hinge, lib_);
    edit::rebuildConnectivity(map_, lib_);
    ASSERT_EQ(female.connections[1].linkedToId, male.connections[1].guid);
    EXPECT_EQ(edit::findSetModules(map_, lib_).size(), 1u);
}

TEST_F(SetsTest, ModulesThatAreNotExactlyASetStayModules) {
    placeAsModule(QStringLiteral("flex.group"), { 3, 4 }, QStringLiteral("Flex Track"));
    placeAsModule(QStringLiteral("flex.group"), { 30, 4 }, QStringLiteral("My loop"));  // renamed
    placeAsModule(QStringLiteral("flex.group"), { 60, 4 }, QStringLiteral("Flex Track"));
    map_.sidecar.modules[2].pinned = true;                                                 // pinned
    placeAsModule(QStringLiteral("flex.group"), { 90, 4 }, QStringLiteral("Flex Track"));
    layer().bricks.back().displayArea.translate(0.5, 0);                                   // pulled apart
    placeAsModule(QStringLiteral("flex.group"), { 120, 4 }, QStringLiteral("Flex Track"));
    core::Brick extra = layer().bricks.back();                                              // a part more
    extra.guid = core::newBbmId();
    extra.displayArea.translate(0, 10);
    layer().bricks.push_back(extra);
    map_.sidecar.modules[4].memberIds.insert(extra.guid);
    edit::rebuildConnectivity(map_, lib_);
    const auto found = edit::findSetModules(map_, lib_);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].moduleId, map_.sidecar.modules[0].id);
}

TEST_F(SetsTest, LooseFlexHalvesStillJoinedAtTheirHingeBecomeSets) {
    // What layouts made before sets were groups hold: the halves loose, in no
    // group and no module (Aaron's could be pulled apart, one half alone).
    const auto placeLoose = [&](QPointF at, double angle) {
        auto set = edit::expandSet(lib_, QStringLiteral("flex.group"), at, angle);
        for (auto& b : set.bricks) {
            b.myGroupId.clear();
            layer().bricks.push_back(b);
        }
    };
    placeLoose({ 3, 4 }, 0);
    placeLoose({ 30, 4 }, 90);
    // Bent at the hinge, as a flex move leaves it.
    placeLoose({ 60, 4 }, 0);
    {
        auto& female = layer().bricks[4];
        auto& male = layer().bricks[5];
        const QPointF hinge = parts::placement::connectionWorld(female, 1, lib_);
        male.orientation = 9.0f;
        parts::placement::placeByConnection(male, 1, hinge, lib_);
    }
    // A half alone, and a pair pulled apart: they stay loose.
    placeLoose({ 90, 4 }, 0);
    layer().bricks.pop_back();
    placeLoose({ 120, 4 }, 0);
    layer().bricks.back().displayArea.translate(0.5, 0);
    // A set that is already a set: left as it is.
    place(QStringLiteral("flex.group"), { 150, 4 });
    edit::rebuildConnectivity(map_, lib_);

    const auto found = edit::findLooseSets(map_, lib_);
    ASSERT_EQ(found.size(), 3u);
    for (const auto& s : found) EXPECT_TRUE(s.moduleId.isEmpty());
    stack_.push(edit::makeSetsCommand(map_, found));
    ASSERT_EQ(layer().groups.size(), 4u);
    for (size_t i = 0; i < 6; ++i) EXPECT_FALSE(layer().bricks[i].myGroupId.isEmpty()) << i;
    EXPECT_EQ(layer().bricks[0].myGroupId, layer().bricks[1].myGroupId);
    EXPECT_EQ(layer().bricks[4].myGroupId, layer().bricks[5].myGroupId);
    EXPECT_NE(layer().bricks[0].myGroupId, layer().bricks[2].myGroupId);
    for (size_t i = 6; i < 9; ++i) EXPECT_TRUE(layer().bricks[i].myGroupId.isEmpty()) << i;
    // Run again (another app, or the next open): nothing more to do.
    EXPECT_TRUE(edit::findLooseSets(map_, lib_).empty());
    stack_.undo();
    EXPECT_EQ(layer().groups.size(), 1u);
    for (size_t i = 0; i < 9; ++i) EXPECT_TRUE(layer().bricks[i].myGroupId.isEmpty()) << i;
}

TEST_F(SetsTest, LooseHalvesAlreadyBecomingASetFromTheirModuleAreLeftToIt) {
    placeAsModule(QStringLiteral("flex.group"), { 3, 4 }, QStringLiteral("Flex Track"));
    const auto modules = edit::findSetModules(map_, lib_);
    ASSERT_EQ(modules.size(), 1u);
    QSet<QString> taken;
    for (auto it = modules[0].parentOf.constBegin(); it != modules[0].parentOf.constEnd(); ++it) taken.insert(it.key());
    EXPECT_TRUE(edit::findLooseSets(map_, lib_, taken).empty());
    EXPECT_EQ(edit::findLooseSets(map_, lib_).size(), 1u);
}
