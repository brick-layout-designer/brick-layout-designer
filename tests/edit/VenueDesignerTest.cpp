// The Venue Designer's core (edit/venue): typed lengths, the venue edits,
// snapping, undo, and each tool driven through the reducer as the window
// drives it. The same cases as the web's venues/designer/test/core.test.ts
// and tools.test.ts, so both apps design venues the same way.

#include "edit/venue/VenueDesignerState.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace bld;
using namespace bld::edit::venue;

namespace {

constexpr double IN = kStudsPerInch;
constexpr double FT = 12 * IN;

DesignerState run(DesignerState s, std::initializer_list<Action> actions) {
    for (const auto& a : actions) s = reduce(std::move(s), a);
    return s;
}
Action click(double x, double y, bool free = false) {
    return act::Down{ { x, y }, 0.5, free };
}
Action hover(double x, double y) {
    return act::Move{ { x, y }, 0.5, false };
}
Action tool(Tool t) {
    return act::SetTool{ t };
}

} // namespace

TEST(VenueUnits, ReadsLengthsAsWrittenOnASketch) {
    const std::pair<const char*, double> cases[] = {
        { "12'6\"", 150 },        { "12' 6\"", 150 }, { "12′ 6″", 150 }, { "40' 4\"", 484 },
        { "12ft 6in", 150 },      { "12.5'", 150 },   { "6\"", 6 },      { "6 1/2\"", 6.5 },
        { "12' 6 1/2\"", 150.5 }, { "12'6", 150 },    { "129", 129 },    { "74.5", 74.5 },
        { "12 feet", 144 },
    };
    for (const auto& [text, inches] : cases) {
        const auto v = parseLength(QString::fromUtf8(text));
        ASSERT_TRUE(v) << text;
        EXPECT_NEAR(*v, inches * IN, 1e-6) << text;
    }
    EXPECT_NEAR(*parseLength(QStringLiteral("3.2m")), 400, 1e-9);
    EXPECT_NEAR(*parseLength(QStringLiteral("320cm")), 400, 1e-9);
    EXPECT_NEAR(*parseLength(QStringLiteral("3200 mm")), 400, 1e-9);
    EXPECT_NEAR(*parseLength(QStringLiteral("80 studs")), 80, 1e-9);
    EXPECT_NEAR(*parseLength(QStringLiteral("2"), LengthUnit::Metres), 250, 1e-9);
    EXPECT_NEAR(*parseLength(QStringLiteral("80"), LengthUnit::Studs), 80, 1e-9);
    for (const char* bad : { "", "abc", "12'6'", "1/0\"", "-3", "12 bananas" })
        EXPECT_FALSE(parseLength(QString::fromUtf8(bad))) << bad;
}

TEST(VenueUnits, ShowsFeetAndInchesToTheQuarterInch) {
    EXPECT_EQ(formatLength(150 * IN), QStringLiteral("12′ 6″"));
    EXPECT_EQ(formatLength(484 * IN), QStringLiteral("40′ 4″"));
    EXPECT_EQ(formatLength(6.5 * IN), QStringLiteral("6½″"));
    EXPECT_EQ(formatLength(946.6 * IN), QStringLiteral("78′ 10½″"));
    EXPECT_EQ(formatLength(12 * IN - 0.01), QStringLiteral("1′ 0″"));
    EXPECT_EQ(formatLength(400, LengthUnit::Metres), QStringLiteral("3.20 m"));
    EXPECT_EQ(formatLength(145.26, LengthUnit::Studs), QStringLiteral("145.3 studs"));
}

TEST(VenueDesign, RoomsAreFourLabelledWallsAndDoorsCutIntoThem) {
    auto v = addRoom(emptyVenue(), { 100, 50 }, { 0, 0 });
    ASSERT_EQ(v.edges.size(), 4);
    EXPECT_EQ(v.edges[0].label, QStringLiteral("north wall"));
    EXPECT_EQ(v.edges[3].label, QStringLiteral("west wall"));
    EXPECT_EQ(v.edges[0].polyline, (QVector<QPointF>{ { 0, 0 }, { 100, 0 } }));
    const auto size = roomSize(v);
    ASSERT_TRUE(size);
    EXPECT_DOUBLE_EQ(size->area, 5000);

    auto wall =
        addEdge(emptyVenue(), { { 0, 0 }, { 100, 0 } }, core::EdgeKind::Wall, QStringLiteral("south wall"));
    wall.edges[0].estimated = true;
    const auto cut = cutOpening(wall, 0, 0, 0.6, 0.2, core::EdgeKind::Door, QStringLiteral("main door"));
    ASSERT_EQ(cut.edges.size(), 3);
    EXPECT_EQ(cut.edges[1].kind, core::EdgeKind::Door);
    EXPECT_NEAR(cut.edges[1].doorWidthStuds, 40, 1e-9);
    EXPECT_EQ(cut.edges[0].polyline.last(), QPointF(20, 0));
    EXPECT_EQ(cut.edges[2].polyline.first(), QPointF(60, 0));
    for (const auto& e : cut.edges) EXPECT_TRUE(e.estimated);
    EXPECT_EQ(cutOpening(wall, 0, 0, 0.5, 1, core::EdgeKind::Open).edges.size(), 2);
}

