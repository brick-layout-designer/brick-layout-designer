// LiveLayout (sync phase P4): a server layout shown in the MapView, against
// the in-process y-websocket server. Edits made in the view reach the
// server and leave the local undo stack; Undo goes through the shared
// document; others' changes wait while the mouse is down; view-only
// sessions put edits back; reloads keep the zoom.

#include "ui/LiveLayout.h"
#include "ui/ModuleEditBar.h"
#include "FakeSyncServer.h"

#include "edit/EditCommands.h"
#include "edit/ModuleCommands.h"
#include "core/ModuleEdit.h"
#include "parts/PartsLibrary.h"
#include "ui/MapView.h"

#include <gtest/gtest.h>

#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QMouseEvent>
#include <QUndoStack>

using namespace bld;
using namespace bld::synctest;
using namespace std::chrono_literals;
using Status = sync::SyncClient::Status;

namespace {

edit::BrickRef firstBrickRef(core::Map& m) {
    for (int i = 0; i < static_cast<int>(m.layers().size()); ++i)
        if (m.layers()[i]->kind() == core::LayerKind::Brick)
            return { i, static_cast<core::LayerBrick&>(*m.layers()[i]).bricks[0].guid };
    return {};
}

// Move the first brick by `dx` studs through the view's undo stack, as a drag does.
void moveFirstBrick(ui::MapView& view, double dx) {
    core::Map& m = *view.currentMap();
    const QPointF at = firstBrick(m).displayArea.topLeft();
    view.undoStack()->push(new edit::MoveBricksCommand(m, { { firstBrickRef(m), at, at + QPointF(dx, 0) } }));
}

void mouse(ui::MapView& view, QEvent::Type type) {
    QMouseEvent e(type, QPointF(10, 10), view.viewport()->mapToGlobal(QPointF(10, 10)), Qt::LeftButton,
                  type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view.viewport(), &e);
}

struct Harness {
    FakeServer server;
    parts::PartsLibrary parts;
    ui::MapView view{ parts };
    ui::LiveLayout live{ view };
    int reloads = 0;

    Harness() {
        QObject::connect(&live, &ui::LiveLayout::mapReloaded, [this] { ++reloads; });
    }
    void open(bool readOnly = false) {
        live.open(server.url(), QStringLiteral("bld_pat_test"), readOnly, QStringLiteral("Show 2026"));
        ASSERT_TRUE(waitFor([&] { return live.session().status() == Status::Synced && view.currentMap(); }));
    }
};

} // namespace

TEST(LiveLayout, EditsInTheViewReachTheServerAndUndoGoesThroughIt) {
    Harness h;
    h.open();
    EXPECT_EQ(h.live.statusText(), QStringLiteral("Connected"));
    const QRectF original = firstBrickArea(h.server.doc);
    EXPECT_EQ(firstBrick(*h.view.currentMap()).displayArea, original);

    moveFirstBrick(h.view, 8);
    ASSERT_TRUE(waitFor([&] { return firstBrickArea(h.server.doc) == original.translated(8, 0); }));
    // The step left the local stack; Undo is the live one now.
    ASSERT_TRUE(waitFor([&] { return h.view.undoStack()->count() == 0; }));
    ASSERT_TRUE(h.live.canUndo());

    const int before = h.reloads;
    EXPECT_TRUE(h.live.undo());
    EXPECT_EQ(h.reloads, before + 1);
    EXPECT_EQ(firstBrick(*h.view.currentMap()).displayArea, original);
    ASSERT_TRUE(waitFor([&] { return firstBrickArea(h.server.doc) == original; }));
}

TEST(LiveLayout, OthersChangesWaitWhileTheMouseIsDownAndKeepTheZoom) {
    Harness h;
    h.open();
    h.view.scale(3, 3);
    const QTransform zoom = h.view.transform();

    mouse(h.view, QEvent::MouseButtonPress);
    const int before = h.reloads;
    h.server.remoteEdit([](core::Map& m) { firstBrick(m).orientation = 90.0f; });
    ASSERT_TRUE(waitFor([&] { return firstBrick(*h.live.session().currentMap()).orientation == 90.0f; }));
    waitFor([] { return false; }, 100);
    EXPECT_EQ(h.reloads, before);

    mouse(h.view, QEvent::MouseButtonRelease);
    ASSERT_TRUE(waitFor([&] { return h.reloads == before + 1; }));
    EXPECT_EQ(firstBrick(*h.view.currentMap()).orientation, 90.0f);
    EXPECT_EQ(h.view.transform(), zoom);
}

TEST(LiveLayout, ViewOnlyPutsEditsBackAndSendsNothing) {
    Harness h;
    h.open(true);
    EXPECT_EQ(h.live.statusText(), QStringLiteral("Connected, view only"));
    const QJsonObject before = h.server.doc.toJson();
    const QRectF original = firstBrick(*h.view.currentMap()).displayArea;

    // A menu command still edits the map; it is put back.
    moveFirstBrick(h.view, 8);
    ASSERT_TRUE(waitFor([&] { return firstBrick(*h.view.currentMap()).displayArea == original; }));
    waitFor([] { return false; }, 100);
    EXPECT_EQ(h.server.doc.toJson(), before);
    EXPECT_FALSE(h.live.canUndo());
}

