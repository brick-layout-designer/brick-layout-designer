// Undo and redo of the ruler, text and venue commands, step by step and as
// a whole stack: every undo brings back exactly what was there (the saved
// .bbm bytes, and the venue in the sidecar), every redo exactly what the
// step made, however often they go back and forth.

#include "edit/RulerCommands.h"
#include "edit/TextCommands.h"
#include "edit/VenueCommands.h"
#include "core/LayerRuler.h"
#include "core/LayerText.h"
#include "core/Map.h"
#include "saveload/BbmWriter.h"
#include "saveload/SidecarIO.h"

#include <gtest/gtest.h>

#include <QBuffer>
#include <QJsonDocument>
#include <QUndoStack>

using namespace bld;

namespace {

// Everything the commands touch: the layout as BlueBrick saves it, and the sidecar.
std::string stateOf(const core::Map& map) {
    QBuffer out;
    out.open(QIODevice::WriteOnly);
    EXPECT_TRUE(saveload::writeBbm(map, out).ok);
    return (out.data() + QJsonDocument(saveload::sidecarToJson(map.sidecar)).toJson(QJsonDocument::Indented)).toStdString();
}

core::LayerRuler::AnyRuler line(const char* guid, QPointF a, QPointF b) {
    core::LayerRuler::AnyRuler r;
    r.linear.guid = QString::fromLatin1(guid);
    r.linear.point1 = a;
    r.linear.point2 = b;
    r.linear.displayArea = QRectF(a, b).normalized();
    return r;
}

core::LayerRuler::AnyRuler circle(const char* guid, QPointF centre, float radius) {
    core::LayerRuler::AnyRuler r;
    r.kind = core::RulerKind::Circular;
    r.circular.guid = QString::fromLatin1(guid);
    r.circular.center = centre;
    r.circular.radius = radius;
    r.circular.displayArea = QRectF(centre.x() - radius, centre.y() - radius, 2 * radius, 2 * radius);
    return r;
}

core::TextCell cell(const char* guid, const char* text, QRectF area) {
    core::TextCell c;
    c.guid = QString::fromLatin1(guid);
    c.text = QString::fromUtf8(text);
    c.displayArea = area;
    return c;
}

// A text sheet (layer 0) with two cells and a ruler sheet (layer 1) with a
// line and a circle.
core::Map sample() {
    core::Map m;
    auto text = std::make_unique<core::LayerText>();
    text->name = QStringLiteral("Labels");
    text->textCells.push_back(cell("t1", "Station", QRectF(0, 0, 20, 10)));
    text->textCells.push_back(cell("t2", "Yard", QRectF(40, 0, 12, 10)));
    m.layers().push_back(std::move(text));
    auto rulers = std::make_unique<core::LayerRuler>();
    rulers->name = QStringLiteral("Rulers");
    rulers->rulers.push_back(line("r1", QPointF(0, 0), QPointF(32, 0)));
    rulers->rulers.push_back(circle("c1", QPointF(10, 10), 8));
    m.layers().push_back(std::move(rulers));
    return m;
}

core::LayerText& texts(core::Map& m) { return static_cast<core::LayerText&>(*m.layers()[0]); }
core::LayerRuler& rulers(core::Map& m) { return static_cast<core::LayerRuler&>(*m.layers()[1]); }

core::Venue hall(const char* name, double width) {
    core::Venue v;
    v.name = QString::fromUtf8(name);
    core::VenueEdge wall;
    wall.polyline = { QPointF(0, 0), QPointF(width, 0), QPointF(width, 200), QPointF(0, 200), QPointF(0, 0) };
    v.edges.push_back(wall);
    core::VenueObstacle pillar;
    pillar.polygon = { QPointF(50, 50), QPointF(60, 50), QPointF(60, 60), QPointF(50, 60) };
    v.obstacles.push_back(pillar);
    return v;
}

// Undo every step back to the start, redo them all, twice: each undo brings
// back the state before its step and each redo the state after it.
void walkTheStack(QUndoStack& stack, const core::Map& map, const std::vector<std::string>& states) {
    ASSERT_EQ(static_cast<int>(states.size()), stack.count() + 1);
    for (int round = 0; round < 2; ++round) {
        for (int i = stack.count(); i > 0; --i) {
            stack.undo();
            EXPECT_EQ(stateOf(map), states[i - 1]) << "undo of step " << i << ", round " << round;
        }
        for (int i = 1; i <= stack.count(); ++i) {
            stack.redo();
            EXPECT_EQ(stateOf(map), states[i]) << "redo of step " << i << ", round " << round;
        }
    }
}

}  // namespace