TEST(VenueDesign, ObstaclesPowerAndCornerMoves) {
    auto v = addObstacle(emptyVenue(), core::ObstacleKind::Stairs, { 10, 0 }, { 0, 20 });
    EXPECT_EQ(v.obstacles[0].upDegrees, 270.0);
    EXPECT_EQ(v.obstacles[0].polygon, (QVector<QPointF>{ { 0, 0 }, { 10, 0 }, { 10, 20 }, { 0, 20 } }));
    v = addObstacle(v, core::ObstacleKind::Railing, { 0, 0 }, { 0, 30 });
    EXPECT_NEAR(bounds(v.obstacles[1].polygon).width(), 4, 1e-9);
    v = resizeObstacle(v, 0, 240 * IN, 180 * IN);
    EXPECT_NEAR(v.obstacles[0].polygon[2].x(), 240 * IN, 1e-9);

    auto room = addRoom(emptyVenue(), { 0, 0 }, { 100, 50 });
    room = moveCorner(room, { 100, 0 }, { 120, 0 });
    EXPECT_EQ(room.edges[0].polyline[1], QPointF(120, 0));
    EXPECT_EQ(room.edges[1].polyline[0], QPointF(120, 0));
    room = addPower(room, { 45, 25 }, true);
    const auto hit = hitTest(room, { 45, 25 }, 2);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->sel, (Selection{ PartKind::Power, 0 }));
}

TEST(VenueDesign, SnappingPrefersCornersThenAngleThenWallsThenGrid) {
    const auto v = addRoom(emptyVenue(), { 0, 0 }, { 100, 50 });
    SnapOptions o{ std::nullopt, IN, 45, 3, true };
    EXPECT_EQ(snapPoint(v, { 101, 1 }, o).kind, SnapKind::Corner);
    auto withFrom = o;
    withFrom.from = QPointF(10, 20);
    const auto a = snapPoint(v, { 30, 20.4 }, withFrom);
    EXPECT_EQ(a.kind, SnapKind::Angle);
    EXPECT_NEAR(a.pt.y(), 20, 1e-9);
    const auto w = snapPoint(v, { 30, 1 }, o);
    EXPECT_EQ(w.kind, SnapKind::Wall);
    EXPECT_NEAR(w.pt.y(), 0, 1e-9);
    EXPECT_EQ(snapPoint(v, { 30.3, 20.3 }, o).kind, SnapKind::Grid);
    const auto p = pointAtLength({ 0, 0 }, { 10, 1 }, 50, 45);
    EXPECT_NEAR(p.x(), 50, 1e-9);
    EXPECT_NEAR(p.y(), 0, 1e-9);
}

TEST(VenueDesignerTools, WallsWithTypedLengthsCloseOnTheFirstCorner) {
    auto s = run(initialState(emptyVenue()), { tool(Tool::Wall), click(0, 0), hover(50, 1),
                                               act::Type{ QStringLiteral("10'") }, act::Enter{} });
    ASSERT_EQ(s.venue().edges.size(), 1);
    EXPECT_NEAR(s.venue().edges[0].polyline[1].x(), 10 * FT, 1e-6);
    s = run(std::move(s), { click(10 * FT, 5 * FT), click(0, 5 * FT), click(0, 0) });
    EXPECT_EQ(s.venue().edges.size(), 4);
    EXPECT_TRUE(s.draft.isEmpty());
}

