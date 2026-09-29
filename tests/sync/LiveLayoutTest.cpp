// LiveLayout (sync phase P4): a server layout shown in the MapView, against
// the in-process y-websocket server. Edits made in the view reach the
// server and leave the local undo stack; Undo goes through the shared
// document; others' changes wait while the mouse is down; view-only
// sessions put edits back; reloads keep the zoom.

#include "ui/LiveLayout.h"
#include "FakeSyncServer.h"

#include "edit/EditCommands.h"
#include "parts/PartsLibrary.h"
#include "ui/MapView.h"

#include <gtest/gtest.h>

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
