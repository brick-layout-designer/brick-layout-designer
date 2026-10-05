// Drawing the venue model v2 parts (the web repo's references/
// VENUE-MODEL.md, "Drawing"): the shared geometry (the web's venueDraw test
// checks the same numbers) and what the scene gets for the Grand Lobby.

#include "rendering/VenueDraw.h"
#include "rendering/SceneBuilder.h"
#include "rendering/VenueLabels.h"

#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "saveload/VenueIO.h"

#include <gtest/gtest.h>

#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>

#include <cmath>

using namespace bld;
namespace vd = bld::rendering::venuedraw;

namespace {
QVector<QVector<int>> rounded(const QVector<QLineF>& ls) {
    QVector<QVector<int>> out;
    for (const auto& l : ls)
        out.append({ int(std::lround(l.x1())), int(std::lround(l.y1())), int(std::lround(l.x2())),
                     int(std::lround(l.y2())) });
    return out;
}
} // namespace

TEST(VenueDraw, StairTreadsRunAcrossAndTheArrowPointsUp) {
    const QVector<QPointF> stairs{ { 0, 0 }, { 40, 0 }, { 40, 30 }, { 0, 30 } }; // up to the north
    const auto m = vd::stairMarks(stairs, 270.0);
    EXPECT_EQ(rounded(m.treads), (QVector<QVector<int>>{ { 0, 20, 40, 20 }, { 0, 10, 40, 10 } }));
    ASSERT_EQ(m.arrow.size(), 3);
    EXPECT_NEAR(m.arrow[0].x1(), 20, 1e-9);
    EXPECT_NEAR(m.arrow[0].x2(), 20, 1e-9);
    EXPECT_LT(m.arrow[0].y2(), m.arrow[0].y1());
    EXPECT_TRUE(vd::stairMarks(stairs, std::nullopt).treads.isEmpty());
}

TEST(VenueDraw, ElevatorsPowerAndMeasurementsMatchTheWeb) {
    EXPECT_EQ(rounded(vd::elevatorCross({ { 0, 0 }, { 4, 0 }, { 4, 2 } })),
              (QVector<QVector<int>>{ { 0, 0, 4, 2 }, { 4, 0, 0, 2 } }));
    core::VenuePower p;
    p.label = QStringLiteral("Stage");
    p.amps = 20;
    p.volts = 120;
    EXPECT_EQ(vd::powerText(p), QStringLiteral("Stage · 20 A · 120 V"));
    EXPECT_TRUE(vd::powerText({}).isEmpty());
    const auto g = vd::dimensionGeometry({ 0, 0 }, { 100, 0 });
    ASSERT_TRUE(g);
    EXPECT_EQ(g->ticks[0], QLineF(0, -5, 0, 5));
    EXPECT_EQ(g->ticks[1], QLineF(100, -5, 100, 5));
    EXPECT_EQ(g->label, QPointF(50, -8));
    EXPECT_EQ(g->angleDeg, 0.0);
    EXPECT_EQ(vd::dimensionGeometry({ 100, 0 }, { 0, 0 })->angleDeg, 0.0);
    EXPECT_FALSE(vd::dimensionGeometry({ 1, 1 }, { 1, 1 }));
    EXPECT_FALSE(vd::obstacleStyle(core::ObstacleKind::Railing).fill);
}

TEST(VenueDraw, SceneShowsTheGrandLobbysPowerNotesAndEstimates) {
    const auto venue = saveload::readVenueFile(QStringLiteral(BLD_VENUE_FIXTURE));
    ASSERT_TRUE(venue);
    core::Map map;
    map.sidecar.venue = *venue;
    parts::PartsLibrary lib;
    QGraphicsScene scene;
    rendering::SceneBuilder builder(scene, lib);
    builder.build(map);

    int power = 0, floor = 0, faded = 0;
    QStringList texts;
    for (QGraphicsItem* it : scene.items()) {
        if (auto* e = dynamic_cast<QGraphicsEllipseItem*>(it)) {
            ++power;
            if (e->brush().color() != QColor(Qt::white)) ++floor;
        }
        if (auto* t = dynamic_cast<QGraphicsSimpleTextItem*>(it)) texts << t->text();
        // Wall labels are pills (VenueLabels.h) with their whole text on them.
        if (it->data(rendering::kVenueLabelTextRole).isValid()) texts << it->data(rendering::kVenueLabelTextRole).toString();
        if (dynamic_cast<QGraphicsPathItem*>(it) && std::abs(it->opacity() - 0.45) < 1e-9) ++faded;
    }
    EXPECT_EQ(power, venue->power.size());
    EXPECT_EQ(floor, 3);
    EXPECT_EQ(faded, 3); // the estimated outline edges
    EXPECT_TRUE(texts.contains(QStringLiteral("Concessions entrance is on the floor above")));
    EXPECT_TRUE(texts.contains(QStringLiteral("20′")));
    EXPECT_TRUE(texts.contains(QStringLiteral("≈ 57′ (est.)")));
    bool estEdge = false;
    for (const auto& t : texts)
        estEdge |=
            t.startsWith(QStringLiteral("angled north-east wall")) && t.endsWith(QStringLiteral("(est.)"));
    EXPECT_TRUE(estEdge);
}
