// File › Connect to Server… (sync phase P4) against the in-process HTTP
// server: sign in with the device code, reuse a saved token, sign in again
// when it was revoked, refuse a server we can't talk to, and hand back the
// chosen layout (view-only access included).

#include "ConnectDialog.h"
#include "ServerList.h"
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

#include <algorithm>
#include <functional>
#include <memory>

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

// No Show filter and no servers from an earlier test.
void resetSettings() {
    QSettings s;
    s.remove(QStringLiteral("sync/ownerFilter"));
    s.remove(QStringLiteral("sync/serverAddress"));
    s.setValue(QLatin1String(ServerList::kKey), QByteArray("[]"));
}

struct Harness {
    FakeHttp http;
    ServerApi api;
    MemoryTokenStore tokens;
    QList<QUrl> opened;
    ConnectDialog dialog;

    explicit Harness(ConnectDialog::Purpose purpose = ConnectDialog::Purpose::OpenLayout)
        : dialog((resetSettings(), api), tokens,
                 [this](const QUrl& u) { opened << u; }, nullptr, purpose) {
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

TEST(ConnectDialog, ShowsWhereTheSourceIsOnEveryPage) {
    Harness h;
    h.dialog.show();
    auto* links = h.dialog.findChild<QLabel*>(QStringLiteral("sourceLinks"));
    ASSERT_NE(links, nullptr);
    EXPECT_TRUE(links->isVisible());
    EXPECT_TRUE(links->openExternalLinks());  // the system browser
    EXPECT_TRUE(links->text().contains(QStringLiteral("href=\"https://github.com/brick-layout-designer/brick-layout-designer\"")));
    EXPECT_TRUE(links->text().contains(
        QStringLiteral("href=\"https://github.com/brick-layout-designer/collaborative-brick-layout-designer\"")));
    // Still there on the sign-in code page.
    h.dialog.findChild<QStackedWidget*>()->setCurrentIndex(1);
    EXPECT_TRUE(links->isVisible());
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

TEST(ConnectDialog, AnAppTooOldForTheServerIsAskedToUpdateBeforeSigningIn) {
    const QString before = QCoreApplication::applicationVersion();
    QCoreApplication::setApplicationVersion(QStringLiteral("1.1.0"));
    Harness h;
    QJsonObject v = version();
    v.insert(QStringLiteral("desktop"), QJsonObject{ { QStringLiteral("minimum"), QStringLiteral("1.2.0") },
                                                     { QStringLiteral("recommended"), QStringLiteral("1.3.0") },
                                                     { QStringLiteral("downloadUrl"), QStringLiteral("https://example.org/get") } });
    h.http.clear("/api/version");
    h.http.reply("/api/version", 200, v);
    h.dialog.connectToServer();
    ASSERT_TRUE(waitFor([&] { return h.message().contains(QStringLiteral("1.2.0")); }));
    QCoreApplication::setApplicationVersion(before);
    EXPECT_EQ(h.message(), QStringLiteral("This server needs Brick Layout Designer 1.2.0 or newer, and you have 1.1.0. "
                                          "Download the new version, install it, then connect again."));
    // Nothing signed in or fetched: only the version was asked.
    EXPECT_EQ(h.page(), 0);
    EXPECT_TRUE(h.opened.isEmpty());
    for (const auto& r : h.http.requests) EXPECT_EQ(r.path, QByteArray("/api/version"));
    auto* update = h.dialog.findChild<QPushButton*>(QStringLiteral("downloadUpdate"));
    ASSERT_NE(update, nullptr);
    EXPECT_FALSE(update->isHidden());
    update->click();
    ASSERT_EQ(h.opened.size(), 1);
    EXPECT_EQ(h.opened.first(), QUrl(QStringLiteral("https://example.org/get")));
}

TEST(ConnectDialog, VenuesFromAServerWithoutAVenueLibrarySaySo) {
    Harness h(ConnectDialog::Purpose::DownloadVenues);
    QJsonObject v = version();
    v.insert(QStringLiteral("features"), QJsonArray{ QStringLiteral("liveSync"), QStringLiteral("signIn") });
    h.http.clear("/api/version");
    h.http.reply("/api/version", 200, v);
    h.tokens.save(h.http.base(), QStringLiteral("bld_pat_saved"));
    h.dialog.connectToServer();
    ASSERT_TRUE(waitFor([&] { return h.message().contains(QStringLiteral("venue library")); }));
    for (const auto& r : h.http.requests) EXPECT_EQ(r.path, QByteArray("/api/version"));
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

// Clubs you can find and join are on the server's Clubs page, in the browser.
TEST(ConnectDialog, FindAClubOpensTheServersClubsPage) {
    Harness h;
    h.tokens.save(h.http.base(), QStringLiteral("bld_pat_saved"));
    h.http.reply("/api/layouts", 200,
                 { { QStringLiteral("layouts"), QJsonArray{ layout("L1", "Show 2026", "owner") } } });
    h.dialog.connectToServer();
    ASSERT_TRUE(waitFor([&] { return h.listed(); }));
    auto* find = h.dialog.findChild<QPushButton*>(QStringLiteral("findClub"));
    ASSERT_NE(find, nullptr);
    find->click();
    ASSERT_EQ(h.opened.size(), 1);
    QUrl expected = h.http.base();
    expected.setPath(QStringLiteral("/orgs"));
    expected.setFragment(QStringLiteral("find"));
    EXPECT_EQ(h.opened.first(), expected);
    // Nothing else happened: still on the list, still signed in.
    EXPECT_EQ(h.page(), 2);
    EXPECT_FALSE(h.tokens.tokens.isEmpty());
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
    // A server from before owner tags names no club.
    EXPECT_EQ(lobby->text(1), QStringLiteral("Club"));

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

namespace {
QJsonObject venue(const char* id, const char* name, const char* orgSlug = "", const char* orgName = "") {
    const bool club = *orgSlug != 0;
    return { { QStringLiteral("id"), QString::fromLatin1(id) },
             { QStringLiteral("name"), QString::fromLatin1(name) },
             { QStringLiteral("ownerOrgId"), club ? QJsonValue(QStringLiteral("id-") + QString::fromLatin1(orgSlug))
                                                  : QJsonValue(QJsonValue::Null) },
             { QStringLiteral("ownerOrgSlug"), club ? QJsonValue(QString::fromLatin1(orgSlug)) : QJsonValue() },
             { QStringLiteral("ownerOrgName"), club ? QJsonValue(QString::fromUtf8(orgName)) : QJsonValue() } };
}
QStringList shown(QTreeWidget* list) {
    QStringList out;
    for (int i = 0; i < list->topLevelItemCount(); ++i)
        if (!list->topLevelItem(i)->isHidden()) out << list->topLevelItem(i)->text(0);
    out.sort();
    return out;
}
} // namespace

// Yours and your clubs' things together: each venue says who owns it, and
// Show narrows the list to Mine or one club; the choice is kept for next time.
TEST(ConnectDialog, MarksEachVenuesOwnerAndShowsMineOrOneClub) {
    {
        Harness h(ConnectDialog::Purpose::DownloadVenues);
        h.tokens.save(h.http.base(), QStringLiteral("bld_pat_saved"));
        h.http.reply("/api/venues", 200,
                     { { QStringLiteral("venues"),
                         QJsonArray{ venue("v1", "Grand Lobby", "club", "Train Club"), venue("v2", "Garage"),
                                     venue("v3", "Gym", "other", "Other Club") } } });
        h.dialog.connectToServer();
        ASSERT_TRUE(waitFor([&] { return h.listed(); }));
        auto owner = [&](const char* name) {
            return h.list()->findItems(QString::fromLatin1(name), Qt::MatchExactly).value(0)->text(1);
        };
        EXPECT_EQ(owner("Grand Lobby"), QStringLiteral("Train Club"));
        EXPECT_EQ(owner("Garage"), QStringLiteral("Me"));

        auto* show = h.dialog.findChild<QComboBox*>(QStringLiteral("ownerFilter"));
        ASSERT_TRUE(show);
        QStringList choices;
        for (int i = 0; i < show->count(); ++i) choices << show->itemText(i);
        EXPECT_EQ(choices, (QStringList{ QStringLiteral("All"), QStringLiteral("Mine"), QStringLiteral("Train Club"),
                                         QStringLiteral("Other Club") }));
        EXPECT_EQ(shown(h.list()).size(), 3);

        show->setCurrentIndex(1);
        emit show->activated(1);
        EXPECT_EQ(shown(h.list()), QStringList{ QStringLiteral("Garage") });
        show->setCurrentIndex(2);
        emit show->activated(2);
        EXPECT_EQ(shown(h.list()), QStringList{ QStringLiteral("Grand Lobby") });
    }
    // Kept for next time: the next list opens on the same club…
    QSettings().setValue(QStringLiteral("sync/ownerFilter"), QStringLiteral("club"));
    {
        FakeHttp http;
        ServerApi api;
        MemoryTokenStore tokens;
        ConnectDialog dialog(api, tokens, [](const QUrl&) {}, nullptr, ConnectDialog::Purpose::DownloadVenues);
        api.setPollIntervalScale(5);
        http.reply("/api/version", 200, version());
        tokens.save(http.base(), QStringLiteral("bld_pat_saved"));
        http.reply("/api/venues", 200,
                   { { QStringLiteral("venues"),
                       QJsonArray{ venue("v1", "Grand Lobby", "club", "Train Club"), venue("v2", "Garage") } } });
        dialog.setAddress(http.base().toString());
        dialog.connectToServer();
        auto* list = dialog.findChild<QTreeWidget*>(QStringLiteral("layouts"));
        ASSERT_TRUE(waitFor([&] { return list->topLevelItemCount() == 2; }));
        EXPECT_EQ(dialog.findChild<QComboBox*>(QStringLiteral("ownerFilter"))->currentText(), QStringLiteral("Train Club"));
        EXPECT_EQ(shown(list), QStringList{ QStringLiteral("Grand Lobby") });
    }
    QSettings().remove(QStringLiteral("sync/ownerFilter"));
}

// Layouts too, and Publish starts at the club the lists were showing.
TEST(ConnectDialog, MarksLayoutOwnersAndPublishStartsAtTheShownClub) {
    {
        Harness h;
        h.tokens.save(h.http.base(), QStringLiteral("bld_pat_saved"));
        QJsonObject clubLayout = layout("L1", "Show 2026", "editor", "Train Club");
        clubLayout.insert(QStringLiteral("ownerOrgSlug"), QStringLiteral("club"));
        h.http.reply("/api/layouts", 200,
                     { { QStringLiteral("layouts"), QJsonArray{ clubLayout, layout("L2", "Home", "owner") } } });
        h.dialog.connectToServer();
        ASSERT_TRUE(waitFor([&] { return h.listed(); }));
        EXPECT_EQ(h.list()->findItems(QStringLiteral("Home"), Qt::MatchExactly).value(0)->text(1), QStringLiteral("Me"));
        auto* show = h.dialog.findChild<QComboBox*>(QStringLiteral("ownerFilter"));
        const int club = show->findText(QStringLiteral("Train Club"));
        ASSERT_GE(club, 0);
        show->setCurrentIndex(club);
        emit show->activated(club);
        EXPECT_EQ(shown(h.list()), QStringList{ QStringLiteral("Show 2026") });
        EXPECT_EQ(QSettings().value(QStringLiteral("sync/ownerFilter")).toString(), QStringLiteral("club"));
    }
    {
        FakeHttp http;
        ServerApi api;
        MemoryTokenStore tokens;
        ConnectDialog dialog(api, tokens, [](const QUrl&) {}, nullptr, ConnectDialog::Purpose::Publish);
        http.reply("/api/version", 200, version());
        tokens.save(http.base(), QStringLiteral("bld_pat_saved"));
        http.reply("/api/orgs", 200,
                   { { QStringLiteral("orgs"),
                       QJsonArray{ QJsonObject{ { QStringLiteral("slug"), QStringLiteral("club") },
                                                { QStringLiteral("name"), QStringLiteral("Train Club") },
                                                { QStringLiteral("myRole"), QStringLiteral("member") } } } } });
        dialog.setAddress(http.base().toString());
        dialog.setPublishContent(QByteArrayLiteral("<Map/>"), {}, QStringLiteral("x"));
        dialog.connectToServer();
        auto* owner = dialog.findChild<QComboBox*>(QStringLiteral("publishOwner"));
        ASSERT_TRUE(waitFor([&] { return owner->count() == 2; }));
        EXPECT_EQ(owner->itemText(0), QStringLiteral("Me"));
        EXPECT_EQ(owner->currentText(), QStringLiteral("Train Club"));
    }
    QSettings().remove(QStringLiteral("sync/ownerFilter"));
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
    // Into a club: asked first. Cancel publishes nothing.
    QStringList asked;
    bool answer = false;
    h.dialog.setConfirmSaveToClub([&](const QString& club) {
        asked << club;
        return answer;
    });
    const int before = static_cast<int>(h.http.requests.size());
    h.dialog.findChild<QPushButton*>(QStringLiteral("publish"))->click();
    EXPECT_EQ(asked, QStringList{ QStringLiteral("Train Club") });
    EXPECT_FALSE(waitFor([&] { return static_cast<int>(h.http.requests.size()) > before; }, 300));
    answer = true;
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

// A club you manage is offered under Save to like one you run or belong to.
TEST(ConnectDialog, PublishesToAClubYouManage) {
    Harness h(ConnectDialog::Purpose::Publish);
    h.tokens.save(h.http.base(), QStringLiteral("bld_pat_saved"));
    const auto org = [](const char* slug, const char* name, const char* role) {
        return QJsonObject{ { QStringLiteral("slug"), QLatin1String(slug) },
                            { QStringLiteral("name"), QLatin1String(name) },
                            { QStringLiteral("myRole"), QLatin1String(role) } };
    };
    h.http.reply("/api/orgs", 200,
                 { { QStringLiteral("orgs"), QJsonArray{ org("rail", "Rail Club", "manager"),
                                                         org("town", "Town Club", "member") } } });
    h.http.reply("/api/layouts", 201,
                 { { QStringLiteral("id"), QStringLiteral("L9") }, { QStringLiteral("title"), QStringLiteral("Yard") } });
    h.dialog.setPublishContent(QByteArrayLiteral("<Map/>"), {}, QStringLiteral("Yard"));
    h.dialog.connectToServer();
    auto* owner = h.dialog.findChild<QComboBox*>(QStringLiteral("publishOwner"));
    ASSERT_TRUE(waitFor([&] { return owner->count() == 3; }));
    EXPECT_EQ(owner->itemText(1), QStringLiteral("Rail Club"));
    EXPECT_EQ(owner->itemText(2), QStringLiteral("Town Club"));
    owner->setCurrentIndex(1);
    h.dialog.setConfirmSaveToClub([](const QString&) { return true; });
    h.dialog.findChild<QPushButton*>(QStringLiteral("publish"))->click();
    ASSERT_TRUE(waitFor([&] { return h.dialog.QDialog::result() == QDialog::Accepted; }));
    const auto body = QJsonDocument::fromJson(h.http.requests.back().body).object();
    EXPECT_EQ(body.value(QLatin1String("orgSlug")).toString(), QStringLiteral("rail"));
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
    for (const auto& r : h.http.requests) {
        if (r.path == "/api/auth/device/code") {
            EXPECT_TRUE(r.authorization.isEmpty());
        }
    }
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
    EXPECT_TRUE(keys.contains(QStringLiteral("owners.filter")));
    EXPECT_TRUE(keys.contains(QStringLiteral("club.find")));
    ASSERT_NE(server, nullptr);
    EXPECT_EQ(server->target(), h.dialog.findChild<QLineEdit*>(QStringLiteral("serverAddress")));
}

namespace {

// Two servers in the list, each with its own token; the second also has a
// recent layout.
struct TwoServers {
    FakeHttp club, pal;
    ServerApi api;
    MemoryTokenStore tokens;
    std::unique_ptr<ConnectDialog> dialog;

    explicit TwoServers(ConnectDialog::Purpose purpose = ConnectDialog::Purpose::OpenLayout) {
        resetSettings();
        ServerList list;
        list.add(club.base(), QStringLiteral("Train club"));
        list.add(pal.base(), QStringLiteral("Bob's server"));
        list.addRecent(pal.base(), { QStringLiteral("P2"), QStringLiteral("Bob's yard"), false, {} });
        list.touch(club.base());
        // A minute ago, so connecting to the other server is later even on a
        // clock that ticks every 15 ms (Windows).
        list.find(club.base())->lastUsed = QDateTime::currentDateTimeUtc().addSecs(-60);
        list.save();
        for (FakeHttp* h : { &club, &pal }) h->reply("/api/version", 200, version());
        tokens.save(club.base(), QStringLiteral("bld_pat_club"));
        tokens.save(pal.base(), QStringLiteral("bld_pat_pal"));
        club.reply("/api/layouts", 200, { { QStringLiteral("layouts"), QJsonArray{ layout("C1", "Club show", "owner") } } });
        pal.reply("/api/layouts", 200,
                  { { QStringLiteral("layouts"),
                      QJsonArray{ layout("P1", "Bob's loop", "owner"), layout("P2", "Bob's yard", "viewer") } } });
        dialog = std::make_unique<ConnectDialog>(api, tokens, [](const QUrl&) {}, nullptr, purpose);
    }
    QTreeWidget* servers() { return dialog->findChild<QTreeWidget*>(QStringLiteral("servers")); }
    QTreeWidgetItem* serverItem(const QString& startsWith) {
        for (int i = 0; i < servers()->topLevelItemCount(); ++i)
            if (servers()->topLevelItem(i)->text(0).startsWith(startsWith)) return servers()->topLevelItem(i);
        return nullptr;
    }
};

void expectOnlyOwnToken(const FakeHttp& http, const QByteArray& own) {
    for (const auto& r : http.requests)
        EXPECT_TRUE(r.authorization.isEmpty() || r.authorization == "Bearer " + own)
            << r.path.toStdString() << " got " << r.authorization.toStdString();
}

}  // namespace

TEST(ConnectDialog, ListsYourServersWithTheirRecentLayoutsAndStartsAtTheLastOne) {
    TwoServers t;
    auto* club = t.serverItem(QStringLiteral("Train club"));
    auto* pal = t.serverItem(QStringLiteral("Bob's server"));
    ASSERT_TRUE(club && pal);
    EXPECT_EQ(club->text(0), QStringLiteral("Train club (Main)"));
    // Recent layouts sit under the server they're on.
    EXPECT_EQ(club->childCount(), 0);
    ASSERT_EQ(pal->childCount(), 1);
    EXPECT_EQ(pal->child(0)->text(0), QStringLiteral("Bob's yard"));
    // The one connected to last is picked, and its address filled in.
    EXPECT_EQ(t.servers()->currentItem(), club);
    EXPECT_EQ(QUrl(t.dialog->findChild<QLineEdit*>(QStringLiteral("serverAddress"))->text()), t.club.base());
    t.servers()->setCurrentItem(pal);
    EXPECT_EQ(QUrl(t.dialog->findChild<QLineEdit*>(QStringLiteral("serverAddress"))->text()), t.pal.base());
}

TEST(ConnectDialog, ARecentLayoutOpensFromItsOwnServerWithItsOwnToken) {
    TwoServers t;
    auto* pal = t.serverItem(QStringLiteral("Bob's server"));
    ASSERT_TRUE(pal);
    emit t.servers()->itemActivated(pal->child(0), 0);
    ASSERT_TRUE(waitFor([&] { return t.dialog->result().has_value(); }));
    EXPECT_EQ(t.dialog->result()->server, t.pal.base());
    EXPECT_EQ(t.dialog->result()->layoutId, QStringLiteral("P2"));
    EXPECT_EQ(t.dialog->result()->token, QStringLiteral("bld_pat_pal"));
    EXPECT_TRUE(t.dialog->result()->readOnly);
    EXPECT_TRUE(t.club.requests.empty());
    expectOnlyOwnToken(t.pal, "bld_pat_pal");
    // Connecting made it the last used.
    EXPECT_EQ(ServerList::load().lastUsed()->url, t.pal.base());
}

TEST(ConnectDialog, PublishesToTheServerYouPick) {
    TwoServers t(ConnectDialog::Purpose::Publish);
    t.pal.reply("/api/orgs", 200, { { QStringLiteral("orgs"), QJsonArray{} } });
    t.pal.clear("/api/layouts");
    t.pal.reply("/api/layouts", 201,
                { { QStringLiteral("id"), QStringLiteral("P9") }, { QStringLiteral("title"), QStringLiteral("Show") } });
    t.dialog->setPublishContent(QByteArrayLiteral("<Map/>"), {}, QStringLiteral("Show"));
    // Publish lists servers only (no recent layouts under them).
    EXPECT_EQ(t.serverItem(QStringLiteral("Bob's server"))->childCount(), 0);
    t.servers()->setCurrentItem(t.serverItem(QStringLiteral("Bob's server")));
    t.dialog->connectToServer();
    auto* where = t.dialog->findChild<QLabel*>(QStringLiteral("publishServer"));
    ASSERT_TRUE(waitFor([&] { return where->text() == QStringLiteral("Bob's server"); }));
    t.dialog->publishNow();
    ASSERT_TRUE(waitFor([&] { return t.dialog->result().has_value(); }));
    EXPECT_EQ(t.dialog->result()->server, t.pal.base());
    EXPECT_EQ(t.dialog->result()->layoutId, QStringLiteral("P9"));
    EXPECT_EQ(t.pal.requests.back().method, QByteArray("POST"));
    EXPECT_EQ(t.pal.requests.back().authorization, QByteArray("Bearer bld_pat_pal"));
    EXPECT_TRUE(t.club.requests.empty());
    expectOnlyOwnToken(t.pal, "bld_pat_pal");
}

TEST(ConnectDialog, ANewAddressJoinsYourServersAndTheFirstIsMain) {
    Harness h;
    h.tokens.save(h.http.base(), QStringLiteral("bld_pat_saved"));
    h.http.reply("/api/layouts", 200, { { QStringLiteral("layouts"), QJsonArray{} } });
    h.dialog.connectToServer();
    ASSERT_TRUE(waitFor([&] { return h.page() == 2; }));
    const ServerList list = ServerList::load();
    ASSERT_EQ(list.servers().size(), 1);
    EXPECT_EQ(list.servers().first().url, h.http.base());
    EXPECT_TRUE(list.servers().first().main);
    EXPECT_EQ(list.servers().first().version, QStringLiteral("2.0.0"));
}

TEST(ConnectDialog, ManageServersOpensOnceTheClickHasReturned) {
    TwoServers t;
    int opened = 0;
    bool insideClick = false;
    t.dialog->setManageServers([&] {
        EXPECT_FALSE(insideClick);
        ++opened;
        // Renamed meanwhile: the list shows it when Servers closes.
        ServerList list = ServerList::load();
        list.rename(t.pal.base(), QStringLiteral("Bob's"));
        list.save();
    });
    insideClick = true;
    t.dialog->findChild<QPushButton*>(QStringLiteral("manageServers"))->click();
    insideClick = false;
    EXPECT_EQ(opened, 0);
    ASSERT_TRUE(waitFor([&] { return opened == 1; }));
    ASSERT_TRUE(waitFor([&] { return t.serverItem(QStringLiteral("Bob's")) != nullptr; }));
    EXPECT_EQ(t.serverItem(QStringLiteral("Bob's"))->text(0), QStringLiteral("Bob's"));
}

TEST(ConnectDialog, DeletesALayoutYouOwnAfterAsking) {
    Harness h;
    h.tokens.save(h.http.base(), QStringLiteral("bld_pat_saved"));
    h.http.reply("/api/layouts", 200,
                 { { QStringLiteral("layouts"), QJsonArray{ layout("L1", "Show 2026", "owner"), layout("L2", "Club table", "editor") } } });
    h.http.reply("/api/layouts", 200, { { QStringLiteral("layouts"), QJsonArray{ layout("L2", "Club table", "editor") } } });
    h.http.reply("/api/layouts/L1", 200, { { QStringLiteral("ok"), true } });
    QStringList asked;
    bool answer = false;
    h.dialog.setConfirmDelete([&](const QString& name, bool isLayout) {
        asked << name;
        EXPECT_TRUE(isLayout);
        return answer;
    });
    h.dialog.connectToServer();
    ASSERT_TRUE(waitFor([&] { return h.listed(); }));
    auto* del = h.dialog.findChild<QPushButton*>(QStringLiteral("deleteItem"));
    ASSERT_TRUE(del);
    EXPECT_FALSE(del->isEnabled());
    // Only the owner deletes a layout.
    h.list()->setCurrentItem(h.list()->findItems(QStringLiteral("Club table"), Qt::MatchExactly).value(0));
    EXPECT_FALSE(del->isEnabled());
    h.list()->setCurrentItem(h.list()->findItems(QStringLiteral("Show 2026"), Qt::MatchExactly).value(0));
    ASSERT_TRUE(del->isEnabled());

    // Cancel: nothing is sent.
    const auto deletes = [&] {
        int n = 0;
        for (const auto& r : h.http.requests) n += r.method == "DELETE";
        return n;
    };
    del->click();
    EXPECT_EQ(asked, QStringList{ QStringLiteral("Show 2026") });
    EXPECT_FALSE(waitFor([&] { return deletes() > 0; }, 400));

    // Delete: the route is called, the list comes back without it.
    answer = true;
    del->click();
    ASSERT_TRUE(waitFor([&] { return h.list()->topLevelItemCount() == 1; }));
    EXPECT_EQ(deletes(), 1);
    const auto it = std::find_if(h.http.requests.begin(), h.http.requests.end(), [](const auto& r) { return r.method == "DELETE"; });
    EXPECT_EQ(it->path, QByteArray("/api/layouts/L1"));
    EXPECT_EQ(it->authorization, QByteArray("Bearer bld_pat_saved"));
    EXPECT_EQ(h.message(), QStringLiteral("Deleted “Show 2026”"));
}

TEST(ConnectDialog, DeletesAVenueAndSaysWhyNot) {
    Harness h(ConnectDialog::Purpose::DownloadVenues);
    h.tokens.save(h.http.base(), QStringLiteral("bld_pat_saved"));
    const QJsonObject hall{ { QStringLiteral("id"), QStringLiteral("V1") }, { QStringLiteral("name"), QStringLiteral("Hall") } };
    h.http.reply("/api/venues", 200, { { QStringLiteral("venues"), QJsonArray{ hall } } });
    h.http.reply("/api/orgs", 200, { { QStringLiteral("orgs"), QJsonArray{} } });
    h.http.reply("/api/venues/V1", 403, { { QStringLiteral("error"), QStringLiteral("forbidden") } });
    h.dialog.setConfirmDelete([](const QString&, bool isLayout) { return !isLayout; });
    h.dialog.connectToServer();
    ASSERT_TRUE(waitFor([&] { return h.listed(); }));
    h.list()->setCurrentItem(h.list()->topLevelItem(0));
    auto* del = h.dialog.findChild<QPushButton*>(QStringLiteral("deleteItem"));
    ASSERT_TRUE(del->isEnabled());
    del->click();
    ASSERT_TRUE(waitFor([&] { return h.message().startsWith(QStringLiteral("Could not delete it")); }));
    EXPECT_EQ(h.http.requests.back().method, QByteArray("DELETE"));
    EXPECT_EQ(h.http.requests.back().path, QByteArray("/api/venues/V1"));
    EXPECT_TRUE(del->isEnabled());  // can try again
    EXPECT_EQ(h.list()->topLevelItemCount(), 1);
}

// A club's layout or venue back to its author, from the ⋯ button: the
// author takes it back, a club's runner gives it back; the Owner column
// says who made it.
namespace {
QJsonObject credited(QJsonObject item, const char* by, bool take, bool give) {
    item.insert(QStringLiteral("credit"),
                QJsonObject{ { QStringLiteral("by"), QString::fromUtf8(by) },
                             { QStringLiteral("authorName"), QStringLiteral("Sam") },
                             { QStringLiteral("club"), QStringLiteral("Train Club") },
                             { QStringLiteral("canTakeBack"), take },
                             { QStringLiteral("canGiveBack"), give } });
    return item;
}
} // namespace

TEST(ConnectDialog, TakesALayoutBackAfterAskingAndShowsWhoMadeEach) {
    Harness h;
    h.tokens.save(h.http.base(), QStringLiteral("bld_pat_saved"));
    QJsonObject club = credited(layout("L1", "Show 2026", "editor", "Train Club"), "you", true, false);
    club.insert(QStringLiteral("ownerOrgSlug"), QStringLiteral("club"));
    const QJsonObject mine = layout("L2", "Home", "owner");
    h.http.reply("/api/layouts", 200, { { QStringLiteral("layouts"), QJsonArray{ club, mine } } });
    h.http.reply("/api/layouts/L1/take-back", 200,
                 { { QStringLiteral("ok"), true }, { QStringLiteral("keptCopyId"), QStringLiteral("L9") } });
    h.http.reply("/api/layouts", 200, { { QStringLiteral("layouts"), QJsonArray{ mine } } });
    QStringList asked;
    bool answer = false;
    h.dialog.setConfirmReturn([&](const QString& name, const Credit& c, bool give) {
        asked << name;
        EXPECT_FALSE(give);
        EXPECT_EQ(c.club, QStringLiteral("Train Club"));
        return answer;
    });
    h.dialog.connectToServer();
    ASSERT_TRUE(waitFor([&] { return h.listed(); }));
    auto* show = h.list()->findItems(QStringLiteral("Show 2026"), Qt::MatchExactly).value(0);
    EXPECT_EQ(show->text(1), QStringLiteral("by you · in Train Club"));
    EXPECT_EQ(h.list()->findItems(QStringLiteral("Home"), Qt::MatchExactly).value(0)->text(1),
              QStringLiteral("Me"));
    auto* more = h.dialog.moreButton();
    // Nothing to take back from your own layout.
    h.list()->setCurrentItem(h.list()->findItems(QStringLiteral("Home"), Qt::MatchExactly).value(0));
    EXPECT_FALSE(more->isEnabled());
    h.list()->setCurrentItem(show);
    ASSERT_TRUE(more->isEnabled());
    const auto posts = [&] {
        int n = 0;
        for (const auto& r : h.http.requests)
            n += r.path.endsWith("/take-back") || r.path.endsWith("/give-back");
        return n;
    };
    h.dialog.returnSelected(true); // not a runner: no give back
    h.dialog.returnSelected(false);
    EXPECT_EQ(asked, QStringList{ QStringLiteral("Show 2026") });
    EXPECT_FALSE(waitFor([&] { return posts() > 0; }, 300));
    answer = true;
    h.dialog.returnSelected(false);
    ASSERT_TRUE(waitFor([&] { return h.list()->topLevelItemCount() == 1; }));
    EXPECT_EQ(posts(), 1);
    const auto it = std::find_if(h.http.requests.begin(), h.http.requests.end(),
                                 [](const auto& r) { return r.path.endsWith("/take-back"); });
    EXPECT_EQ(it->method, QByteArray("POST"));
    EXPECT_EQ(it->path, QByteArray("/api/layouts/L1/take-back"));
    EXPECT_EQ(it->authorization, QByteArray("Bearer bld_pat_saved"));
    EXPECT_TRUE(h.message().contains(QStringLiteral("the club kept its own copy")));
}

TEST(ConnectDialog, GivesAVenueBackToItsAuthorAndSaysWhyNot) {
    Harness h(ConnectDialog::Purpose::DownloadVenues);
    h.tokens.save(h.http.base(), QStringLiteral("bld_pat_saved"));
    const QJsonObject hall =
        credited(QJsonObject{ { QStringLiteral("id"), QStringLiteral("V1") },
                              { QStringLiteral("name"), QStringLiteral("Hall") },
                              { QStringLiteral("ownerOrgId"), QStringLiteral("o1") },
                              { QStringLiteral("ownerOrgName"), QStringLiteral("Train Club") },
                              { QStringLiteral("ownerOrgSlug"), QStringLiteral("club") } },
                 "Sam", false, true);
    h.http.reply("/api/venues", 200, { { QStringLiteral("venues"), QJsonArray{ hall } } });
    h.http.reply("/api/orgs", 200, { { QStringLiteral("orgs"), QJsonArray{} } });
    h.http.reply("/api/venues/V1/give-back", 409,
                 { { QStringLiteral("error"), QStringLiteral("author_gone") },
                   { QStringLiteral("message"),
                     QStringLiteral("The person who made it no longer has an account here.") } });
    bool gave = false;
    h.dialog.setConfirmReturn([&](const QString&, const Credit& c, bool give) {
        gave = give;
        EXPECT_EQ(c.authorName, QStringLiteral("Sam"));
        return true;
    });
    h.dialog.connectToServer();
    ASSERT_TRUE(waitFor([&] { return h.listed(); }));
    EXPECT_EQ(h.list()->topLevelItem(0)->text(1), QStringLiteral("by Sam · in Train Club"));
    h.list()->setCurrentItem(h.list()->topLevelItem(0));
    ASSERT_TRUE(h.dialog.moreButton()->isEnabled());
    h.dialog.returnSelected(false); // not the author: no take back
    EXPECT_FALSE(gave);
    h.dialog.returnSelected(true);
    EXPECT_TRUE(gave);
    ASSERT_TRUE(waitFor([&] { return h.message().startsWith(QStringLiteral("Could not do that")); }));
    EXPECT_EQ(h.http.requests.back().path, QByteArray("/api/venues/V1/give-back"));
    EXPECT_TRUE(h.dialog.moreButton()->isEnabled()); // can try again
}
