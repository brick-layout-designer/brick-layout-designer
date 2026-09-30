// SyncSession (sync phase P4): the rules the editor follows while a live
// layout is open, against the in-process y-websocket server.

#include "FakeSyncServer.h"
#include "SyncSession.h"

#include <gtest/gtest.h>

#include <QJsonObject>

using namespace bld;
using namespace bld::synctest;
using namespace std::chrono_literals;
using Status = sync::SyncClient::Status;

TEST(SyncSession, LoadsTheLayoutSendsEditsAndReloadsOnRemoteChanges) {
    FakeServer server;
    sync::SyncSession session;
    int changed = 0;
    QObject::connect(&session, &sync::SyncSession::mapChanged, [&] { ++changed; });
    EXPECT_FALSE(session.loaded());
    session.open(server.url(), QStringLiteral("bld_pat_test"), false);

    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    EXPECT_TRUE(session.loaded());
    EXPECT_GT(changed, 0);
    auto map = session.currentMap();
    ASSERT_TRUE(map);
    EXPECT_EQ(firstBrick(*map).displayArea, firstBrickArea(server.doc));

    // Our edit reaches the server.
    firstBrick(*map).displayArea.translate(8, 0);
    session.localEdit(*map);
    ASSERT_TRUE(waitFor([&] { return firstBrickArea(server.doc) == firstBrick(*map).displayArea; }));

    // Someone else's edit: the editor is told to reload, and sees it.
    const int before = changed;
    server.remoteEdit([](core::Map& m) { firstBrick(m).orientation = 90.0f; });
    ASSERT_TRUE(waitFor([&] { return changed > before; }));
    EXPECT_EQ(firstBrick(*session.currentMap()).orientation, 90.0f);
}

TEST(SyncSession, RemoteChangesWaitWhileHeld) {
    FakeServer server;
    sync::SyncSession session;
    int changed = 0;
    QObject::connect(&session, &sync::SyncSession::mapChanged, [&] { ++changed; });
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));

    // Mid-drag: the change lands in the document but the editor isn't
    // told until the drag ends.
    session.holdRemoteChanges(true);
    const int before = changed;
    server.remoteEdit([](core::Map& m) { firstBrick(m).orientation = 180.0f; });
    ASSERT_TRUE(waitFor([&] { return firstBrick(*session.currentMap()).orientation == 180.0f; }));
    waitFor([] { return false; }, 100);
    EXPECT_EQ(changed, before);
    session.holdRemoteChanges(false);
    EXPECT_EQ(changed, before + 1);
    // Nothing pending: releasing again says nothing.
    session.holdRemoteChanges(true);
    session.holdRemoteChanges(false);
    EXPECT_EQ(changed, before + 1);
}

TEST(SyncSession, UndoRevertsOnlyOurOwnEditOnTheServerToo) {
    FakeServer server;
    sync::SyncSession session;
    int changed = 0;
    QObject::connect(&session, &sync::SyncSession::mapChanged, [&] { ++changed; });
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    const QRectF original = firstBrickArea(server.doc);

    EXPECT_FALSE(session.canUndo());
    auto map = session.currentMap();
    firstBrick(*map).displayArea.translate(0, 16);
    session.localEdit(*map);
    ASSERT_TRUE(waitFor([&] { return firstBrickArea(server.doc) != original; }));
    server.remoteEdit([](core::Map& m) { firstBrick(m).orientation = 270.0f; });
    ASSERT_TRUE(waitFor([&] { return firstBrick(*session.currentMap()).orientation == 270.0f; }));

    ASSERT_TRUE(session.canUndo());
    const int before = changed;
    EXPECT_TRUE(session.undo());
    EXPECT_EQ(changed, before + 1);
    // Our move is undone everywhere; their rotation stays.
    ASSERT_TRUE(waitFor([&] { return firstBrickArea(server.doc) == original; }));
    EXPECT_EQ(firstBrick(*sync::mapFromDocJson(server.doc.toJson())).orientation, 270.0f);
    EXPECT_EQ(firstBrick(*session.currentMap()).displayArea, original);

    EXPECT_TRUE(session.redo());
    ASSERT_TRUE(waitFor([&] { return firstBrickArea(server.doc) != original; }));
}