TEST(AnnotationUndo, RulerCommandsUndoAndRedoExactly) {
    core::Map m = sample();
    QUndoStack stack;
    std::vector<std::string> states{ stateOf(m) };
    const auto step = [&](QUndoCommand* c) {
        stack.push(c);
        states.push_back(stateOf(m));
    };

    // Add a line without an id: it gets one, and undo takes away that one.
    step(new edit::AddRulerItemCommand(m, 1, line("", QPointF(0, 40), QPointF(0, 80))));
    ASSERT_EQ(rulers(m).rulers.size(), 3u);
    const QString added = rulers(m).rulers[2].linear.guid;
    EXPECT_FALSE(added.isEmpty());
    step(new edit::AddRulerItemCommand(m, 1, circle("c2", QPointF(100, 100), 4)));
    step(new edit::MoveRulerItemCommand(m, 1, QStringLiteral("r1"), QPointF(8, -4)));
    EXPECT_EQ(rulers(m).rulers[0].linear.point2, QPointF(40, -4));
    step(new edit::MoveRulerItemCommand(m, 1, QStringLiteral("c1"), QPointF(-2, 6)));
    EXPECT_EQ(rulers(m).rulers[1].circular.center, QPointF(8, 16));
    // An end dragged: the line's box follows.
    step(new edit::MoveRulerEndpointCommand(m, 1, added, 1, QPointF(24, 80)));
    EXPECT_EQ(rulers(m).rulers[2].linear.displayArea, QRectF(0, 40, 24, 40));
    // A circle's rim dragged: its radius and box follow.
    step(new edit::MoveRulerEndpointCommand(m, 1, QStringLiteral("c1"), 1, QPointF(8, 28)));
    EXPECT_FLOAT_EQ(rulers(m).rulers[1].circular.radius, 12);
    EXPECT_EQ(rulers(m).rulers[1].circular.displayArea, QRectF(-4, 4, 24, 24));
    step(new edit::MoveRulerEndpointCommand(m, 1, QStringLiteral("c1"), 0, QPointF(30, 30)));
    step(new edit::ScaleRulerCommand(m, 1, QStringLiteral("r1"), 6));
    edit::EditRulerItemCommand::BaseProps props;
    props.color = core::ColorSpec::fromArgb(QColor(200, 30, 30));
    props.lineThickness = 3;
    props.displayUnit = false;
    props.unit = 2;
    props.guidelineDashPattern = { 2, 1 };
    step(new edit::EditRulerItemCommand(m, 1, QStringLiteral("r1"), props));
    EXPECT_FLOAT_EQ(rulers(m).rulers[0].linear.lineThickness, 3);
    EXPECT_EQ(rulers(m).rulers[0].linear.unit, 2);
    // The middle one deleted: undo puts it back in its place, under the others.
    step(new edit::DeleteRulerItemCommand(m, 1, QStringLiteral("c1")));
    ASSERT_EQ(rulers(m).rulers.size(), 3u);
    step(new edit::DeleteRulerItemCommand(m, 1, QStringLiteral("r1")));
    ASSERT_EQ(rulers(m).rulers.size(), 2u);

    walkTheStack(stack, m, states);
    stack.setIndex(0);
    EXPECT_EQ(stateOf(m), states.front());
    ASSERT_EQ(rulers(m).rulers.size(), 2u);
    EXPECT_EQ(rulers(m).rulers[0].linear.guid, QStringLiteral("r1"));
    EXPECT_EQ(rulers(m).rulers[1].circular.guid, QStringLiteral("c1"));
}

// Something that isn't there (another sheet, an id no ruler has) changes nothing, and neither does its undo.
TEST(AnnotationUndo, RulerCommandsLeaveOtherSheetsAndUnknownIdsAlone) {
    core::Map m = sample();
    const std::string before = stateOf(m);
    QUndoStack stack;
    stack.push(new edit::AddRulerItemCommand(m, 0, line("x", QPointF(), QPointF(1, 1))));
    stack.push(new edit::DeleteRulerItemCommand(m, 1, QStringLiteral("nope")));
    stack.push(new edit::MoveRulerItemCommand(m, 7, QStringLiteral("r1"), QPointF(1, 1)));
    stack.push(new edit::MoveRulerEndpointCommand(m, 1, QStringLiteral("nope"), 0, QPointF(5, 5)));
    EXPECT_EQ(stateOf(m), before);
    stack.setIndex(0);
    EXPECT_EQ(stateOf(m), before);
    stack.setIndex(stack.count());
    EXPECT_EQ(stateOf(m), before);
}

