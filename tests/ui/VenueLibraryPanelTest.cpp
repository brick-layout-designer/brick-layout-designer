// Venue Library "Start Layout": with a venue selected, the button asks for
// a new layout from that venue (MainWindow::startLayoutFromVenue), and it
// is disabled with nothing selected.

#include "saveload/VenueIO.h"
#include "ui/VenueLibraryPanel.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFileInfo>
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

TEST(VenueLibraryPanel, AddsDownloadedVenuesWithoutReplacingOnes) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    ui::VenueLibraryPanel panel;
    panel.setLibraryPath(dir.path());
    auto* list = panel.findChild<QListWidget*>();
    const QByteArray file = R"({"schema":"bld-venue/1","name":"Grand Lobby","edges":[],"obstacles":[]})";

    const QString first = panel.addVenueFile(QStringLiteral("Grand Lobby"), file);
    const QString second = panel.addVenueFile(QStringLiteral("Grand Lobby"), file);
    EXPECT_EQ(QFileInfo(first).fileName(), QStringLiteral("Grand Lobby.bld-venue"));
    EXPECT_EQ(QFileInfo(second).fileName(), QStringLiteral("Grand Lobby (2).bld-venue"));
    // Names that aren't safe file names still land in the library folder.
    const QString odd = panel.addVenueFile(QStringLiteral("../Hall: A/B"), file);
    ASSERT_FALSE(odd.isEmpty());
    EXPECT_EQ(QFileInfo(odd).absolutePath(), QDir(dir.path()).absolutePath());
    EXPECT_EQ(list->count(), 3);
    const auto v = saveload::readVenueFile(second);
    ASSERT_TRUE(v);
    EXPECT_EQ(v->name, QStringLiteral("Grand Lobby"));
}
