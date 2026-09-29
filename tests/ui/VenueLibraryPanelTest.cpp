// Venue Library "Start Layout": with a venue selected, the button asks for
// a new layout from that venue (MainWindow::startLayoutFromVenue), and it
// is disabled with nothing selected.

#include "saveload/VenueIO.h"
#include "ui/VenueLibraryPanel.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>

#include <vector>

using namespace bld;

TEST(VenueLibraryPanel, StartLayoutAsksForANewLayoutFromTheSelectedVenue) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    core::Venue hall;
    hall.name = QStringLiteral("Grand Lobby");
    hall.edges.push_back({ { QPointF(0, 0), QPointF(100, 0) }, core::EdgeKind::Open, 0.0, QStringLiteral("to the Lobby") });
    hall.obstacles.push_back({ { QPointF(1, 1), QPointF(2, 1), QPointF(2, 2) }, QStringLiteral("stairs") });
    ASSERT_TRUE(saveload::writeVenueFile(QDir(dir.path()).filePath(QStringLiteral("Grand Lobby.bld-venue")), hall));

    ui::VenueLibraryPanel panel;
    panel.setLibraryPath(dir.path());
    auto* start = panel.findChild<QPushButton*>(QStringLiteral("venueStartLayout"));
    auto* list = panel.findChild<QListWidget*>();
    ASSERT_TRUE(start && list);
    ASSERT_EQ(list->count(), 1);
    list->setCurrentRow(-1);
    EXPECT_FALSE(start->isEnabled());

    std::vector<core::Venue> asked;
    int loaded = 0;
    QObject::connect(&panel, &ui::VenueLibraryPanel::newLayoutRequested, [&](const core::Venue& v) { asked.push_back(v); });
    QObject::connect(&panel, &ui::VenueLibraryPanel::venueLoadRequested, [&](const core::Venue&) { ++loaded; });
    list->setCurrentRow(0);
    ASSERT_TRUE(start->isEnabled());
    start->click();
    ASSERT_EQ(asked.size(), 1u);
    EXPECT_EQ(loaded, 0);
    const core::Venue& v = asked[0];
    EXPECT_EQ(v.name, QStringLiteral("Grand Lobby"));
    ASSERT_EQ(v.obstacles.size(), 1);
    EXPECT_EQ(v.obstacles[0].label, QStringLiteral("stairs"));
    ASSERT_EQ(v.edges.size(), 1);
    EXPECT_EQ(v.edges[0].kind, core::EdgeKind::Open);
}