TEST(AnnotationUndo, TextCommandsUndoAndRedoExactly) {
    core::Map m = sample();
    QUndoStack stack;
    std::vector<std::string> states{ stateOf(m) };
    const auto step = [&](QUndoCommand* c) {
        stack.push(c);
        states.push_back(stateOf(m));
    };
    step(new edit::AddTextCellCommand(m, 0, cell("t3", "Depot", QRectF(0, 40, 16, 10))));
    step(new edit::EditTextCellTextCommand(m, 0, QStringLiteral("t1"), QStringLiteral("Main station")));
    EXPECT_EQ(texts(m).textCells[0].text, QStringLiteral("Main station"));
    step(new edit::MoveTextCellCommand(m, 0, QStringLiteral("t2"), QPointF(-8, 16)));
    step(new edit::EditTextCellTextCommand(m, 0, QStringLiteral("t1"), QStringLiteral("Central")));
    // The first one deleted: undo puts it back first.
    step(new edit::DeleteTextCellCommand(m, 0, QStringLiteral("t1")));
    ASSERT_EQ(texts(m).textCells.size(), 2u);
    EXPECT_EQ(texts(m).textCells[0].guid, QStringLiteral("t2"));
    step(new edit::DeleteTextCellCommand(m, 0, QStringLiteral("t3")));

    walkTheStack(stack, m, states);
    stack.setIndex(0);
    ASSERT_EQ(texts(m).textCells.size(), 2u);
    EXPECT_EQ(texts(m).textCells[0].text, QStringLiteral("Station"));
    EXPECT_EQ(texts(m).textCells[1].displayArea, QRectF(40, 0, 12, 10));
}

// A text cell added without an id gets one, as a ruler does, so its undo
// takes away that cell and not another cell that has no id either.
TEST(AnnotationUndo, AddedTextWithoutAnIdUndoesOnlyItself) {
    core::Map m = sample();
    texts(m).textCells.push_back(cell("", "Loose label", QRectF(80, 0, 10, 10)));
    const std::string before = stateOf(m);
    QUndoStack stack;
    auto* add = new edit::AddTextCellCommand(m, 0, cell("", "New", QRectF(0, 80, 10, 10)));
    EXPECT_FALSE(add->cellGuid().isEmpty());
    stack.push(add);
    ASSERT_EQ(texts(m).textCells.size(), 4u);
    stack.undo();
    EXPECT_EQ(stateOf(m), before);
    ASSERT_EQ(texts(m).textCells.size(), 3u);
    EXPECT_EQ(texts(m).textCells[2].text, QStringLiteral("Loose label"));
}

TEST(AnnotationUndo, VenueCommandsSetReplaceAndClearUndoExactly) {
    core::Map m = sample();
    QUndoStack stack;
    std::vector<std::string> states{ stateOf(m) };
    const auto step = [&](QUndoCommand* c) {
        stack.push(c);
        states.push_back(stateOf(m));
    };
    step(new edit::SetVenueCommand(m, hall("Gym", 400)));
    EXPECT_EQ(stack.undoText(), QStringLiteral("Update venue"));
    ASSERT_TRUE(m.sidecar.venue);
    EXPECT_EQ(m.sidecar.venue->name, QStringLiteral("Gym"));
    step(new edit::SetVenueCommand(m, hall("Big hall", 640)));
    step(new edit::SetVenueCommand(m, std::nullopt));
    EXPECT_EQ(stack.undoText(), QStringLiteral("Clear venue"));
    EXPECT_FALSE(m.sidecar.venue);
    step(new edit::SetVenueCommand(m, hall("Gym", 400)));

    walkTheStack(stack, m, states);
    stack.setIndex(2);
    ASSERT_TRUE(m.sidecar.venue);
    EXPECT_EQ(m.sidecar.venue->name, QStringLiteral("Big hall"));
    EXPECT_EQ(m.sidecar.venue->edges[0].polyline[1], QPointF(640, 0));
    stack.setIndex(0);
    EXPECT_FALSE(m.sidecar.venue);
}

// Rulers, text and the venue in one stack, as an editing session makes them.
TEST(AnnotationUndo, AMixedSessionUndoesBackToTheStart) {
    core::Map m = sample();
    const std::string start = stateOf(m);
    QUndoStack stack;
    stack.push(new edit::SetVenueCommand(m, hall("Gym", 400)));
    stack.push(new edit::AddRulerItemCommand(m, 1, line("r9", QPointF(0, 0), QPointF(0, 50))));
    stack.push(new edit::AddTextCellCommand(m, 0, cell("t9", "Entrance", QRectF(0, 0, 30, 10))));
    stack.push(new edit::MoveRulerEndpointCommand(m, 1, QStringLiteral("r9"), 0, QPointF(10, 0)));
    stack.push(new edit::EditTextCellTextCommand(m, 0, QStringLiteral("t9"), QStringLiteral("Way in")));
    stack.push(new edit::SetVenueCommand(m, hall("Gym", 480)));
    stack.push(new edit::DeleteTextCellCommand(m, 0, QStringLiteral("t2")));
    const std::string end = stateOf(m);
    stack.setIndex(0);
    EXPECT_EQ(stateOf(m), start);
    stack.setIndex(stack.count());
    EXPECT_EQ(stateOf(m), end);
}
