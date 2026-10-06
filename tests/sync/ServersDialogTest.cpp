// File › Servers… against two in-process servers: each row says who is
// signed in, the version and what the server can't do yet; each server is
// only ever sent its own token; sign out, remove, Main and sign in act on
// one server alone.

#include "FakeHttp.h"
#include "ServerList.h"
#include "ServersDialog.h"
#include "TokenStore.h"
#include "ui/help/HelpButton.h"
#include "ui/help/HelpTexts.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>

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

QJsonObject version(const QString& v, const std::optional<QStringList>& features = std::nullopt) {
    QJsonObject o{ { QStringLiteral("version"), v },
                   { QStringLiteral("schemaVersion"), 1 },
                   { QStringLiteral("protocols"), QJsonArray{ QStringLiteral("y-websocket/1") } } };
    if (features) o.insert(QStringLiteral("features"), QJsonArray::fromStringList(*features));
    return o;
}

QJsonObject user(const QString& name) {
    return { { QStringLiteral("user"),
               QJsonObject{ { QStringLiteral("id"), name.toLower() }, { QStringLiteral("displayName"), name } } } };
}

// Every request each server got carried no token or its own.
void expectOnlyOwnToken(const FakeHttp& http, const QByteArray& own) {
    for (const auto& r : http.requests)
        EXPECT_TRUE(r.authorization.isEmpty() || r.authorization == "Bearer " + own)
            << r.path.toStdString() << " got " << r.authorization.toStdString();
}

class ServersDialogTest : public ::testing::Test {
protected:
    void SetUp() override {
        QSettings s;
        s.remove(QLatin1String(ServerList::kLegacyAddressKey));
        ServerList list;
        list.add(club.base(), QStringLiteral("Train club"));
        list.add(pal.base());
        list.save();
        // The club lists every feature; the friend's server is older.
        club.reply("/api/version", 200, version(QStringLiteral("2.4.0"), desktopFeatures()));
        pal.reply("/api/version", 200,
                  version(QStringLiteral("2.0.1"), QStringList{ QStringLiteral("liveSync"), QStringLiteral("signIn"),
                                                                QStringLiteral("publish") }));
        club.reply("/api/tokens/current", 200, user(QStringLiteral("Ann")));
        pal.reply("/api/tokens/current", 200, user(QStringLiteral("Bob")));
        tokens.save(club.base(), QStringLiteral("bld_pat_club"));
        tokens.save(pal.base(), QStringLiteral("bld_pat_pal"));
    }
    void TearDown() override { QSettings().setValue(QLatin1String(ServerList::kKey), QByteArray("[]")); }

    std::unique_ptr<ServersDialog> open() {
        auto d = std::make_unique<ServersDialog>(tokens, [](const QUrl&) {});
        dialog = d.get();
        return d;
    }
    bool settled() {
        return dialog->isSignedIn(club.base()) && dialog->isSignedIn(pal.base())
               && !dialog->statusText(club.base()).contains(QStringLiteral("Checking"))
               && dialog->statusText(pal.base()).contains(QStringLiteral("version"))
               && dialog->statusText(club.base()).contains(QStringLiteral("version"));
    }

    FakeHttp club, pal;
    MemoryTokenStore tokens;
    ServersDialog* dialog = nullptr;
};

}  // namespace

TEST_F(ServersDialogTest, EachRowSaysWhoIsSignedInTheVersionAndWhatIsMissing) {
    auto d = open();
    ASSERT_TRUE(waitFor([&] { return settled(); }));
    EXPECT_EQ(dialog->statusText(club.base()), QStringLiteral("Signed in as Ann · version 2.4.0"));
    EXPECT_EQ(dialog->statusText(pal.base()), QStringLiteral("Signed in as Bob · version 2.0.1"));
    EXPECT_TRUE(dialog->missingText(club.base()).isEmpty());
    const QString missing = dialog->missingText(pal.base());
    EXPECT_TRUE(missing.contains(featureLabel(QStringLiteral("venues")))) << missing.toStdString();

    // The rows show the same words, and Main on the club only.
    auto* rows = dialog->findChild<QListWidget*>(QStringLiteral("serverRows"));
    ASSERT_EQ(rows->count(), 2);
    QWidget* first = rows->itemWidget(rows->item(0));
    EXPECT_EQ(first->findChild<QLabel*>(QStringLiteral("serverName"))->text(), QStringLiteral("Train club"));
    EXPECT_FALSE(first->findChild<QLabel*>(QStringLiteral("mainBadge"))->isHidden());
    EXPECT_EQ(first->findChild<QLabel*>(QStringLiteral("serverStatus"))->text(), dialog->statusText(club.base()));
    QWidget* second = rows->itemWidget(rows->item(1));
    EXPECT_TRUE(second->findChild<QLabel*>(QStringLiteral("mainBadge"))->isHidden());
    EXPECT_EQ(second->findChild<QLabel*>(QStringLiteral("serverMissing"))->text(), missing);
    EXPECT_EQ(second->findChild<QLabel*>(QStringLiteral("serverMissing"))->toolTip(), missing);
    // A row with the "Can't do yet" line is no taller than one without.
    EXPECT_TRUE(first->findChild<QLabel*>(QStringLiteral("serverMissing"))->isHidden());
    EXPECT_EQ(rows->item(0)->sizeHint().height(), rows->item(1)->sizeHint().height());
    EXPECT_EQ(first->sizeHint().height(), second->sizeHint().height());

    // What they said is remembered for next time.
    const ServerList list = ServerList::load();
    EXPECT_EQ(list.find(pal.base())->version, QStringLiteral("2.0.1"));
    EXPECT_EQ(list.find(club.base())->userName, QStringLiteral("Ann"));
}