TEST(SyncSession, ReadOnlySessionsNeverSend) {
    FakeServer server;
    sync::SyncSession session;
    session.open(server.url(), {}, true);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    EXPECT_TRUE(session.readOnly());

    const QJsonObject before = server.doc.toJson();
    auto map = session.currentMap();
    firstBrick(*map).displayArea.translate(40, 40);
    session.localEdit(*map);
    EXPECT_FALSE(session.canUndo());
    EXPECT_FALSE(session.undo());
    waitFor([] { return false; }, 150);
    EXPECT_EQ(server.doc.toJson(), before);
    // Our copy is unchanged too: the next reload shows the server's layout.
    EXPECT_EQ(firstBrick(*session.currentMap()).displayArea, firstBrickArea(server.doc));
}

TEST(SyncSession, CountsEditsMadeOfflineUntilTheyAreDelivered) {
    FakeServer server;
    sync::SyncSession session;
    session.client().setReconnectDelays(50ms, 200ms);
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));

    server.dropAll(QWebSocketProtocol::CloseCodeGoingAway);
    ASSERT_TRUE(waitFor([&] { return session.status() != Status::Synced; }));
    auto map = session.currentMap();
    firstBrick(*map).displayArea.translate(0, 8);
    session.localEdit(*map);
    firstBrick(*map).displayArea.translate(0, 8);
    session.localEdit(*map);
    EXPECT_EQ(session.unsyncedEdits(), 2);

    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    EXPECT_EQ(session.unsyncedEdits(), 0);
    EXPECT_EQ(firstBrickArea(server.doc), firstBrick(*map).displayArea);
}

TEST(SyncSession, TellsTheEditorWhenTheServerEndsIt) {
    FakeServer server;
    sync::SyncSession session;
    int code = 0;
    QObject::connect(&session, &sync::SyncSession::ended, [&](int c, const QString&) { code = c; });
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    server.dropAll(QWebSocketProtocol::CloseCodePolicyViolated, QStringLiteral("access_revoked"));
    ASSERT_TRUE(waitFor([&] { return code != 0; }));
    EXPECT_EQ(code, static_cast<int>(QWebSocketProtocol::CloseCodePolicyViolated));
}

TEST(SyncSession, SharesPresenceWithEveryoneElseOnTheLayout) {
    FakeServer server;
    sync::SyncSession a, b;
    a.client().setReconnectDelays(50ms, 200ms);
    int changes = 0;
    QObject::connect(&b, &sync::SyncSession::peersChanged, [&] { ++changes; });
    a.open(server.url(), {}, false);
    b.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return a.status() == Status::Synced && b.status() == Status::Synced; }));
    EXPECT_NE(a.clientId(), b.clientId());

    const QJsonObject alice{
        { QStringLiteral("user"), QJsonObject{ { QStringLiteral("name"), QStringLiteral("Alice") } } },
        { QStringLiteral("cursor"), QJsonObject{ { QStringLiteral("x"), 10 }, { QStringLiteral("y"), 20 } } }
    };
    a.setPresence(alice);
    ASSERT_TRUE(waitFor([&] { return b.peers().contains(a.clientId()); }));
    EXPECT_EQ(b.peers().value(a.clientId()), alice);
    EXPECT_TRUE(a.peers().isEmpty()); // not our own
    EXPECT_GT(changes, 0);

    // After a dropped connection A says where it is again.
    server.dropAll(QWebSocketProtocol::CloseCodeGoingAway);
    ASSERT_TRUE(waitFor([&] { return b.peers().isEmpty(); })); // B went offline too: nobody shown
    ASSERT_TRUE(waitFor([&] { return b.peers().contains(a.clientId()); }, 8000));

    // Leaving takes A off B's map.
    a.setPresence(std::nullopt);
    ASSERT_TRUE(waitFor([&] { return b.peers().isEmpty(); }));
}
