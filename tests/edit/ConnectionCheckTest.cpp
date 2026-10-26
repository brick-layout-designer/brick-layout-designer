// The connection check (edit/ConnectionCheck.h) on real Brick Track Designer
// exports (fixtures/btd-layout-parts): each set placed whole, its points
// paired where its parts meet, its track ends left open.

#include "edit/ConnectionCheck.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "edit/Sets.h"
#include "import/PartsZip.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QTemporaryDir>

#include <memory>

using namespace bld;

namespace {

// The sample unpacked and read as a parts library.
struct Sample {
    QTemporaryDir dir;
    parts::PartsLibrary lib;
    explicit Sample(const char* zip) {
        // Part pictures (the parts' footprints) need a GUI application.
        if (!QCoreApplication::instance()) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
            static int argc = 1;
            static char name[] = "bld_edit_tests";
            static char* argv[] = { name, nullptr };
            new QGuiApplication(argc, argv);
        }
        const auto added = import::addPartsFromZip(
            QStringLiteral(BLD_SOURCE_DIR "/fixtures/btd-layout-parts/") + QString::fromUtf8(zip),
            dir.path());
        EXPECT_TRUE(added.ok) << added.error.toStdString();
        for (const QString& f : added.folders) lib.addSearchPath(dir.filePath(f));
        lib.scan();
    }
};

} // namespace

TEST(ConnectionCheck, YardPairsEveryCutAndLeavesItsFourTrackEndsOpen) {
    Sample yard("Yard.zip");
    const auto r = edit::checkSetConnections(QStringLiteral("Yard.whole"), yard.lib);
    EXPECT_EQ(r.parts, 15);
    EXPECT_EQ(r.points, 34);
    EXPECT_EQ(r.pointsByType, (QMap<QString, int>{ { QStringLiteral("1"), 34 } }));
    EXPECT_EQ(r.pairs.size(), 15);
    ASSERT_EQ(r.open.size(), 4);
    EXPECT_TRUE(r.rough.isEmpty());
    // The ends: two at each end of the yard, 144 studs apart, facing out.
    for (const auto& end : r.open) {
        EXPECT_NEAR(std::abs(end.at.y()), 72.0, 1e-3);
        EXPECT_NEAR(end.angle, end.at.y() < 0 ? -90.0 : 90.0, 1e-3);
    }
}

TEST(ConnectionCheck, TheLeftSwitchHasItsThreeEndsOpen) {
    Sample sw("Left switch cut 150.zip");
    const auto r = edit::checkSetConnections(QStringLiteral("Left_switch_cut_150.whole"), sw.lib);
    EXPECT_EQ(r.parts, 3);
    EXPECT_EQ(r.points, 11);
    EXPECT_EQ(r.pairs.size(), 4);
    EXPECT_EQ(r.open.size(), 3);
    EXPECT_TRUE(r.rough.isEmpty());
}

TEST(ConnectionCheck, TheMonorailLoopIsClosedAndMonorailOnly) {
    Sample loop("Monorail loop.zip");
    const auto r = edit::checkSetConnections(QStringLiteral("Monorail_loop.whole"), loop.lib);
    EXPECT_EQ(r.parts, 8);
    EXPECT_EQ(r.pointsByType, (QMap<QString, int>{ { QStringLiteral("3"), 16 } }));
    EXPECT_EQ(r.pairs.size(), 8);
    EXPECT_TRUE(r.open.isEmpty());
}

TEST(ConnectionCheck, OnALayoutAPartMovedALittleIsRoughAndOneMovedAwayLeavesEndsOpen) {
    Sample sw("Left switch cut 150.zip");
    auto set = edit::expandSet(sw.lib, QStringLiteral("Left_switch_cut_150.whole"), QPointF(10, 10));
    ASSERT_EQ(set.bricks.size(), 3u);
    core::Map map;
    auto layer = std::make_unique<core::LayerBrick>();
    layer->bricks = set.bricks;
    // A hair off: still linked, but not cleanly.
    layer->bricks[1].displayArea.translate(0.1, 0);
    map.layers().push_back(std::move(layer));
    auto r = edit::checkMapConnections(map, sw.lib);
    EXPECT_EQ(r.pairs.size(), 4);
    EXPECT_FALSE(r.rough.isEmpty());
    // Far away: the joints it had are open ends now.
    static_cast<core::LayerBrick&>(*map.layers().front()).bricks[1].displayArea.translate(100, 0);
    r = edit::checkMapConnections(map, sw.lib);
    EXPECT_EQ(r.pairs.size(), 0);
    EXPECT_EQ(r.open.size(), 11);
    const QString text = edit::connectionReportText(QStringLiteral("moved"), r);
    EXPECT_TRUE(text.contains(QStringLiteral("0 pairs, 11 open ends"))) << text.toStdString();
}
