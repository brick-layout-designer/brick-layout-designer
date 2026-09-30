// File › Connect to Server… (sync phase P4) against the in-process HTTP
// server: sign in with the device code, reuse a saved token, sign in again
// when it was revoked, refuse a server we can't talk to, and hand back the
// chosen layout (view-only access included).

#include "ConnectDialog.h"
#include "FakeHttp.h"
#include "TokenStore.h"
#include "ui/help/HelpButton.h"
#include "ui/help/HelpTexts.h"

#include <gtest/gtest.h>

#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
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
    ConnectDialog dialog;

    explicit Harness(ConnectDialog::Purpose purpose = ConnectDialog::Purpose::OpenLayout)
        : dialog(api, tokens, [this](const QUrl& u) { opened << u; }, nullptr, purpose) {
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

TEST(ConnectDialog, DownloadsThePickedVenuesAndSignsInAgainForTheVenueLibrary) {
    Harness h(ConnectDialog::Purpose::DownloadVenues);
    // Signed in before the venue library was reachable: the token lacks
    // venues:read, so the list is refused and sign-in starts again.
    h.tokens.save(h.http.base(), QStringLiteral("bld_pat_old"));
    h.http.reply("/api/venues", 403, { { QStringLiteral("error"), QStringLiteral("insufficient_scope") } });
    h.http.reply("/api/venues", 200,
                 { { QStringLiteral("venues"),
                     QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("v1") },
                                              { QStringLiteral("name"), QStringLiteral("Grand Lobby") },
                                              { QStringLiteral("ownerOrgId"), QStringLiteral("org1") } },
                                 QJsonObject{ { QStringLiteral("id"), QStringLiteral("v2") },
                                              { QStringLiteral("name"), QStringLiteral("Garage") },
                                              { QStringLiteral("ownerOrgId"), QJsonValue::Null } } } } });
    h.http.reply("/api/auth/device/code", 200,
                 { { QStringLiteral("device_code"), QStringLiteral("dev") },
                   { QStringLiteral("user_code"), QStringLiteral("BCDF-GHJK") },
                   { QStringLiteral("verification_uri"), QStringLiteral("https://x.org/device") },
                   { QStringLiteral("expires_in"), 600 },
                   { QStringLiteral("interval"), 1 } });
    h.http.reply("/api/auth/device/token", 200,
                 { { QStringLiteral("access_token"), QStringLiteral("bld_pat_new") } });
    const QJsonObject hall{ { QStringLiteral("name"), QStringLiteral("Grand Lobby") },
                            { QStringLiteral("edges"), QJsonArray{} } };
    const QJsonObject garage{ { QStringLiteral("name"), QStringLiteral("Garage") },
                              { QStringLiteral("edges"), QJsonArray{} } };
    h.http.reply("/api/venues/v1", 200,
                 { { QStringLiteral("id"), QStringLiteral("v1") },
                   { QStringLiteral("name"), QStringLiteral("Grand Lobby") },
                   { QStringLiteral("data"), hall } });
    h.http.reply("/api/venues/v2", 200,
                 { { QStringLiteral("id"), QStringLiteral("v2") },
                   { QStringLiteral("name"), QStringLiteral("Garage") },
                   { QStringLiteral("data"), garage } });

    h.dialog.connectToServer();
    ASSERT_TRUE(waitFor([&] { return !h.opened.isEmpty(); }));
    ASSERT_TRUE(waitFor([&] { return h.listed(); }));
    EXPECT_EQ(h.tokens.tokens.value(TokenStore::keyFor(h.http.base())), QStringLiteral("bld_pat_new"));
    ASSERT_EQ(h.list()->topLevelItemCount(), 2);
    auto* lobby = h.list()->findItems(QStringLiteral("Grand Lobby"), Qt::MatchExactly).value(0);
    ASSERT_TRUE(lobby);
    EXPECT_EQ(lobby->text(1), QStringLiteral("Organisation"));

    // Both picked: both downloaded, then the dialog closes.
    h.list()->selectAll();
    h.dialog.findChild<QPushButton*>(QStringLiteral("open"))->click();
    ASSERT_TRUE(waitFor([&] { return h.dialog.QDialog::result() == QDialog::Accepted; }));
    const auto got = h.dialog.venues();
    ASSERT_EQ(got.size(), 2);
    QStringList names;
    for (const auto& v : got) {
        names << v.name;
        EXPECT_EQ(QJsonDocument::fromJson(v.file).object().value(QLatin1String("schema")).toString(),
                  QStringLiteral("bld-venue/1"));
    }
    names.sort();
    EXPECT_EQ(names, (QStringList{ QStringLiteral("Garage"), QStringLiteral("Grand Lobby") }));
    EXPECT_FALSE(h.dialog.ConnectDialog::result().has_value());
}