TEST(LiveLayout, ClosingLeavesTheMapAndStopsFollowingTheServer) {
    Harness h;
    h.open();
    h.live.close();
    EXPECT_FALSE(h.live.active());
    EXPECT_TRUE(h.live.statusText().isEmpty());
    const int before = h.reloads;
    moveFirstBrick(h.view, 8);
    // An ordinary local edit again: it stays on the stack.
    waitFor([] { return false; }, 100);
    EXPECT_EQ(h.view.undoStack()->count(), 1);
    EXPECT_EQ(h.reloads, before);
    EXPECT_NE(firstBrickArea(h.server.doc), firstBrick(*h.view.currentMap()).displayArea);
}

// Edit module in presence: which module someone edits goes both ways, and
// the bar names whoever else is in the module we edit.
TEST(LiveLayout, SharesWhoIsEditingWhichModule) {
    Harness h;
    h.open();
    // A module, made as an edit so it reaches the shared layout.
    auto* create = new edit::CreateModuleCommand(*h.view.currentMap(), QStringLiteral("Harbour"),
                                                 { { 0, firstBrick(*h.view.currentMap()).guid } });
    const QString id = create->moduleId();
    h.view.undoStack()->push(create);
    ASSERT_TRUE(waitFor([&] { return core::findModule(h.view.currentMap()->sidecar.modules, id) != nullptr; }));
    sync::SyncSession other;
    other.open(h.server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return other.status() == Status::Synced; }));
    other.setPresence(sync::presence::state({ QStringLiteral("u-sam"), QStringLiteral("Sam"), QStringLiteral("#60a5fa") },
                                            QPointF(10, 20), {}, 0, id));
    ASSERT_TRUE(waitFor([&] { return h.live.drawnPeers() == 1; }));
    h.view.setEditingModule(id);
    ASSERT_NE(h.view.moduleEditBar(), nullptr);
    EXPECT_EQ(h.view.moduleEditBar()->others(), QStringLiteral("Sam is here too"));
    ASSERT_TRUE(waitFor([&] {
        for (const auto& s : other.peers())
            if (sync::presence::peerFrom(s).editingModule == id) return true;
        return false;
    }));
    h.view.setEditingModule({});
    ASSERT_TRUE(waitFor([&] {
        for (const auto& s : other.peers())
            if (!sync::presence::peerFrom(s).editingModule.isEmpty()) return false;
        return true;
    }));
}

TEST(LiveLayout, ShowsOtherPeoplesCursorsAndSendsOurs) {
    Harness h;
    h.open();
    // Someone else on the same layout.
    sync::SyncSession other;
    other.open(h.server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return other.status() == Status::Synced; }));
    const QString brick = firstBrick(*h.view.currentMap()).guid;
    other.setPresence(
        sync::presence::state({ QStringLiteral("u-bob"), QStringLiteral("Bob"), QStringLiteral("#60a5fa") },
                              QPointF(10, 20), { brick }, 0));
    ASSERT_TRUE(waitFor([&] { return h.live.drawnPeers() == 1; }));
    QStringList names;
    int outlines = 0;
    for (QGraphicsItem* it : h.view.scene()->items()) {
        if (auto* t = dynamic_cast<QGraphicsSimpleTextItem*>(it)) names << t->text();
        if (auto* r = dynamic_cast<QGraphicsRectItem*>(it);
            r && r->pen().style() == Qt::DashLine && r->zValue() >= 1e9)
            ++outlines;
    }
    EXPECT_TRUE(names.contains(QStringLiteral("Bob")));
    EXPECT_EQ(outlines, 1); // Bob's selected brick

    // Our cursor and name reach them, in the web's shape and colour.
    h.live.setUser(QStringLiteral("u-alice"), QStringLiteral("Alice"), QStringLiteral("L1"));
    QMouseEvent move(QEvent::MouseMove, QPointF(30, 30), h.view.viewport()->mapToGlobal(QPointF(30, 30)),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(h.view.viewport(), &move);
    ASSERT_TRUE(waitFor([&] {
        for (const auto& s : other.peers()) {
            const auto p = sync::presence::peerFrom(s);
            if (p.name == QLatin1String("Alice") && p.cursor) return true;
        }
        return false;
    }));
    const auto alice = sync::presence::peerFrom(other.peers().begin().value());
    EXPECT_EQ(alice.color, sync::presence::colorFor(QStringLiteral("u-alice"), QStringLiteral("L1")));

    // They leave: their cursor goes.
    other.setPresence(std::nullopt);
    ASSERT_TRUE(waitFor([&] { return h.live.drawnPeers() == 0; }));
    // We close: nothing of theirs is left on the map.
    h.live.close();
    EXPECT_EQ(h.live.drawnPeers(), 0);
}
