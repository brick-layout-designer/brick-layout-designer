// Settings sync with the connected server's account (GET / PUT
// /api/me/preferences) against a scripted server: pull on connecting, the
// newest updatedAt wins either way, local changes push once after a pause,
// and what each server confirmed is kept per host.

#include "ui/PrefsSync.h"
#include "FakeHttp.h"
#include "ui/theme/AppPrefs.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>

#include <functional>

using namespace bld;
using bld::synctest::FakeHttp;
using ui::PrefsSync;
using ui::theme::AppPrefs;
using ui::theme::PrefsStore;
using ui::theme::ThemeChoice;

namespace {

const QByteArray kPath = QByteArrayLiteral("/api/me/preferences");

bool waitFor(const std::function<bool()>& done, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

void settle(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

QJsonObject answer(const QJsonObject& prefs, const QString& updatedAt) {
    return QJsonObject{ { QStringLiteral("prefs"), prefs },
                        { QStringLiteral("updatedAt"), updatedAt.isEmpty() ? QJsonValue() : QJsonValue(updatedAt) } };
}

QJsonObject serverPrefs(const QString& accent, const QString& theme) {
    AppPrefs p;
    p.accent = accent;
    p.theme = ui::theme::themeChoiceFromId(theme);
    QJsonObject json = p.toJson();
    json.remove(QStringLiteral("partsIconSize"));  // a server from before the Parts list's size was synced
    return json;
}

int count(const FakeHttp& http, const QByteArray& method) {
    int n = 0;
    for (const auto& r : http.requests) n += r.method == method && r.path == kPath;
    return n;
}

class PrefsSyncTest : public ::testing::Test {
protected:
    void SetUp() override {
        QSettings().remove(kGroup);
        QSettings().remove(QStringLiteral("servers"));
    }
    void TearDown() override {
        QSettings().remove(kGroup);
        QSettings().remove(QStringLiteral("servers"));
    }
    static constexpr const char* kGroup = "test-prefs-sync";
};

}  // namespace

TEST_F(PrefsSyncTest, PullTakesTheServersNewerSettings) {
    PrefsStore store(QString::fromLatin1(kGroup));
    AppPrefs mine;
    mine.accent = QStringLiteral("ocean");
    store.update(mine);  // stamped now

    FakeHttp http;
    const QString later = QDateTime::currentDateTimeUtc().addSecs(3600).toString(Qt::ISODateWithMs);
    http.reply(kPath, 200, answer(serverPrefs(QStringLiteral("forest"), QStringLiteral("dark")), later));

    PrefsSync sync(store);
    int synced = 0;
    QObject::connect(&sync, &PrefsSync::synced, [&synced] { ++synced; });
    sync.start(http.base(), QStringLiteral("bld_pat_test"));
    ASSERT_TRUE(waitFor([&] { return synced > 0; }));

    EXPECT_EQ(store.prefs().accent, QStringLiteral("forest"));
    EXPECT_EQ(store.prefs().theme, ThemeChoice::Dark);
    EXPECT_EQ(store.prefs().updatedAt, QDateTime::fromString(later, Qt::ISODateWithMs));
    EXPECT_EQ(count(http, "PUT"), 0);  // taking theirs doesn't push them back
    ASSERT_FALSE(http.requests.empty());
    EXPECT_EQ(http.requests.front().method, QByteArrayLiteral("GET"));
    EXPECT_EQ(http.requests.front().authorization, QByteArrayLiteral("Bearer bld_pat_test"));
    EXPECT_EQ(sync.host(), QStringLiteral("127.0.0.1:%1").arg(http.base().port()));
}

TEST_F(PrefsSyncTest, PullPushesMineWhenTheyAreNewer) {
    PrefsStore store(QString::fromLatin1(kGroup));
    AppPrefs mine;
    mine.accent = QStringLiteral("plum");
    mine.largeText = true;
    store.update(mine);

    FakeHttp http;
    const QString saved = QStringLiteral("2031-01-02T03:04:05.678Z");
    http.reply(kPath, 200, answer(serverPrefs(QStringLiteral("forest"), QStringLiteral("light")),
                                  QStringLiteral("2020-05-01T00:00:00.000Z")));
    AppPrefs echoed = store.prefs();
    http.reply(kPath, 200, answer(echoed.toJson(), saved));  // the PUT's answer

    PrefsSync sync(store);
    sync.start(http.base(), QStringLiteral("bld_pat_test"));
    ASSERT_TRUE(waitFor([&] { return count(http, "PUT") == 1 && store.prefs().updatedAt.date().year() == 2031; }));

    EXPECT_EQ(store.prefs().accent, QStringLiteral("plum"));  // mine kept
    const auto& put = http.requests.back();
    const QJsonObject sent = QJsonDocument::fromJson(put.body).object().value(QStringLiteral("prefs")).toObject();
    EXPECT_EQ(sent.value(QStringLiteral("accent")).toString(), QStringLiteral("plum"));
    EXPECT_TRUE(sent.value(QStringLiteral("largeText")).toBool());
    EXPECT_EQ(sent.keys().size(), 6);
    // The server's time is kept, so the next pull doesn't bounce them.
    EXPECT_EQ(store.prefs().updatedAt, QDateTime::fromString(saved, Qt::ISODateWithMs));
}

TEST_F(PrefsSyncTest, ChangesPushOnceAfterAPauseAndNotAfterStop) {
    PrefsStore store(QString::fromLatin1(kGroup));  // never changed
    FakeHttp http;
    http.reply(kPath, 200, answer(serverPrefs(QStringLiteral("brick"), QStringLiteral("system")), QString()));

    PrefsSync sync(store);
    sync.setPushDelay(150);
    int synced = 0;
    QObject::connect(&sync, &PrefsSync::synced, [&synced] { ++synced; });
    sync.start(http.base(), QStringLiteral("bld_pat_test"));
    ASSERT_TRUE(waitFor([&] { return synced == 1; }));
    EXPECT_EQ(count(http, "PUT"), 0);  // nothing set on either side yet

    http.clear(kPath);
    http.reply(kPath, 200, answer(serverPrefs(QStringLiteral("forest"), QStringLiteral("dark")),
                                  QStringLiteral("2031-01-01T00:00:00.000Z")));
    AppPrefs p = store.prefs();
    p.accent = QStringLiteral("ocean");
    store.update(p);
    p.accent = QStringLiteral("forest");
    p.theme = ThemeChoice::Dark;
    store.update(p);
    ASSERT_TRUE(waitFor([&] { return count(http, "PUT") >= 1; }));
    settle(400);
    EXPECT_EQ(count(http, "PUT"), 1);
    const QJsonObject sent =
        QJsonDocument::fromJson(http.requests.back().body).object().value(QStringLiteral("prefs")).toObject();
    EXPECT_EQ(sent.value(QStringLiteral("accent")).toString(), QStringLiteral("forest"));
    EXPECT_EQ(sent.value(QStringLiteral("theme")).toString(), QStringLiteral("dark"));

    sync.stop();
    EXPECT_TRUE(sync.host().isEmpty());
    p.accent = QStringLiteral("plum");
    store.update(p);
    settle(400);
    EXPECT_EQ(count(http, "PUT"), 1);
}

TEST_F(PrefsSyncTest, EachServerIsRememberedByItsHost) {
    PrefsStore store(QString::fromLatin1(kGroup));
    FakeHttp one, two;
    one.reply(kPath, 200, answer(serverPrefs(QStringLiteral("forest"), QStringLiteral("dark")),
                                 QStringLiteral("2030-01-01T00:00:00.000Z")));
    two.reply(kPath, 200, answer(serverPrefs(QStringLiteral("plum"), QStringLiteral("light")),
                                 QStringLiteral("2031-06-01T00:00:00.000Z")));
    EXPECT_NE(PrefsSync::hostKey(one.base()), PrefsSync::hostKey(two.base()));
    EXPECT_EQ(PrefsSync::hostKey(QUrl(QStringLiteral("https://Layouts.Example.org"))),
              QStringLiteral("servers/layouts.example.org"));

    PrefsSync sync(store);
    int synced = 0;
    QObject::connect(&sync, &PrefsSync::synced, [&synced] { ++synced; });
    sync.start(one.base(), QStringLiteral("t1"));
    ASSERT_TRUE(waitFor([&] { return synced == 1; }));
    sync.start(two.base(), QStringLiteral("t2"));
    ASSERT_TRUE(waitFor([&] { return synced == 2; }));

    EXPECT_EQ(PrefsSync::syncedAt(one.base()).date().year(), 2030);
    EXPECT_EQ(PrefsSync::syncedAt(two.base()).date().year(), 2031);
    EXPECT_EQ(PrefsSync::syncedPrefs(one.base()).value(QStringLiteral("accent")).toString(), QStringLiteral("forest"));
    EXPECT_EQ(PrefsSync::syncedPrefs(two.base()).value(QStringLiteral("accent")).toString(), QStringLiteral("plum"));
    EXPECT_EQ(store.prefs().accent, QStringLiteral("plum"));  // the newest won
}

TEST_F(PrefsSyncTest, ThePartsPictureSizeGoesOnlyToAServerThatKeepsIt) {
    // An older server refuses unknown keys, so it never gets partsIconSize
    // (PullPushesMineWhenTheyAreNewer: six keys). A newer one answers with it.
    PrefsStore store(QString::fromLatin1(kGroup));
    AppPrefs mine;
    mine.partsIconSize = 40;
    store.update(mine);

    FakeHttp http;
    QJsonObject theirs = serverPrefs(QStringLiteral("brick"), QStringLiteral("system"));
    theirs.insert(QStringLiteral("partsIconSize"), 128);
    http.reply(kPath, 200, answer(theirs, QStringLiteral("2020-01-01T00:00:00.000Z")));
    http.reply(kPath, 200, answer(store.prefs().toJson(), QStringLiteral("2031-01-01T00:00:00.000Z")));
    PrefsSync sync(store);
    sync.start(http.base(), QStringLiteral("bld_pat_test"));
    ASSERT_TRUE(waitFor([&] { return count(http, "PUT") == 1; }));
    const QJsonObject sent =
        QJsonDocument::fromJson(http.requests.back().body).object().value(QStringLiteral("prefs")).toObject();
    EXPECT_EQ(sent.value(QStringLiteral("partsIconSize")).toInt(), 40);  // mine were newer
    EXPECT_EQ(sent.keys().size(), 7);

    // The server's newer size is taken in.
    http.clear(kPath);
    theirs.insert(QStringLiteral("partsIconSize"), 152);
    http.reply(kPath, 200, answer(theirs, QStringLiteral("2032-01-01T00:00:00.000Z")));
    int synced = 0;
    QObject::connect(&sync, &PrefsSync::synced, [&synced] { ++synced; });
    sync.start(http.base(), QStringLiteral("bld_pat_test"));
    ASSERT_TRUE(waitFor([&] { return synced > 0 && store.prefs().partsIconSize == 152; }));
}

TEST_F(PrefsSyncTest, ATokenWithoutTheScopeSaysSo) {
    PrefsStore store(QString::fromLatin1(kGroup));
    FakeHttp http;
    http.reply(kPath, 403, QJsonObject{ { QStringLiteral("error"), QStringLiteral("insufficient_scope") } });
    PrefsSync sync(store);
    bool unauthorized = false;
    int failures = 0;
    QObject::connect(&sync, &PrefsSync::failed, [&](const QString&, bool u) {
        ++failures;
        unauthorized = u;
    });
    sync.start(http.base(), QStringLiteral("old"));
    ASSERT_TRUE(waitFor([&] { return failures == 1; }));
    EXPECT_TRUE(unauthorized);
    EXPECT_EQ(store.prefs().accent, QStringLiteral("brick"));
}