TEST(ConnectDialog, PublishesALayoutPersonallyOrToAnOrganisation) {
    Harness h(ConnectDialog::Purpose::Publish);
    h.tokens.save(h.http.base(), QStringLiteral("bld_pat_saved"));
    h.http.reply("/api/orgs", 200,
                 { { QStringLiteral("orgs"),
                     QJsonArray{ QJsonObject{ { QStringLiteral("slug"), QStringLiteral("club") },
                                              { QStringLiteral("name"), QStringLiteral("Train Club") },
                                              { QStringLiteral("myRole"), QStringLiteral("member") } } } } });
    h.http.reply("/api/layouts", 201,
                 { { QStringLiteral("id"), QStringLiteral("L9") },
                   { QStringLiteral("title"), QStringLiteral("Show 2027") } });
    h.dialog.setPublishContent(QByteArrayLiteral("<Map/>"), QByteArrayLiteral("{\"schemaVersion\":1}"),
                               QStringLiteral("Show 2027"));
    h.dialog.connectToServer();
    auto* owner = h.dialog.findChild<QComboBox*>(QStringLiteral("publishOwner"));
    ASSERT_TRUE(waitFor([&] { return owner->count() == 2; }));
    EXPECT_EQ(owner->itemText(1), QStringLiteral("Train Club"));
    owner->setCurrentIndex(1);
    h.dialog.findChild<QPushButton*>(QStringLiteral("publish"))->click();
    ASSERT_TRUE(waitFor([&] { return h.dialog.QDialog::result() == QDialog::Accepted; }));
    const auto body = QJsonDocument::fromJson(h.http.requests.back().body).object();
    EXPECT_EQ(h.http.requests.back().path, QByteArray("/api/layouts"));
    EXPECT_EQ(h.http.requests.back().authorization, QByteArray("Bearer bld_pat_saved"));
    EXPECT_EQ(body.value(QLatin1String("orgSlug")).toString(), QStringLiteral("club"));
    EXPECT_EQ(body.value(QLatin1String("bbm")).toString(), QStringLiteral("<Map/>"));
    EXPECT_EQ(body.value(QLatin1String("sidecar")).toString(), QStringLiteral("{\"schemaVersion\":1}"));
    ASSERT_TRUE(h.dialog.ConnectDialog::result());
    EXPECT_EQ(h.dialog.ConnectDialog::result()->layoutId, QStringLiteral("L9"));
    EXPECT_FALSE(h.dialog.ConnectDialog::result()->readOnly);
}

TEST(ConnectDialog, PublishingWithAnOldSignInSignsInAgain) {
    Harness h(ConnectDialog::Purpose::Publish);
    h.tokens.save(h.http.base(), QStringLiteral("bld_pat_old"));
    h.http.reply("/api/orgs", 200, { { QStringLiteral("orgs"), QJsonArray{} } });
    h.http.reply("/api/layouts", 403, { { QStringLiteral("error"), QStringLiteral("insufficient_scope") } });
    h.http.reply("/api/auth/device/code", 200,
                 { { QStringLiteral("device_code"), QStringLiteral("dev") },
                   { QStringLiteral("user_code"), QStringLiteral("BCDF-GHJK") },
                   { QStringLiteral("verification_uri"), QStringLiteral("https://x.org/device") },
                   { QStringLiteral("expires_in"), 600 },
                   { QStringLiteral("interval"), 1 } });
    h.dialog.setPublishContent(QByteArrayLiteral("<Map/>"), {}, QStringLiteral("x"));
    h.dialog.connectToServer();
    ASSERT_TRUE(waitFor([&] { return h.page() == 3; }));
    h.dialog.publishNow();
    ASSERT_TRUE(waitFor([&] { return !h.opened.isEmpty(); }));
    EXPECT_TRUE(h.tokens.tokens.isEmpty());
    // The sign-in request carries no dead token.
    for (const auto& r : h.http.requests)
        if (r.path == "/api/auth/device/code") EXPECT_TRUE(r.authorization.isEmpty());
}

// The "?"s: the server address, and the owner when publishing.
TEST(ConnectDialog, HasHelpForTheAddressAndTheOwner) {
    Harness h(ConnectDialog::Purpose::Publish);
    QStringList keys;
    bld::ui::help::HelpButton* server = nullptr;
    for (auto* b : h.dialog.findChildren<bld::ui::help::HelpButton*>()) {
        keys << b->key();
        EXPECT_TRUE(bld::ui::help::helpEntry(b->key())) << b->key().toStdString();
        if (b->key() == QLatin1String("connect.server")) server = b;
    }
    EXPECT_TRUE(keys.contains(QStringLiteral("publish.owner")));
    ASSERT_NE(server, nullptr);
    EXPECT_EQ(server->target(), h.dialog.findChild<QLineEdit*>(QStringLiteral("serverAddress")));
}