TEST(VenueDesignerTools, RoomsFromTypedSizesAndDoorsFromTypedWidths) {
    auto s = run(initialState(emptyVenue()), { tool(Tool::Room), click(50 * FT, 10 * FT), hover(0, 20 * FT),
                                               act::Type{ QStringLiteral("40' 4\" x 20'") }, act::Enter{} });
    ASSERT_EQ(s.venue().edges.size(), 4);
    EXPECT_NEAR(bounds(s.venue().edges[0].polyline).left(), 50 * FT - 484 * IN, 1e-6);

    const auto room = addRoom(emptyVenue(), { 0, 0 }, { 40 * FT, 20 * FT });
    s = run(initialState(room), { tool(Tool::Door), click(10 * FT, 0.2), hover(20 * FT, 0),
                                  act::Type{ QStringLiteral("6'") }, act::Enter{} });
    ASSERT_EQ(s.venue().edges.size(), 6);
    EXPECT_EQ(s.venue().edges[1].kind, core::EdgeKind::Door);
    EXPECT_NEAR(s.venue().edges[1].doorWidthStuds, 6 * FT, 1e-6);
    EXPECT_EQ(s.selection, (Selection{ PartKind::Edge, 1 }));
    const auto off = run(initialState(room), { tool(Tool::Opening), click(10 * FT, 10 * FT) });
    EXPECT_FALSE(off.cut);
    EXPECT_EQ(off.message, QStringLiteral("Click on a wall"));
}

TEST(VenueDesignerTools, PlacesAndSelectsObstaclesPowerNotesAndMeasurements) {
    const auto room = addRoom(emptyVenue(), { 0, 0 }, { 40 * FT, 20 * FT });
    auto s = run(initialState(room), { tool(Tool::Stairs), click(10 * FT, 0), click(30 * FT, 15 * FT) });
    EXPECT_EQ(s.venue().obstacles[0].kind, core::ObstacleKind::Stairs);
    EXPECT_EQ(s.selection, (Selection{ PartKind::Obstacle, 0 }));
    s = run(std::move(s), { tool(Tool::Power), click(5 * FT, 0.1), click(5 * FT, 10 * FT) });
    ASSERT_EQ(s.venue().power.size(), 2);
    EXPECT_FALSE(s.venue().power[0].floor);
    EXPECT_TRUE(s.venue().power[1].floor);
    s = run(std::move(s), { tool(Tool::Note), click(2 * FT, 2 * FT) });
    EXPECT_EQ(s.venue().notes[0].text, QStringLiteral("Note"));
    s = run(std::move(s), { tool(Tool::Measure), click(0, 0), click(40 * FT, 0) });
    EXPECT_EQ(s.venue().dimensions[0].label, QStringLiteral("40′ 0″"));
}

TEST(VenueDesignerTools, DragsInWholeInchesAsOneUndoStepAndEscPutsItBack) {
    const auto room = addRoom(emptyVenue(), { 0, 0 }, { 40 * FT, 20 * FT });
    auto s = run(initialState(room),
                 { tool(Tool::Stairs), click(10 * FT, 5 * FT), click(12 * FT, 7 * FT), tool(Tool::Select) });
    s = run(std::move(s), { click(11 * FT, 6 * FT), hover(11 * FT + 3.3, 6 * FT),
                            hover(11 * FT + 10.2, 6 * FT), act::Up{} });
    EXPECT_NEAR(s.venue().obstacles[0].polygon[0].x(), 10 * FT + std::round(10.2 / IN) * IN, 1e-6);
    s = reduce(std::move(s), act::Undo{});
    EXPECT_NEAR(s.venue().obstacles[0].polygon[0].x(), 10 * FT, 1e-6);
    s = run(std::move(s), { click(11 * FT, 6 * FT), hover(20 * FT, 6 * FT), act::Escape{} });
    EXPECT_NEAR(s.venue().obstacles[0].polygon[0].x(), 10 * FT, 1e-6);
    EXPECT_FALSE(s.drag);
    s = run(std::move(s), { click(40 * FT, 0), hover(45 * FT, 0), act::Up{} });
    EXPECT_NEAR(s.venue().edges[0].polyline[1].x(), 45 * FT, 1e-6);
    EXPECT_NEAR(s.venue().edges[1].polyline[0].x(), 45 * FT, 1e-6);
    s = run(std::move(s), { act::Duplicate{}, act::Delete{} });
    EXPECT_EQ(s.venue().edges.size(), 4);
    auto sizes = parseSize(QStringLiteral("40' x 20'"), LengthUnit::FeetInches);
    ASSERT_TRUE(sizes);
    EXPECT_NEAR((*sizes)[1], 240 * IN, 1e-9);
    EXPECT_FALSE(parseSize(QStringLiteral("1' x 2' x 3'"), LengthUnit::FeetInches));
}
