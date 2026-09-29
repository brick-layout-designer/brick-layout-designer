// File › Connect to Server… (sync phase P4) against the in-process HTTP
// server: sign in with the device code, reuse a saved token, sign in again
// when it was revoked, refuse a server we can't talk to, and hand back the
// chosen layout (view-only access included).

#include "ConnectDialog.h"
#include "FakeHttp.h"
#include "TokenStore.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTreeWidget>

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

QJsonObject version(int schema = 1) {
    return { { QStringLiteral("version"), QStringLiteral("2.0.0") },
             { QStringLiteral("schemaVersion"), schema },
             { QStringLiteral("protocols"), QJsonArray{ QStringLiteral("y-websocket/1") } } };
}

QJsonObject layout(const char* id, const char* title, const char* role, const char* org = "") {
    return { { QStringLiteral("id"), QString::fromLatin1(id) },
             { QStringLiteral("title"), QString::fromLatin1(title) },
             { QStringLiteral("role"), QString::fromLatin1(role) },
             { QStringLiteral("ownerOrgName"), QString::fromLatin1(org) },
             { QStringLiteral("updatedAt"), QStringLiteral("2026-09-29T10:00:00.000Z") } };
}

struct Harness {
    FakeHttp http;
    ServerApi api;
    MemoryTokenStore tokens;
    QList<QUrl> opened;
    ConnectDialog dialog{ api, tokens, [this](const QUrl& u) { opened << u; } };

    Harness() {
        api.setPollIntervalScale(5);
        http.reply("/api/version", 200, version());
        dialog.setAddress(http.base().toString());
    }
    QTreeWidget* list() { return dialog.findChild<QTreeWidget*>(QStringLiteral("layouts")); }
    QString message() { return dialog.findChild<QLabel*>(QStringLiteral("message"))->text(); }
    int page() { return dialog.findChild<QStackedWidget*>()->currentIndex(); }
    bool listed() { return page() == 2 && list()->topLevelItemCount() > 0; }
};

} // namespace

TEST(ConnectDialog, SignsInWithTheDeviceCodeAndOpensTheChosenLayout) {
    Harness h;
    h.http.reply("/api/auth/device/code", 200,
                 { { QStringLiteral("device_code"), QStringLiteral("dev") },
                   { QStringLiteral("user_code"), QStringLiteral("BCDF-GHJK") },
                   { QStringLiteral("verification_uri"), QStringLiteral("https://x.org/device") },
                   { QStringLiteral("verification_uri_complete"),
                     QStringLiteral("https://x.org/device?user_code=BCDF-GHJK") },
                   { QStringLiteral("expires_in"), 600 },
                   { QStringLiteral("interval"), 1 } });
    h.http.reply("/api/auth/device/token", 400,
                 { { QStringLiteral("error"), QStringLiteral("authorization_pending") } });
    h.http.reply("/api/auth/device/token", 200,
                 { { QStringLiteral("access_token"), QStringLiteral("bld_pat_new") } });
    h.http.reply(
        "/api/layouts", 200,
        { { QStringLiteral("layouts"), QJsonArray{ layout("L1", "Show 2026", "editor", "Train Club"),
                                                   layout("L2", "Club table", "viewer", "Train Club") } } });

    h.dialog.connectToServer();
    // The code is shown and the browser opened on the sign-in page.
    ASSERT_TRUE(waitFor([&] { return !h.opened.isEmpty(); }));
    EXPECT_EQ(h.opened[0], QUrl(QStringLiteral("https://x.org/device?user_code=BCDF-GHJK")));
    EXPECT_EQ(h.dialog.findChild<QLabel*>(QStringLiteral("userCode"))->text(), QStringLiteral("BCDF-GHJK"));
    EXPECT_EQ(h.page(), 1);

    // Approved: the token is kept for this server and the layouts listed.
    ASSERT_TRUE(waitFor([&] { return h.listed(); }));
    EXPECT_EQ(h.tokens.tokens.value(TokenStore::keyFor(h.http.base())), QStringLiteral("bld_pat_new"));
    EXPECT_EQ(h.http.requests.back().authorization, QByteArray("Bearer bld_pat_new"));
    ASSERT_EQ(h.list()->topLevelItemCount(), 2);

    // Picking the view-only one hands it back as read-only.
    auto* club = h.list()->findItems(QStringLiteral("Club table"), Qt::MatchExactly).value(0);
    ASSERT_TRUE(club);
    EXPECT_EQ(club->text(2), QStringLiteral("View only"));
    h.list()->setCurrentItem(club);
    h.dialog.findChild<QPushButton*>(QStringLiteral("open"))->click();
    ASSERT_TRUE(h.dialog.result());
    EXPECT_EQ(h.dialog.result()->layoutId, QStringLiteral("L2"));
    EXPECT_EQ(h.dialog.result()->title, QStringLiteral("Club table"));
    EXPECT_TRUE(h.dialog.result()->readOnly);
    EXPECT_EQ(h.dialog.result()->token, QStringLiteral("bld_pat_new"));
    EXPECT_EQ(h.dialog.result()->server, h.http.base());
}

