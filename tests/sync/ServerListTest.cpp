// Your servers (ServerList): the single-server setting becomes the first
// server, marked Main, with no new sign-in; its folders move when its
// address has a port; Main and the settings rule; per-server folders that
// never share; recent layouts per server.

#include "ServerList.h"
#include "TokenStore.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>

using namespace bld::sync;

namespace {

class ServerListTest : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        clear();
    }
    void TearDown() override {
        clear();
        QDir(appData() + QStringLiteral("/live")).removeRecursively();
        QDir(appData() + QStringLiteral("/server-parts")).removeRecursively();
        QStandardPaths::setTestModeEnabled(false);
    }
    static void clear() {
        QSettings s;
        s.remove(QLatin1String(ServerList::kKey));
        s.remove(QLatin1String(ServerList::kLegacyAddressKey));
        s.remove(QStringLiteral("partsLibrary/userPaths"));
    }
    static QString appData() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation); }
    static void touchFile(const QString& path) {
        QDir().mkpath(QFileInfo(path).path());
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
};

}  // namespace

TEST_F(ServerListTest, TheOneServerFromBeforeBecomesMainWithoutSigningInAgain) {
    QSettings().setValue(QLatin1String(ServerList::kLegacyAddressKey), QStringLiteral("club.example.org"));
    // Its token, saved by the single-server version.
    MemoryTokenStore tokens;
    tokens.save(QUrl(QStringLiteral("https://club.example.org")), QStringLiteral("bld_pat_club"));

    const ServerList list = ServerList::load();
    ASSERT_EQ(list.servers().size(), 1);
    const ServerEntry& e = list.servers().first();
    EXPECT_EQ(e.url, QUrl(QStringLiteral("https://club.example.org")));
    EXPECT_TRUE(e.main);
    EXPECT_EQ(list.mainServer(), &e);
    // The same token is found for it: no new sign-in.
    QString found;
    tokens.load(e.url, [&](const QString& t) { found = t; });
    EXPECT_EQ(found, QStringLiteral("bld_pat_club"));
    // Saved: it stays, and isn't migrated twice.
    EXPECT_TRUE(QSettings().contains(QLatin1String(ServerList::kKey)));
    EXPECT_EQ(ServerList::load().servers().size(), 1);
    // A server without a port keeps its folders' names.
    EXPECT_EQ(ServerList::folderName(e.url), QStringLiteral("club.example.org"));
}

TEST_F(ServerListTest, AServerWithAPortMovesItsFoldersAndTheLibraryFollows) {
    QSettings().setValue(QLatin1String(ServerList::kLegacyAddressKey), QStringLiteral("http://localhost:8080"));
    touchFile(appData() + QStringLiteral("/live/localhost/L1/offline.bin"));
    touchFile(appData() + QStringLiteral("/server-parts/localhost/p.xml"));
    const QString oldParts = appData() + QStringLiteral("/server-parts/localhost");
    QSettings().setValue(QStringLiteral("partsLibrary/userPaths"), QStringList{ QStringLiteral("/my/parts"), oldParts });

    const ServerList list = ServerList::load();
    ASSERT_EQ(list.servers().size(), 1);
    EXPECT_EQ(ServerList::folderName(list.servers().first().url), QStringLiteral("localhost_8080"));
    EXPECT_TRUE(QFile::exists(appData() + QStringLiteral("/live/localhost_8080/L1/offline.bin")));
    EXPECT_TRUE(QFile::exists(appData() + QStringLiteral("/server-parts/localhost_8080/p.xml")));
    EXPECT_FALSE(QDir(oldParts).exists());
    EXPECT_EQ(QSettings().value(QStringLiteral("partsLibrary/userPaths")).toStringList(),
              (QStringList{ QStringLiteral("/my/parts"), appData() + QStringLiteral("/server-parts/localhost_8080") }));
}

TEST_F(ServerListTest, NothingToMigrateStartsEmpty) {
    const ServerList list = ServerList::load();
    EXPECT_TRUE(list.isEmpty());
    EXPECT_EQ(list.mainServer(), nullptr);
    EXPECT_TRUE(list.settingsAccount().isEmpty());
}

TEST_F(ServerListTest, TheFirstIsMainAndSettingsFollowMain) {
    const QUrl club(QStringLiteral("https://club.example.org")), friendly(QStringLiteral("https://friend.example.net"));
    ServerList list = ServerList::load();
    list.add(club, QStringLiteral("Train club"));
    list.add(friendly);
    list.save();
    list = ServerList::load();
    ASSERT_EQ(list.servers().size(), 2);
    EXPECT_TRUE(list.find(club)->main);
    EXPECT_FALSE(list.find(friendly)->main);
    EXPECT_EQ(list.find(club)->label(), QStringLiteral("Train club"));
    EXPECT_EQ(list.find(friendly)->label(), QStringLiteral("friend.example.net"));
    // The rule: settings follow Main, not the one connected to last.
    list.touch(friendly);
    EXPECT_EQ(list.lastUsed()->url, friendly);
    EXPECT_EQ(list.settingsAccount(), club);
    list.setMain(friendly);
    EXPECT_EQ(list.settingsAccount(), friendly);
    EXPECT_FALSE(list.find(club)->main);
    // Removing Main makes the one left Main.
    EXPECT_TRUE(list.remove(friendly));
    EXPECT_TRUE(list.find(club)->main);
    EXPECT_EQ(list.settingsAccount(), club);
    // Renaming to nothing shows the host again.
    EXPECT_TRUE(list.rename(club, QStringLiteral("  ")));
    EXPECT_EQ(list.find(club)->label(), QStringLiteral("club.example.org"));
}

TEST_F(ServerListTest, TwoServersOnOneMachineNeverShareATokenOrAFolder) {
    const QUrl a(QStringLiteral("http://127.0.0.1:4001")), b(QStringLiteral("http://127.0.0.1:4002"));
    EXPECT_NE(TokenStore::keyFor(a), TokenStore::keyFor(b));
    EXPECT_NE(ServerList::folderName(a), ServerList::folderName(b));
    ServerList list;
    list.add(a);
    list.add(b);
    EXPECT_EQ(list.servers().size(), 2);
    EXPECT_EQ(list.find(b)->url, b);
    // The same address again is the same server.
    list.add(QUrl(QStringLiteral("http://127.0.0.1:4001/some/path")));
    EXPECT_EQ(list.servers().size(), 2);
}

TEST_F(ServerListTest, RecentLayoutsAreKeptPerServerNewestFirst) {
    const QUrl a(QStringLiteral("https://a.example.org")), b(QStringLiteral("https://b.example.org"));
    ServerList list;
    for (int i = 0; i < ServerList::kMaxRecent + 2; ++i)
        list.addRecent(a, { QStringLiteral("L%1").arg(i), QStringLiteral("Layout %1").arg(i), false, {} });
    list.addRecent(b, { QStringLiteral("B1"), QStringLiteral("Club table"), true, {} });
    list.addRecent(a, { QStringLiteral("L3"), QStringLiteral("Layout 3"), false, {} });
    list.save();
    list = ServerList::load();
    const auto& ra = list.find(a)->recent;
    ASSERT_EQ(ra.size(), ServerList::kMaxRecent);
    EXPECT_EQ(ra.first().id, QStringLiteral("L3"));
    EXPECT_EQ(ra.at(1).id, QStringLiteral("L9"));
    ASSERT_EQ(list.find(b)->recent.size(), 1);
    EXPECT_TRUE(list.find(b)->recent.first().readOnly);
}