TEST_F(ServersDialogTest, EachServerIsOnlySentItsOwnToken) {
    auto d = open();
    ASSERT_TRUE(waitFor([&] { return settled(); }));
    ASSERT_FALSE(club.requests.empty());
    ASSERT_FALSE(pal.requests.empty());
    expectOnlyOwnToken(club, "bld_pat_club");
    expectOnlyOwnToken(pal, "bld_pat_pal");
    bool clubAsked = false, palAsked = false;
    for (const auto& r : club.requests) clubAsked |= r.authorization == "Bearer bld_pat_club";
    for (const auto& r : pal.requests) palAsked |= r.authorization == "Bearer bld_pat_pal";
    EXPECT_TRUE(clubAsked && palAsked);
}

TEST_F(ServersDialogTest, NotSignedInAndSignedOutByTheServerSaySo) {
    tokens.remove(pal.base());
    club.clear("/api/tokens/current");
    club.reply("/api/tokens/current", 401, { { QStringLiteral("error"), QStringLiteral("unauthorized") } });
    auto d = open();
    ASSERT_TRUE(waitFor([&] {
        return dialog->statusText(pal.base()).startsWith(QStringLiteral("Not signed in"))
               && dialog->statusText(club.base()).startsWith(QStringLiteral("Signed out by the server"));
    }));
    // Nothing was sent to the friend's server with anyone's token.
    for (const auto& r : pal.requests) EXPECT_TRUE(r.authorization.isEmpty()) << r.path.toStdString();
}

TEST_F(ServersDialogTest, SignOutRemoveAndMainTouchOneServerOnly) {
    auto d = open();
    ASSERT_TRUE(waitFor([&] { return settled(); }));
    dialog->selectServer(pal.base());
    auto* sign = dialog->findChild<QPushButton*>(QStringLiteral("signInOut"));
    EXPECT_EQ(sign->text(), QStringLiteral("Sign Out"));

    dialog->signOut(pal.base());
    EXPECT_FALSE(tokens.tokens.contains(TokenStore::keyFor(pal.base())));
    EXPECT_TRUE(tokens.tokens.contains(TokenStore::keyFor(club.base())));
    EXPECT_TRUE(dialog->statusText(pal.base()).startsWith(QStringLiteral("Not signed in")));
    EXPECT_EQ(sign->text(), QStringLiteral("Sign In"));

    dialog->makeMain(pal.base());
    EXPECT_EQ(ServerList::load().settingsAccount(), pal.base());

    dialog->renameServer(pal.base(), QStringLiteral("Bob's server"));
    EXPECT_EQ(ServerList::load().find(pal.base())->label(), QStringLiteral("Bob's server"));

    // Removing the club signs out of it and nothing else; the friend's stays Main.
    tokens.save(pal.base(), QStringLiteral("bld_pat_pal"));
    dialog->removeServer(club.base());
    EXPECT_FALSE(tokens.tokens.contains(TokenStore::keyFor(club.base())));
    EXPECT_TRUE(tokens.tokens.contains(TokenStore::keyFor(pal.base())));
    const ServerList list = ServerList::load();
    ASSERT_EQ(list.servers().size(), 1);
    EXPECT_EQ(list.settingsAccount(), pal.base());
}

TEST_F(ServersDialogTest, AddsAServerByItsAddressAndSignsInToIt) {
    auto d = open();
    QString error;
    EXPECT_FALSE(dialog->addServer(QStringLiteral("http://layouts.example.org"), {}, &error));
    EXPECT_FALSE(error.isEmpty());

    FakeHttp third;
    third.reply("/api/version", 200, version(QStringLiteral("2.4.0"), desktopFeatures()));
    third.reply("/api/tokens/current", 200, user(QStringLiteral("Cy")));
    ASSERT_TRUE(dialog->addServer(third.base().toString(), QStringLiteral("  Show hall ")));
    EXPECT_EQ(dialog->selectedServer(), third.base());
    ASSERT_TRUE(waitFor([&] { return dialog->statusText(third.base()).startsWith(QStringLiteral("Not signed in")); }));
    EXPECT_EQ(ServerList::load().find(third.base())->label(), QStringLiteral("Show hall"));

    // Signing in: the new token is kept for this server alone.
    QList<QUrl> asked;
    dialog->setSignInHandler([&](const QUrl& url) {
        asked << url;
        tokens.save(url, QStringLiteral("bld_pat_third"));
        return QStringLiteral("bld_pat_third");
    });
    dialog->signIn(third.base());
    ASSERT_TRUE(waitFor([&] { return dialog->isSignedIn(third.base()); }));
    EXPECT_EQ(asked, QList<QUrl>{ third.base() });
    // The name and the version arrive in separate replies.
    EXPECT_TRUE(waitFor([&] {
        return dialog->statusText(third.base()) == QStringLiteral("Signed in as Cy · version 2.4.0");
    })) << dialog->statusText(third.base()).toStdString();
    expectOnlyOwnToken(third, "bld_pat_third");
    expectOnlyOwnToken(club, "bld_pat_club");
    expectOnlyOwnToken(pal, "bld_pat_pal");
}

TEST_F(ServersDialogTest, HasHelpForTheListAndMain) {
    auto d = open();
    QStringList keys;
    for (auto* b : dialog->findChildren<bld::ui::help::HelpButton*>()) {
        keys << b->key();
        EXPECT_TRUE(bld::ui::help::helpEntry(b->key())) << b->key().toStdString();
    }
    EXPECT_TRUE(keys.contains(QStringLiteral("servers.list")));
    EXPECT_TRUE(keys.contains(QStringLiteral("servers.main")));
}
