// Coming back to a window that lists server things asks the server again:
// the helper itself (only after being away, at most once per gap), the
// Download venues / Open layout list (refilled, keeping what you picked),
// File › Servers… (checks every server again), and the parts recheck rule.

#include "RefreshOnFocus.h"
#include "ConnectDialog.h"
#include "FakeHttp.h"
#include "PartsSync.h"
#include "ServerList.h"
#include "ServersDialog.h"
#include "TokenStore.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QJsonArray>
#include <QSettings>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QWidget>

#include <functional>

using namespace bld::sync;
using bld::synctest::FakeHttp;

namespace {

bool waitFor(const std::function<bool()>& done, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

void leaveAndComeBack(QWidget* w) {
    QEvent away(QEvent::WindowDeactivate);
    QCoreApplication::sendEvent(w, &away);
    QEvent back(QEvent::WindowActivate);
    QCoreApplication::sendEvent(w, &back);
}

int requestsTo(const FakeHttp& http, const QByteArray& path) {
    int n = 0;
    for (const auto& r : http.requests)
        if (r.path == path) ++n;
    return n;
}

QJsonObject venue(const char* id, const char* name) {
    return { { QStringLiteral("id"), QString::fromLatin1(id) },
             { QStringLiteral("name"), QString::fromLatin1(name) },
             { QStringLiteral("ownerOrgId"), QJsonValue::Null } };
}

QJsonObject version() {
    return { { QStringLiteral("version"), QStringLiteral("2.0.0") },
             { QStringLiteral("schemaVersion"), 1 },
             { QStringLiteral("protocols"), QJsonArray{ QStringLiteral("y-websocket/1") } } };
}

} // namespace

TEST(RefreshOnFocus, RefreshesOnlyAfterBeingAwayAndAtMostOncePerGap) {
    QWidget w;
    int calls = 0;
    auto* r = new RefreshOnFocus(&w, [&] { ++calls; }, 0);
    // Opening the window: nothing to refresh yet.
    QEvent open(QEvent::WindowActivate);
    QCoreApplication::sendEvent(&w, &open);
    EXPECT_EQ(calls, 0);
    leaveAndComeBack(&w);
    EXPECT_EQ(calls, 1);
    leaveAndComeBack(&w);
    EXPECT_EQ(calls, 2);
    // Within the gap: no second request.
    r->setMinGap(60'000);
    leaveAndComeBack(&w);
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(r->refreshCount(), 2);
}

TEST(RefreshOnFocus, TheVenueListIsAskedForAgainAndKeepsThePickedVenue) {
    QSettings().setValue(QLatin1String(ServerList::kKey), QByteArray("[]"));
    QSettings().remove(QStringLiteral("sync/ownerFilter"));
    FakeHttp http;
    ServerApi api;
    MemoryTokenStore tokens;
    ConnectDialog dialog(api, tokens, [](const QUrl&) {}, nullptr, ConnectDialog::Purpose::DownloadVenues);
    http.reply("/api/version", 200, version());
    tokens.save(http.base(), QStringLiteral("bld_pat_saved"));
    http.reply("/api/venues", 200,
               { { QStringLiteral("venues"), QJsonArray{ venue("v1", "Grand Lobby") } } });
    // Meanwhile, on the web, someone saves another venue.
    http.reply(
        "/api/venues", 200,
        { { QStringLiteral("venues"), QJsonArray{ venue("v1", "Grand Lobby"), venue("v2", "Garage") } } });
    dialog.setAddress(http.base().toString());
    dialog.connectToServer();
    auto* list = dialog.findChild<QTreeWidget*>(QStringLiteral("layouts"));
    ASSERT_TRUE(waitFor([&] { return list->topLevelItemCount() == 1; }));
    list->topLevelItem(0)->setSelected(true);

    dialog.findChild<RefreshOnFocus*>(QStringLiteral("refreshOnFocus"))->setMinGap(0);
    leaveAndComeBack(&dialog);
    ASSERT_TRUE(waitFor([&] { return list->topLevelItemCount() == 2; }));
    EXPECT_EQ(requestsTo(http, "/api/venues"), 2);
    // Grand Lobby is still the one picked.
    ASSERT_EQ(list->selectedItems().size(), 1);
    EXPECT_EQ(list->selectedItems().first()->text(0), QStringLiteral("Grand Lobby"));
}

TEST(RefreshOnFocus, AfterSigningOutNothingIsAskedFor) {
    QSettings().setValue(QLatin1String(ServerList::kKey), QByteArray("[]"));
    QSettings().remove(QStringLiteral("sync/ownerFilter"));
    FakeHttp http;
    ServerApi api;
    MemoryTokenStore tokens;
    ConnectDialog dialog(api, tokens, [](const QUrl&) {}, nullptr, ConnectDialog::Purpose::DownloadVenues);
    http.reply("/api/version", 200, version());
    tokens.save(http.base(), QStringLiteral("bld_pat_saved"));
    http.reply("/api/venues", 200,
               { { QStringLiteral("venues"), QJsonArray{ venue("v1", "Grand Lobby") } } });
    dialog.setAddress(http.base().toString());
    dialog.connectToServer();
    auto* list = dialog.findChild<QTreeWidget*>(QStringLiteral("layouts"));
    ASSERT_TRUE(waitFor([&] { return list->topLevelItemCount() == 1; }));
    dialog.signOut();
    dialog.findChild<RefreshOnFocus*>(QStringLiteral("refreshOnFocus"))->setMinGap(0);
    leaveAndComeBack(&dialog);
    // Give a stray request time to arrive.
    waitFor([&] { return requestsTo(http, "/api/venues") > 1; }, 500);
    EXPECT_EQ(requestsTo(http, "/api/venues"), 1);
}

TEST(RefreshOnFocus, TheServersWindowChecksEveryServerAgain) {
    FakeHttp club;
    ServerList list;
    list.add(club.base(), QStringLiteral("Train club"));
    list.save();
    club.reply("/api/version", 200, version());
    MemoryTokenStore tokens;
    ServersDialog dialog(tokens, [](const QUrl&) {});
    ASSERT_TRUE(waitFor([&] { return requestsTo(club, "/api/version") == 1; }));
    dialog.findChild<RefreshOnFocus*>(QStringLiteral("refreshOnFocus"))->setMinGap(0);
    leaveAndComeBack(&dialog);
    ASSERT_TRUE(waitFor([&] { return requestsTo(club, "/api/version") == 2; }));
    QSettings().setValue(QLatin1String(ServerList::kKey), QByteArray("[]"));
}

TEST(RefreshOnFocus, ServerPartsAreCheckedAgainAtMostEveryFiveMinutes) {
    EXPECT_FALSE(partsRecheckDue(-1, 10'000'000)); // never synced with this server: connecting decides
    EXPECT_FALSE(partsRecheckDue(1'000'000, 1'000'000 + kPartsRecheckMs - 1));
    EXPECT_TRUE(partsRecheckDue(1'000'000, 1'000'000 + kPartsRecheckMs));
}