TEST(ConnectDialog, ReusesASavedTokenAndSignsInAgainWhenItWasRevoked) {
    {
        Harness h;
        h.tokens.save(h.http.base(), QStringLiteral("bld_pat_saved"));
        h.http.reply("/api/layouts", 200,
                     { { QStringLiteral("layouts"), QJsonArray{ layout("L1", "Show 2026", "owner") } } });
        h.dialog.connectToServer();
        ASSERT_TRUE(waitFor([&] { return h.listed(); }));
        EXPECT_TRUE(h.opened.isEmpty());
        EXPECT_EQ(h.http.requests.back().authorization, QByteArray("Bearer bld_pat_saved"));
        h.list()->setCurrentItem(h.list()->topLevelItem(0));
        h.dialog.findChild<QPushButton*>(QStringLiteral("open"))->click();
        ASSERT_TRUE(h.dialog.result());
        EXPECT_FALSE(h.dialog.result()->readOnly);
    }
    {
        Harness h;
        h.tokens.save(h.http.base(), QStringLiteral("bld_pat_revoked"));
        h.http.reply("/api/layouts", 401, { { QStringLiteral("error"), QStringLiteral("invalid_token") } });
        h.http.reply("/api/auth/device/code", 200,
                     { { QStringLiteral("device_code"), QStringLiteral("dev") },
                       { QStringLiteral("user_code"), QStringLiteral("WXYZ-BCDF") },
                       { QStringLiteral("verification_uri"), QStringLiteral("https://x.org/device") },
                       { QStringLiteral("expires_in"), 600 },
                       { QStringLiteral("interval"), 1 } });
        h.dialog.connectToServer();
        // The dead token is forgotten and sign-in starts.
        ASSERT_TRUE(waitFor([&] { return !h.opened.isEmpty(); }));
        EXPECT_EQ(h.opened[0], QUrl(QStringLiteral("https://x.org/device")));
        EXPECT_TRUE(h.tokens.tokens.isEmpty());
    }
}

TEST(ConnectDialog, RefusesBadAddressesAndServersItCantTalkTo) {
    Harness h;
    h.dialog.setAddress(QStringLiteral("http://layouts.example.org"));
    h.dialog.connectToServer();
    EXPECT_FALSE(h.message().isEmpty());
    EXPECT_TRUE(h.http.requests.empty());

    // A server whose documents are newer than this build reads.
    FakeHttp newer;
    newer.reply("/api/version", 200, version(99));
    h.dialog.setAddress(newer.base().toString());
    h.dialog.connectToServer();
    ASSERT_TRUE(waitFor([&] { return h.message().contains(QStringLiteral("newer")); }));
    EXPECT_EQ(h.page(), 0);
    EXPECT_TRUE(h.opened.isEmpty());
}

TEST(ConnectDialog, SignOutForgetsTheToken) {
    Harness h;
    h.tokens.save(h.http.base(), QStringLiteral("bld_pat_saved"));
    h.http.reply("/api/layouts", 200,
                 { { QStringLiteral("layouts"), QJsonArray{ layout("L1", "Show 2026", "owner") } } });
    h.dialog.connectToServer();
    ASSERT_TRUE(waitFor([&] { return h.listed(); }));
    h.dialog.signOut();
    EXPECT_TRUE(h.tokens.tokens.isEmpty());
    EXPECT_EQ(h.page(), 0);
}
