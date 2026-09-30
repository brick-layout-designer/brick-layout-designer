// SyncSession (sync phase P4): the rules the editor follows while a live
// layout is open, against the in-process y-websocket server.

#include "FakeSyncServer.h"
#include "SyncSession.h"

#include <gtest/gtest.h>

#include <QJsonObject>
#include <QTemporaryDir>

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

TEST(SyncSession, KeepsOfflineEditsApartUntilTheyAreResolved) {
    FakeServer server;
    sync::SyncSession session;
    session.client().setReconnectDelays(50ms, 200ms);
    int ready = 0, changed = 0;
    QObject::connect(&session, &sync::SyncSession::offlineEditsReady, [&] { ++ready; });
    QObject::connect(&session, &sync::SyncSession::mapChanged, [&] { ++changed; });
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    const QRectF original = firstBrickArea(server.doc);

    server.dropAll(QWebSocketProtocol::CloseCodeGoingAway);
    ASSERT_TRUE(waitFor([&] { return session.status() != Status::Synced; }));
    auto map = session.currentMap();
    firstBrick(*map).displayArea.translate(0, 8);
    session.localEdit(*map);
    firstBrick(*map).displayArea.translate(0, 8);
    session.localEdit(*map);
    EXPECT_EQ(session.unsyncedEdits(), 2);
    ASSERT_TRUE(session.offlineEdits());
    EXPECT_EQ(firstBrick(*sync::merge::mapOf(session.offlineEdits()->base)).displayArea, original);
    EXPECT_FALSE(session.canUndo());

    // Back in step: nothing is merged, the editor is asked instead.
    ASSERT_TRUE(waitFor([&] { return ready == 1; }));
    EXPECT_EQ(session.status(), Status::Synced);
    EXPECT_EQ(firstBrickArea(server.doc), original);
    EXPECT_EQ(session.unsyncedEdits(), 2);
    // Someone else's edit meanwhile: the editor keeps showing mine.
    const int before = changed;
    server.remoteEdit([](core::Map& m) { firstBrick(m).orientation = 90.0f; });
    ASSERT_TRUE(waitFor([&] { return firstBrick(*session.currentMap()).orientation == 90.0f; }));
    EXPECT_EQ(changed, before);
    EXPECT_EQ(firstBrick(*session.editorMap()).displayArea, firstBrick(*map).displayArea);
    // A further edit while they wait is still an offline edit.
    session.localEdit(*map);
    EXPECT_EQ(session.unsyncedEdits(), 3);
    EXPECT_EQ(firstBrickArea(server.doc), original);

    // Resolved: the result goes in as an ordinary edit.
    auto merged = session.currentMap();
    firstBrick(*merged).displayArea = firstBrick(*map).displayArea;
    session.resolveOffline(merged.get());
    EXPECT_FALSE(session.offlineEdits());
    EXPECT_EQ(session.unsyncedEdits(), 0);
    EXPECT_EQ(changed, before + 1);
    ASSERT_TRUE(waitFor([&] { return firstBrickArea(server.doc) == firstBrick(*map).displayArea; }));
    EXPECT_EQ(firstBrick(*sync::mapFromDocJson(server.doc.toJson())).orientation, 90.0f);
    EXPECT_TRUE(session.canUndo());
}

TEST(SyncSession, DiscardingOfflineEditsLeavesTheServerAlone) {
    FakeServer server;
    sync::SyncSession session;
    session.client().setReconnectDelays(50ms, 200ms);
    session.open(server.url(), {}, false);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    const QRectF original = firstBrickArea(server.doc);
    server.dropAll(QWebSocketProtocol::CloseCodeGoingAway);
    ASSERT_TRUE(waitFor([&] { return session.status() != Status::Synced; }));
    auto map = session.currentMap();
    firstBrick(*map).displayArea.translate(8, 0);
    session.localEdit(*map);
    ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
    session.resolveOffline(nullptr);
    EXPECT_FALSE(session.offlineEdits());
    EXPECT_EQ(firstBrick(*session.editorMap()).displayArea, original);
    waitFor([] { return false; }, 100);
    EXPECT_EQ(firstBrickArea(server.doc), original);
}

TEST(SyncSession, TheCacheOpensTheLayoutOfflineAndKeepsOfflineEdits) {
    QTemporaryDir cache;
    QRectF moved;
    {
        FakeServer server;
        sync::SyncSession session;
        session.setCacheDir(cache.path());
        session.client().setReconnectDelays(50ms, 200ms);
        session.open(server.url(), {}, false);
        ASSERT_TRUE(waitFor([&] { return session.status() == Status::Synced; }));
        server.remoteEdit([](core::Map& m) { firstBrick(m).orientation = 45.0f; });
        ASSERT_TRUE(waitFor([&] { return firstBrick(*session.currentMap()).orientation == 45.0f; }));
        server.dropAll(QWebSocketProtocol::CloseCodeGoingAway);
        ASSERT_TRUE(waitFor([&] { return session.status() != Status::Synced; }));
        auto map = session.currentMap();
        firstBrick(*map).displayArea.translate(0, 24);
        moved = firstBrick(*map).displayArea;
        session.localEdit(*map);
        session.close();  // then the app crashes, say
    }
    // No server now: the layout opens from the cache, offline edits and all.
    sync::SyncSession session;
    session.setCacheDir(cache.path());
    int changed = 0;
    QObject::connect(&session, &sync::SyncSession::mapChanged, [&] { ++changed; });
    session.open(QUrl(QStringLiteral("ws://127.0.0.1:1/ws/layout/L1")), {}, false);
    EXPECT_TRUE(session.loaded());
    ASSERT_TRUE(waitFor([&] { return changed > 0; }));
    EXPECT_EQ(firstBrick(*session.currentMap()).orientation, 45.0f);
    ASSERT_TRUE(session.offlineEdits());
    EXPECT_EQ(session.unsyncedEdits(), 1);
    EXPECT_EQ(firstBrick(*session.editorMap()).displayArea, moved);
    session.close();
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
    b.client().setReconnectDelays(80ms, 200ms);
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
