// The server half of the Module library: the API calls, the live hints,
// a module's contents, the "On the server" and "Catalog" tabs, and the main
// window adding, saving, republishing and changing server modules, and
// showing your warnings.

#include "FakeHttp.h"

#include "EventStream.h"
#include "LibraryApi.h"
#include "ModuleDoc.h"
#include "ServerList.h"
#include "TokenStore.h"
#include "core/Ids.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "edit/EditCommands.h"
#include "edit/ModuleCommands.h"
#include "edit/ModuleLibraryLink.h"
#include "parts/PartsLibrary.h"
#include "sync/Credit.h"
#include "ui/MainWindow.h"
#include "ui/ConfirmDialog.h"
#include "ui/MapView.h"
#include "ui/ModuleLibraryDialogs.h"
#include "ui/ModulesPanel.h"
#include "ui/ModuleLibraryPanel.h"
#include "ui/NoticeArea.h"
#include "ui/SaveModuleDialog.h"
#include "ui/SheetChoiceDialog.h"
#include "ui/ServerLibrary.h"
#include "ui/ServerWindow.h"
#include "ui/UpdateCheck.h"
#include "ui/VenueLibraryPanel.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QInputDialog>
#include <QKeySequence>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFrame>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QUrlQuery>

using namespace bld;
using bld::synctest::FakeHttp;

namespace {

bool waitFor(const std::function<bool()>& done, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

QJsonObject moduleJson(const QString& id, const QString& title, const QString& role, const QString& org = {},
                       qint64 thumbnailAt = 0, int version = 1) {
    QJsonObject owner = org.isEmpty() ? QJsonObject{ { QStringLiteral("kind"), QStringLiteral("user") }, { QStringLiteral("id"), QStringLiteral("u1") },
                                                      { QStringLiteral("name"), QStringLiteral("Ann") } }
                                      : QJsonObject{ { QStringLiteral("kind"), QStringLiteral("org") }, { QStringLiteral("id"), org },
                                                      { QStringLiteral("name"), QStringLiteral("Train Club") }, { QStringLiteral("slug"), QStringLiteral("train") } };
    QJsonObject m{ { QStringLiteral("id"), id }, { QStringLiteral("title"), title }, { QStringLiteral("role"), role },
                   { QStringLiteral("owner"), owner }, { QStringLiteral("docVersion"), version },
                   { QStringLiteral("latestVersion"), version }, { QStringLiteral("updatedAt"), 1790000000000.0 } };
    m.insert(QStringLiteral("thumbnailAt"), thumbnailAt ? QJsonValue(static_cast<double>(thumbnailAt)) : QJsonValue());
    return m;
}

QJsonObject modules(const QJsonArray& list) { return { { QStringLiteral("modules"), list } }; }
QJsonObject orgs() {
    return { { QStringLiteral("orgs"), QJsonArray{ QJsonObject{ { QStringLiteral("slug"), QStringLiteral("train") },
                                                                 { QStringLiteral("name"), QStringLiteral("Train Club") },
                                                                 { QStringLiteral("myRole"), QStringLiteral("member") } } } } };
}
QJsonObject catalogOn(bool on = true) { return { { QStringLiteral("modules"), on }, { QStringLiteral("parts"), on } }; }

// A module of two parts sheets, with parts the bundled library has.
std::unique_ptr<core::Map> sampleModule(int tracks = 2) {
    auto map = std::make_unique<core::Map>();
    auto add = [&](const QString& name, const QString& part, int n, double x) {
        auto l = std::make_unique<core::LayerBrick>();
        l->guid = core::newBbmId();
        l->name = name;
        for (int i = 0; i < n; ++i) {
            core::Brick b;
            b.guid = core::newBbmId();
            b.partNumber = part;
            b.displayArea = QRectF(x + i * 32, 0, 32, 32);
            l->bricks.push_back(b);
        }
        map->layers().push_back(std::move(l));
    };
    add(QStringLiteral("Baseplates"), QStringLiteral("3811.2"), 1, 0);
    add(QStringLiteral("Tracks"), QStringLiteral("3857.0"), tracks, 100);
    map->nbItems = sync::partCount(*map);
    return map;
}

QByteArray snapshotOf(const core::Map& m) { return sync::moduleSnapshotFor(m, {}); }

parts::PartsLibrary& bundledParts() {
    static parts::PartsLibrary lib;
    static bool scanned = false;
    if (!scanned) {
        lib.addSearchPath(QStringLiteral(BLD_SOURCE_DIR "/parts/BlueBrickParts/parts"));
        lib.scan();
        scanned = true;
    }
    return lib;
}

const synctest::Request* lastRequest(const FakeHttp& http, const QByteArray& method, const QByteArray& pathStart) {
    for (auto it = http.requests.rbegin(); it != http.requests.rend(); ++it)
        if (it->method == method && it->path.startsWith(pathStart)) return &*it;
    return nullptr;
}

int count(const FakeHttp& http, const QByteArray& method, const QByteArray& pathStart) {
    int n = 0;
    for (const auto& r : http.requests)
        if (r.method == method && r.path.startsWith(pathStart)) ++n;
    return n;
}

}  // namespace

// ---------- LibraryApi ------------------------------------------------------

TEST(LibraryApi, ListsModulesWithOwnersRolesVersionsAndPictures) {
    FakeHttp http;
    http.reply("/api/modules", 200,
               modules({ moduleJson(QStringLiteral("m1"), QStringLiteral("Yard"), QStringLiteral("owner"), {}, 1234, 3),
                         moduleJson(QStringLiteral("m2"), QStringLiteral("Shed"), QStringLiteral("viewer"), QStringLiteral("o1")) }));
    sync::LibraryApi api;
    api.setBase(http.base());
    api.setToken(QStringLiteral("bld_pat_x"));
    QList<sync::ServerModule> got;
    api.listModules([&](const QList<sync::ServerModule>& l) { got = l; }, {});
    ASSERT_TRUE(waitFor([&] { return got.size() == 2; }));
    EXPECT_EQ(http.requests.back().authorization, QByteArray("Bearer bld_pat_x"));
    EXPECT_EQ(got[0].title, QStringLiteral("Yard"));
    EXPECT_TRUE(got[0].canEdit());
    EXPECT_TRUE(got[0].canDelete());
    EXPECT_EQ(got[0].shownVersion(), 3);
    EXPECT_EQ(got[0].thumbnailPath(), QStringLiteral("/api/modules/m1/thumbnail?v=1234"));
    EXPECT_FALSE(got[0].owner.isClub());
    EXPECT_TRUE(got[1].owner.isClub());
    EXPECT_EQ(got[1].owner.name, QStringLiteral("Train Club"));
    EXPECT_FALSE(got[1].canEdit());
    EXPECT_TRUE(got[1].thumbnailPath().isEmpty());
}

TEST(LibraryApi, SavesANewVersionWithItsNoteAsBytes) {
    FakeHttp http;
    http.reply("/api/modules/m1/snapshot?note=Longer%20siding", 200, { { QStringLiteral("version"), 4 } });
    sync::LibraryApi api;
    api.setBase(http.base());
    int version = 0;
    api.saveModuleSnapshot(QStringLiteral("m1"), QByteArray("\x01\x02", 2), QStringLiteral("  Longer siding "),
                           [&](int v) { version = v; }, {});
    ASSERT_TRUE(waitFor([&] { return version == 4; }));
    EXPECT_EQ(http.requests.back().method, QByteArray("PUT"));
    EXPECT_EQ(http.requests.back().body, QByteArray("\x01\x02", 2));
}

TEST(LibraryApi, AcknowledgesANoticeAndReadsTheList) {
    FakeHttp http;
    http.reply("/api/notices", 200,
               { { QStringLiteral("notices"),
                   QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("w1") }, { QStringLiteral("scope"), QStringLiteral("club") },
                                            { QStringLiteral("severity"), QStringLiteral("final") }, { QStringLiteral("reason"), QStringLiteral("Stop it") },
                                            { QStringLiteral("acknowledgedAt"), QJsonValue() },
                                            { QStringLiteral("club"), QJsonObject{ { QStringLiteral("name"), QStringLiteral("Train Club") } } },
                                            { QStringLiteral("to"), QJsonObject{ { QStringLiteral("kind"), QStringLiteral("org") }, { QStringLiteral("name"), QStringLiteral("Train Club") } } } } } } });
    http.reply("/api/notices/w1/acknowledge", 200, { { QStringLiteral("ok"), true } });
    sync::LibraryApi api;
    api.setBase(http.base());
    QList<sync::Notice> got;
    api.notices([&](const QList<sync::Notice>& l) { got = l; }, {});
    ASSERT_TRUE(waitFor([&] { return !got.isEmpty(); }));
    EXPECT_FALSE(got[0].acknowledged);
    EXPECT_EQ(got[0].severity, QStringLiteral("final"));
    EXPECT_EQ(got[0].clubName, QStringLiteral("Train Club"));
    EXPECT_EQ(got[0].toClub, QStringLiteral("Train Club"));
    bool acked = false;
    api.acknowledge(QStringLiteral("w1"), [&] { acked = true; }, {});
    ASSERT_TRUE(waitFor([&] { return acked; }));
    EXPECT_EQ(http.requests.back().method, QByteArray("POST"));
}

TEST(LibraryApi, ARefusalSaysWhy) {
    FakeHttp http;
    http.reply("/api/modules", 403, { { QStringLiteral("error"), QStringLiteral("only_club_admins_can_add") } });
    sync::LibraryApi api;
    api.setBase(http.base());
    QString why;
    api.createModule(QStringLiteral("Yard"), QStringLiteral("train"), {}, [&](const sync::ServerRefusal& r) {
        why = ui::ServerLibrary::refusalText(r);
    });
    ASSERT_TRUE(waitFor([&] { return !why.isEmpty(); }));
    EXPECT_EQ(why, QStringLiteral("In this club, only admins and managers can add modules and parts."));
    EXPECT_TRUE(http.requests.back().body.contains("\"orgSlug\":\"train\""));
}

// ---------- Live hints --------------------------------------------------------

TEST(EventStreamParser, ReadsDataMessagesAcrossChunksAndSkipsComments) {
    sync::EventStreamParser p;
    EXPECT_TRUE(p.feed("retry: 5000\n\n: connected\n\n").isEmpty());
    EXPECT_EQ(p.retryMs(), 5000);
    EXPECT_TRUE(p.feed("data: {\"kind\":\"mod").isEmpty());
    const auto got = p.feed("ule\",\"id\":\"m1\"}\r\n\r\n: ping\n\ndata: not json\n\ndata: {\"kind\":\"warning\"}\n\n");
    ASSERT_EQ(got.size(), 2);
    EXPECT_EQ(got[0].value(QLatin1String("kind")).toString(), QStringLiteral("module"));
    EXPECT_EQ(got[0].value(QLatin1String("id")).toString(), QStringLiteral("m1"));
    EXPECT_EQ(got[1].value(QLatin1String("kind")).toString(), QStringLiteral("warning"));
}

TEST(EventStream, SendsHintsReconnectsAndStopsWhenRefused) {
    // A server that streams one hint per connection and then hangs up;
    // the third connection is refused.
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));
    int connections = 0;
    QByteArray auth;
    QObject::connect(&server, &QTcpServer::newConnection, [&] {
        while (QTcpSocket* s = server.nextPendingConnection()) {
            QObject::connect(s, &QTcpSocket::readyRead, s, [&, s] {
                const QByteArray req = s->readAll();
                if (!req.contains("\r\n\r\n")) return;
                for (const QByteArray& l : req.split('\n'))
                    if (l.toLower().startsWith("authorization:")) auth = l.mid(14).trimmed();
                ++connections;
                if (connections >= 3) {
                    s->write("HTTP/1.1 401 X\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                } else {
                    const QByteArray body = "retry: 10\n\n: connected\n\ndata: {\"kind\":\"module\",\"n\":" + QByteArray::number(connections) + "}\n\n";
                    s->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: " + QByteArray::number(body.size()) +
                             "\r\nConnection: close\r\n\r\n" + body);
                }
                s->disconnectFromHost();
            });
            QObject::connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        }
    });
    sync::EventStream stream;
    stream.setReconnectDelay(20, 40);
    QList<int> hints;
    int reconnects = 0, refused = 0;
    QObject::connect(&stream, &sync::EventStream::hint, [&](const QJsonObject& h) { hints << h.value(QLatin1String("n")).toInt(); });
    QObject::connect(&stream, &sync::EventStream::reconnected, [&] { ++reconnects; });
    QObject::connect(&stream, &sync::EventStream::refused, [&](int status) { refused = status; });
    stream.start(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort())), QStringLiteral("bld_pat_live"));
    ASSERT_TRUE(waitFor([&] { return refused != 0; }));
    EXPECT_EQ(hints, (QList<int>{ 1, 2 }));
    EXPECT_EQ(reconnects, 1);
    EXPECT_EQ(refused, 401);
    EXPECT_FALSE(stream.running());
    EXPECT_EQ(auth, QByteArray("Bearer bld_pat_live"));
}

// ---------- A module's contents ----------------------------------------------

TEST(ModuleDoc, AMapSavedAsAModuleReadsBackSheetBySheet) {
    auto module = sampleModule(3);
    const QByteArray bytes = snapshotOf(*module);
    ASSERT_FALSE(bytes.isEmpty());
    QString error;
    auto back = sync::mapFromModuleSnapshot(bytes, &error);
    ASSERT_TRUE(back) << error.toStdString();
    ASSERT_EQ(back->layers().size(), 2u);
    EXPECT_EQ(back->layers()[0]->name, QStringLiteral("Baseplates"));
    EXPECT_EQ(back->layers()[1]->name, QStringLiteral("Tracks"));
    EXPECT_EQ(sync::partCount(*back), 4);
    EXPECT_EQ(static_cast<const core::LayerBrick&>(*back->layers()[1]).bricks[2].displayArea, QRectF(164, 0, 32, 32));
}

TEST(ModuleDoc, ANewVersionReplacesWhatTheServerHeld) {
    const QByteArray first = snapshotOf(*sampleModule(3));
    auto smaller = sampleModule(1);
    const QByteArray second = sync::moduleSnapshotFor(*smaller, first);
    auto back = sync::mapFromModuleSnapshot(second);
    ASSERT_TRUE(back);
    EXPECT_EQ(sync::partCount(*back), 2);
    QString error;
    EXPECT_FALSE(sync::mapFromModuleSnapshot(QByteArray("nonsense"), &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_TRUE(sync::moduleSnapshotFor(*smaller, QByteArray("nonsense")).isEmpty());
}

// ---------- The tabs ----------------------------------------------------------

class LibraryTabs : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        cache_ = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/library-test");
        QDir(cache_).removeRecursively();
        library_.setPictureCacheRoot(cache_);
        http_.reply("/api/orgs", 200, orgs());
        http_.reply("/api/catalog/settings", 200, catalogOn());
    }
    void TearDown() override { QDir(cache_).removeRecursively(); }
    void serveModules() {
        http_.reply("/api/modules", 200,
                    modules({ moduleJson(QStringLiteral("c1"), QStringLiteral("Engine shed"), QStringLiteral("editor"), QStringLiteral("o1")),
                              moduleJson(QStringLiteral("m1"), QStringLiteral("Freight yard"), QStringLiteral("owner"), {}, 77),
                              moduleJson(QStringLiteral("m2"), QStringLiteral("Station"), QStringLiteral("viewer")) }));
    }
    void connectSignedIn() {
        library_.setServer(http_.base(), QStringLiteral("Club server"), QStringLiteral("bld_pat_t"));
        ASSERT_TRUE(waitFor([&] { return library_.state() == ui::ServerLibrary::State::Ready; }));
        QCoreApplication::processEvents();
    }
    // The visible texts of the tab, top to bottom (headers and titles).
    QStringList texts(QWidget* tab) {
        QStringList out;
        QCoreApplication::processEvents();
        QList<QLabel*> labels = tab->findChildren<QLabel*>();
        std::stable_sort(labels.begin(), labels.end(), [](QLabel* a, QLabel* b) {
            return a->mapTo(a->window(), QPoint()).y() < b->mapTo(b->window(), QPoint()).y();
        });
        for (QLabel* l : labels)
            if (l->isVisibleTo(tab) && (l->property("groupHeader").toBool() || l->property("rowTitle").toBool())) out << l->text();
        return out;
    }
    FakeHttp http_;
    ui::ServerLibrary library_;
    QString cache_;
};

TEST_F(LibraryTabs, YoursFirstThenEachClubWithVersionsAndPictures) {
    serveModules();
    QByteArray png;
    {
        QImage img(8, 8, QImage::Format_ARGB32);
        img.fill(Qt::red);
        QBuffer b(&png);
        b.open(QIODevice::WriteOnly);
        img.save(&b, "PNG");
    }
    http_.replyRaw("/api/modules/m1/thumbnail?v=77&size=small", 200, png, "image/png");
    ui::ServerModulesTab tab(library_);
    tab.resize(320, 700);
    tab.show();
    connectSignedIn();
    EXPECT_EQ(texts(&tab), (QStringList{ QStringLiteral("Yours"), QStringLiteral("Freight yard"), QStringLiteral("Station"),
                                         QStringLiteral("Train Club"), QStringLiteral("Engine shed") }));
    QWidget* yard = tab.row(QStringLiteral("m1"));
    ASSERT_NE(yard, nullptr);
    EXPECT_EQ(yard->findChild<QLabel*>(QStringLiteral("rowSubtitle"))->text().left(5), QStringLiteral("v1 · "));
    EXPECT_EQ(yard->findChild<QPushButton*>(QStringLiteral("insertModule"))->text(), QStringLiteral("Add to layout"));
    // Its picture comes once, then from the cache folder.
    auto* pic = yard->findChild<QLabel*>(QStringLiteral("rowPicture"));
    ASSERT_TRUE(waitFor([&] { return !pic->pixmap().isNull(); }));
    EXPECT_EQ(count(http_, "GET", "/api/modules/m1/thumbnail"), 1);
    QWidget* before = tab.row(QStringLiteral("m1"));
    library_.refresh();
    ASSERT_TRUE(waitFor([&] {
        QWidget* now = tab.row(QStringLiteral("m1"));
        return now && now != before && !now->findChild<QLabel*>(QStringLiteral("rowPicture"))->pixmap().isNull();
    }));
    EXPECT_EQ(count(http_, "GET", "/api/modules/m1/thumbnail"), 1);
    // A viewer may look, not change.
    auto* more = tab.row(QStringLiteral("m2"))->findChild<QToolButton*>(QStringLiteral("moduleMore"));
    EXPECT_FALSE(more->menu()->findChild<QAction*>(QStringLiteral("openModule"))->isEnabled());
    // Filtering.
    tab.filter()->setText(QStringLiteral("shed"));
    EXPECT_EQ(texts(&tab), (QStringList{ QStringLiteral("Train Club"), QStringLiteral("Engine shed") }));
    tab.filter()->setText(QStringLiteral("nothing like it"));
    EXPECT_NE(tab.findChild<QLabel*>(QStringLiteral("serverModulesEmpty")), nullptr);
}

TEST_F(LibraryTabs, APictureOnItsWayDuringARefreshStillArrives) {
    serveModules();
    QByteArray png;
    {
        QImage img(8, 8, QImage::Format_ARGB32);
        img.fill(Qt::red);
        QBuffer b(&png);
        b.open(QIODevice::WriteOnly);
        img.save(&b, "PNG");
    }
    http_.replyRaw("/api/modules/m1/thumbnail?v=77&size=small", 200, png, "image/png");
    ui::ServerModulesTab tab(library_);
    tab.resize(320, 700);
    tab.show();
    connectSignedIn();
    // A live hint refreshes the list while the picture is still coming.
    ASSERT_TRUE(waitFor([&] { return count(http_, "GET", "/api/modules/m1/thumbnail") == 1; }, 5000));
    library_.refresh();
    ASSERT_TRUE(waitFor([&] {
        QWidget* row = tab.row(QStringLiteral("m1"));
        return row && !row->findChild<QLabel*>(QStringLiteral("rowPicture"))->pixmap().isNull();
    }));
    EXPECT_EQ(count(http_, "GET", "/api/modules/m1/thumbnail"), 1);
}

TEST_F(LibraryTabs, APictureTheServerHasntGotIsAskedForOnce) {
    // m1 says it has a picture, but the server answers 404 (FakeHttp's default).
    serveModules();
    ui::ServerModulesTab tab(library_);
    tab.resize(320, 700);
    tab.show();
    connectSignedIn();
    ASSERT_TRUE(waitFor([&] { return count(http_, "GET", "/api/modules/m1/thumbnail") == 1; }));
    waitFor([] { return false; }, 300);  // its 404 lands
    // Every refresh (each live hint) redraws the rows: still asked only once,
    // so a show's shared address never sends a burst of 404s.
    for (int i = 0; i < 3; ++i) {
        QWidget* before = tab.row(QStringLiteral("m1"));
        library_.refresh();
        ASSERT_TRUE(waitFor([&] { QWidget* now = tab.row(QStringLiteral("m1")); return now && now != before; }));
    }
    QCoreApplication::processEvents();
    EXPECT_EQ(count(http_, "GET", "/api/modules/m1/thumbnail"), 1);
    // Signing in again asks afresh.
    library_.setServer(http_.base(), QStringLiteral("Club server"), QStringLiteral("bld_pat_u"));
    ASSERT_TRUE(waitFor([&] { return count(http_, "GET", "/api/modules/m1/thumbnail") == 2; }));
}

TEST_F(LibraryTabs, TheModuleBeingChangedSaysEditingNowAndTheOthersGoIntoIt) {
    serveModules();
    ui::ServerModulesTab tab(library_);
    connectSignedIn();
    QString inserted;
    QObject::connect(&library_, &ui::ServerLibrary::insertRequested, [&](const QString& id) { inserted = id; });
    library_.setEditingModule(QStringLiteral("m1"));
    QWidget* yard = tab.row(QStringLiteral("m1"));
    ASSERT_NE(yard, nullptr);
    EXPECT_NE(yard->findChild<QLabel*>(QStringLiteral("editingNow")), nullptr);
    EXPECT_EQ(yard->findChild<QPushButton*>(QStringLiteral("insertModule")), nullptr);
    auto* add = tab.row(QStringLiteral("c1"))->findChild<QPushButton*>(QStringLiteral("insertModule"));
    EXPECT_EQ(add->text(), QStringLiteral("Add to this module"));
    add->click();
    EXPECT_EQ(inserted, QStringLiteral("c1"));
    library_.setEditingModule({});
    EXPECT_EQ(tab.row(QStringLiteral("m1"))->findChild<QLabel*>(QStringLiteral("editingNow")), nullptr);
}

TEST_F(LibraryTabs, RenameAndDeleteAskTheServerThenListAgain) {
    serveModules();
    http_.reply("/api/modules/m1", 200, { { QStringLiteral("ok"), true } });
    ui::ServerModulesTab tab(library_);
    tab.askName = [](const sync::ServerModule&) { return QStringLiteral("Big yard"); };
    tab.confirmDelete = [](const sync::ServerModule&) { return true; };
    connectSignedIn();
    auto menuAction = [&](const char* name) {
        return tab.row(QStringLiteral("m1"))->findChild<QToolButton*>(QStringLiteral("moduleMore"))->menu()->findChild<QAction*>(QString::fromLatin1(name));
    };
    const int lists = count(http_, "GET", "/api/modules");
    menuAction("renameModule")->trigger();
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "PATCH", "/api/modules/m1") && count(http_, "GET", "/api/modules") > lists; }));
    EXPECT_TRUE(lastRequest(http_, "PATCH", "/api/modules/m1")->body.contains("\"title\":\"Big yard\""));
    ASSERT_TRUE(waitFor([&] { return library_.state() == ui::ServerLibrary::State::Ready && tab.row(QStringLiteral("m1")); }));
    menuAction("deleteModule")->trigger();
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "DELETE", "/api/modules/m1"); }));
}

// Author credit: "by Sam · in Train Club", and the club's module back to its author.
TEST(Credit, ReadsTheServersCreditAndSaysItInTheWebsitesWords) {
    const QJsonObject item{ { QStringLiteral("credit"),
                              QJsonObject{ { QStringLiteral("by"), QStringLiteral("Sam") },
                                           { QStringLiteral("authorName"), QStringLiteral("Sam") },
                                           { QStringLiteral("club"), QStringLiteral("ArkLUG") },
                                           { QStringLiteral("basedOn"),
                                             QJsonObject{ { QStringLiteral("id"), QStringLiteral("x") },
                                                          { QStringLiteral("title"), QStringLiteral("Yard") },
                                                          { QStringLiteral("by"), QStringLiteral("Bo") } } },
                                           { QStringLiteral("canTakeBack"), true },
                                           { QStringLiteral("canGiveBack"), false } } } };
    const sync::Credit c = sync::creditFromJson(item);
    EXPECT_TRUE(c.canTakeBack);
    EXPECT_FALSE(c.canGiveBack);
    EXPECT_EQ(sync::creditLine(c), QStringLiteral("by Sam · in ArkLUG · based on Yard by Bo"));
    sync::Credit mine;
    mine.by = QStringLiteral("you");
    EXPECT_EQ(sync::creditLine(mine), QStringLiteral("by you"));
    // Older servers send none: nothing shown, nothing offered.
    const sync::Credit none = sync::creditFromJson(QJsonObject{});
    EXPECT_FALSE(none.any());
    EXPECT_TRUE(sync::creditLine(none).isEmpty());
    const auto w = sync::moveToClubWording(QStringLiteral("ArkLUG"));
    EXPECT_EQ(w.removes,
              QStringLiteral("ArkLUG will own this. Its admins and managers can change or delete it."));
    EXPECT_EQ(
        w.keeps,
        QStringLiteral("You stay credited as the author, and you can take it back while you’re a member."));
}

namespace {
QJsonObject withCredit(QJsonObject m, const QString& by, bool take, bool give) {
    m.insert(QStringLiteral("credit"), QJsonObject{ { QStringLiteral("by"), by },
                                                    { QStringLiteral("authorName"), QStringLiteral("Sam") },
                                                    { QStringLiteral("club"), QStringLiteral("Train Club") },
                                                    { QStringLiteral("canTakeBack"), take },
                                                    { QStringLiteral("canGiveBack"), give } });
    return m;
}
} // namespace

TEST_F(LibraryTabs, TheClubsModuleShowsItsAuthorAndGoesBackAfterAsking) {
    http_.reply("/api/modules", 200,
                modules({ withCredit(moduleJson(QStringLiteral("c1"), QStringLiteral("Engine shed"),
                                                QStringLiteral("editor"), QStringLiteral("o1")),
                                     QStringLiteral("you"), true, false),
                          withCredit(moduleJson(QStringLiteral("c2"), QStringLiteral("Turntable"),
                                                QStringLiteral("owner"), QStringLiteral("o1")),
                                     QStringLiteral("Sam"), false, true),
                          moduleJson(QStringLiteral("m1"), QStringLiteral("Freight yard"),
                                     QStringLiteral("owner")) }));
    http_.reply("/api/modules/c1/take-back", 200,
                { { QStringLiteral("ok"), true }, { QStringLiteral("keptCopyId"), QStringLiteral("c9") } });
    http_.reply("/api/modules/c2/give-back", 200,
                { { QStringLiteral("ok"), true }, { QStringLiteral("keptCopyId"), QStringLiteral("c8") } });
    ui::ServerModulesTab tab(library_);
    QList<std::pair<QString, bool>> asked;
    bool answer = false;
    tab.confirmReturn = [&](const sync::ServerModule& m, bool give) {
        asked.append({ m.title, give });
        return answer;
    };
    connectSignedIn();
    EXPECT_EQ(tab.row(QStringLiteral("c1"))->findChild<QLabel*>(QStringLiteral("rowCredit"))->text(),
              QStringLiteral("by you · in Train Club"));
    EXPECT_EQ(tab.row(QStringLiteral("c2"))->findChild<QLabel*>(QStringLiteral("rowCredit"))->text(),
              QStringLiteral("by Sam · in Train Club"));
    EXPECT_EQ(tab.row(QStringLiteral("m1"))->findChild<QLabel*>(QStringLiteral("rowCredit")),
              nullptr); // no credit sent
    const auto menu = [&](const char* row) {
        return tab.row(QString::fromLatin1(row))
            ->findChild<QToolButton*>(QStringLiteral("moduleMore"))
            ->menu();
    };
    // The author takes theirs back; a club's runner gives one back. Neither shows where it doesn't apply.
    EXPECT_EQ(menu("c1")->findChild<QAction*>(QStringLiteral("giveBackModule")), nullptr);
    EXPECT_EQ(menu("c2")->findChild<QAction*>(QStringLiteral("takeBackModule")), nullptr);
    EXPECT_EQ(menu("m1")->findChild<QAction*>(QStringLiteral("takeBackModule")), nullptr);
    EXPECT_EQ(menu("c2")->findChild<QAction*>(QStringLiteral("giveBackModule"))->text(),
              QStringLiteral("Give Back to Sam…"));

    // Cancel: nothing is sent.
    menu("c1")->findChild<QAction*>(QStringLiteral("takeBackModule"))->trigger();
    EXPECT_FALSE(
        waitFor([&] { return lastRequest(http_, "POST", "/api/modules/c1/take-back") != nullptr; }, 300));
    // Take back.
    answer = true;
    const int lists = count(http_, "GET", "/api/modules");
    menu("c1")->findChild<QAction*>(QStringLiteral("takeBackModule"))->trigger();
    ASSERT_TRUE(waitFor([&] {
        return lastRequest(http_, "POST", "/api/modules/c1/take-back")
               && count(http_, "GET", "/api/modules") > lists;
    }));
    EXPECT_EQ(lastRequest(http_, "POST", "/api/modules/c1/take-back")->authorization,
              QByteArray("Bearer bld_pat_t"));
    ASSERT_TRUE(waitFor([&] {
        return library_.state() == ui::ServerLibrary::State::Ready && tab.row(QStringLiteral("c2"));
    }));
    menu("c2")->findChild<QAction*>(QStringLiteral("giveBackModule"))->trigger();
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "POST", "/api/modules/c2/give-back") != nullptr; }));
    EXPECT_EQ(lastRequest(http_, "POST", "/api/modules/c2/take-back"), nullptr);
    EXPECT_EQ(asked, (QList<std::pair<QString, bool>>{ { QStringLiteral("Engine shed"), false },
                                                       { QStringLiteral("Engine shed"), false },
                                                       { QStringLiteral("Turntable"), true } }));
}

TEST_F(LibraryTabs, SignedOutOfflineAndRefusedEachSayWhatToDo) {
    ui::ServerModulesTab tab(library_);
    tab.show();
    auto* line = tab.findChild<ui::LibraryStatusLine*>();
    ASSERT_NE(line, nullptr);
    // No server yet.
    EXPECT_EQ(library_.state(), ui::ServerLibrary::State::NoServer);
    EXPECT_EQ(line->button()->text(), QStringLiteral("Add a server…"));
    // Not signed in: Sign in.
    library_.setServer(http_.base(), QStringLiteral("Club server"), QString());
    EXPECT_FALSE(line->isHidden());
    EXPECT_EQ(line->button()->text(), QStringLiteral("Sign in"));
    EXPECT_TRUE(line->text()->text().contains(QStringLiteral("Sign in to Club server")));
    int asked = 0;
    QObject::connect(&library_, &ui::ServerLibrary::signInRequested, [&] { ++asked; });
    line->button()->click();
    EXPECT_EQ(asked, 1);
    // A token the server no longer takes: back to Sign in.
    http_.reply("/api/modules", 401, { { QStringLiteral("error"), QStringLiteral("invalid_token") } });
    library_.setServer(http_.base(), QStringLiteral("Club server"), QStringLiteral("bld_pat_old"));
    ASSERT_TRUE(waitFor([&] { return library_.state() == ui::ServerLibrary::State::SignedOut; }));
    // A 403: why, and Try again.
    http_.clear("/api/modules");
    http_.reply("/api/modules", 403, { { QStringLiteral("error"), QStringLiteral("insufficient_scope") } });
    library_.setServer(http_.base(), QStringLiteral("Club server"), QStringLiteral("bld_pat_narrow"));
    ASSERT_TRUE(waitFor([&] { return library_.state() == ui::ServerLibrary::State::Refused; }));
    EXPECT_TRUE(line->text()->text().contains(QStringLiteral("Sign in again")));
    EXPECT_EQ(line->button()->text(), QStringLiteral("Try again"));
    // Offline: Try again asks once more, and the list comes.
    QTcpServer gone;
    gone.listen(QHostAddress::LocalHost, 0);
    const QUrl unreachable(QStringLiteral("http://127.0.0.1:%1").arg(gone.serverPort()));
    gone.close();
    library_.setServer(unreachable, QStringLiteral("Away"), QStringLiteral("bld_pat_t"));
    ASSERT_TRUE(waitFor([&] { return library_.state() == ui::ServerLibrary::State::Offline; }));
    EXPECT_TRUE(line->text()->text().contains(QStringLiteral("Can't reach Away")));
    EXPECT_EQ(line->button()->text(), QStringLiteral("Try again"));
}

TEST_F(LibraryTabs, CatalogAddsModulesAndPartsAndWholeCollections) {
    serveModules();
    http_.reply("/api/catalog/items?kind=module&sort=popular", 200,
                { { QStringLiteral("items"),
                    QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("i1") }, { QStringLiteral("kind"), QStringLiteral("module") },
                                             { QStringLiteral("title"), QStringLiteral("Harbour crane") }, { QStringLiteral("by"), QStringLiteral("Bob") },
                                             { QStringLiteral("uses"), 1 }, { QStringLiteral("version"), 1 },
                                             { QStringLiteral("previewUrl"), QStringLiteral("/api/catalog/items/i1/preview?v=1") } } } } });
    http_.reply("/api/catalog/items?kind=part&sort=popular", 200,
                { { QStringLiteral("items"),
                    QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("p1") }, { QStringLiteral("kind"), QStringLiteral("part") },
                                             { QStringLiteral("title"), QStringLiteral("Signal") }, { QStringLiteral("by"), QStringLiteral("Bob") },
                                             { QStringLiteral("uses"), 3 } } } } });
    http_.reply("/api/catalog/items/i1/add", 201, { { QStringLiteral("kind"), QStringLiteral("module") }, { QStringLiteral("id"), QStringLiteral("m9") } });
    http_.reply("/api/catalog/items/p1/add", 201, { { QStringLiteral("kind"), QStringLiteral("part") }, { QStringLiteral("id"), QStringLiteral("cp9") } });
    http_.reply("/api/catalog/collections", 200,
                { { QStringLiteral("collections"),
                    QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("k1") }, { QStringLiteral("title"), QStringLiteral("Harbour starter") },
                                             { QStringLiteral("by"), QStringLiteral("Bob") }, { QStringLiteral("featured"), true },
                                             { QStringLiteral("itemCount"), 3 }, { QStringLiteral("modules"), 2 }, { QStringLiteral("parts"), 1 } } } } });
    http_.reply("/api/catalog/collections/k1/add", 200,
                { { QStringLiteral("added"), QJsonArray{ QJsonObject{}, QJsonObject{} } }, { QStringLiteral("skipped"), QJsonArray{ QStringLiteral("x") } },
                  { QStringLiteral("failed"), QJsonArray{} } });
    ui::CatalogTab tab(library_);
    // Asked where it goes (you're in a club): to the club.
    QStringList asked;
    tab.chooseOwner = [&](const QString& title) -> std::optional<QString> {
        asked << title;
        return QStringLiteral("train");
    };
    connectSignedIn();
    ASSERT_TRUE(waitFor([&] { return tab.row(QStringLiteral("i1")); }));
    QString toInsert;
    QObject::connect(&library_, &ui::ServerLibrary::catalogInsertRequested, [&](const sync::CatalogItem& it) { toInsert = it.id; });
    tab.row(QStringLiteral("i1"))->findChild<QPushButton*>(QStringLiteral("catalogInsert"))->click();
    EXPECT_EQ(toInsert, QStringLiteral("i1"));
    tab.row(QStringLiteral("i1"))->findChild<QPushButton*>(QStringLiteral("catalogAdd"))->click();
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "POST", "/api/catalog/items/i1/add"); }));
    EXPECT_TRUE(lastRequest(http_, "POST", "/api/catalog/items/i1/add")->body.contains("\"orgSlug\":\"train\""));
    EXPECT_EQ(asked.value(0), QStringLiteral("Add to my modules"));
    // A refresh (a live change, back in the window) rebuilds the rows but
    // keeps what Add did, instead of offering it again.
    const auto addText = [&] {
        QWidget* r = tab.row(QStringLiteral("i1"));
        return r ? r->findChild<QPushButton*>(QStringLiteral("catalogAdd"))->text() : QString();
    };
    ASSERT_TRUE(waitFor([&] { return addText() == QStringLiteral("Added"); }));
    QWidget* before = tab.row(QStringLiteral("i1"));
    library_.refresh();
    ASSERT_TRUE(waitFor([&] { return tab.row(QStringLiteral("i1")) && tab.row(QStringLiteral("i1")) != before; }));
    EXPECT_EQ(addText(), QStringLiteral("Added"));
    EXPECT_FALSE(tab.row(QStringLiteral("i1"))->findChild<QPushButton*>(QStringLiteral("catalogAdd"))->isEnabled());

    // Parts: added, then the server's parts are fetched.
    int partsAdded = 0;
    QObject::connect(&library_, &ui::ServerLibrary::partsAdded, [&] { ++partsAdded; });
    tab.setKind(ui::CatalogTab::Kind::Parts);
    ASSERT_TRUE(waitFor([&] { return tab.row(QStringLiteral("p1")); }));
    EXPECT_EQ(tab.row(QStringLiteral("p1"))->findChild<QPushButton*>(QStringLiteral("catalogInsert")), nullptr);
    tab.row(QStringLiteral("p1"))->findChild<QPushButton*>(QStringLiteral("catalogAdd"))->click();
    ASSERT_TRUE(waitFor([&] { return partsAdded == 1; }));

    // Collections: Add all, and what happened.
    tab.setKind(ui::CatalogTab::Kind::Collections);
    ASSERT_TRUE(waitFor([&] { return tab.row(QStringLiteral("k1")); }));
    tab.row(QStringLiteral("k1"))->findChild<QPushButton*>(QStringLiteral("collectionAddAll"))->click();
    ASSERT_TRUE(waitFor([&] { return tab.note()->text() == QStringLiteral("Added 2 items · already had 1"); }));
    EXPECT_EQ(partsAdded, 2);
}

TEST_F(LibraryTabs, CatalogLayoutsAndVenuesShowWhenOnAndCopyThenOpen) {
    serveModules();
    // Modules on; layouts and venues off at first: no tabs for them.
    http_.clear("/api/catalog/settings");
    http_.reply("/api/catalog/settings", 200, { { QStringLiteral("modules"), true }, { QStringLiteral("parts"), false } });
    http_.reply("/api/catalog/items?kind=module&sort=popular", 200, { { QStringLiteral("items"), QJsonArray{} } });
    http_.reply("/api/catalog/items?kind=layout&sort=popular", 200,
                { { QStringLiteral("items"),
                    QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("l1") }, { QStringLiteral("kind"), QStringLiteral("layout") },
                                             { QStringLiteral("title"), QStringLiteral("Harbour town") }, { QStringLiteral("by"), QStringLiteral("Bob · in ArkLUG") },
                                             { QStringLiteral("uses"), 2 }, { QStringLiteral("version"), 1 },
                                             { QStringLiteral("previewUrl"), QStringLiteral("/api/catalog/items/l1/preview?v=1") },
                                             { QStringLiteral("coverUrl"), QStringLiteral("/api/catalog/items/l1/cover?image=c1&size=small") },
                                             { QStringLiteral("summary"), QJsonObject{ { QStringLiteral("widthStuds"), 960 }, { QStringLiteral("heightStuds"), 480 },
                                                                                       { QStringLiteral("partCount"), 1204 } } } } } } });
    http_.reply("/api/catalog/items?kind=venue&sort=popular", 200,
                { { QStringLiteral("items"),
                    QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("v1") }, { QStringLiteral("kind"), QStringLiteral("venue") },
                                             { QStringLiteral("title"), QStringLiteral("Town hall") }, { QStringLiteral("by"), QStringLiteral("Bob") },
                                             { QStringLiteral("uses"), 0 }, { QStringLiteral("version"), 1 },
                                             { QStringLiteral("previewUrl"), QStringLiteral("/api/catalog/items/v1/preview?v=1") },
                                             { QStringLiteral("summary"), QJsonObject{ { QStringLiteral("widthStuds"), 400 }, { QStringLiteral("heightStuds"), 300 } } } } } } });
    http_.reply("/api/catalog/items/l1/add", 201, { { QStringLiteral("kind"), QStringLiteral("layout") }, { QStringLiteral("id"), QStringLiteral("L9") } });
    http_.reply("/api/catalog/items/v1/add", 201, { { QStringLiteral("kind"), QStringLiteral("venue") }, { QStringLiteral("id"), QStringLiteral("V9") } });
    ui::CatalogTab tab(library_);
    tab.chooseOwner = [](const QString&) -> std::optional<QString> { return QString(); };
    auto kindButton = [&](ui::CatalogTab::Kind k) {
        return tab.findChild<QPushButton*>(QStringLiteral("catalogKind%1").arg(static_cast<int>(k)));
    };
    connectSignedIn();
    ASSERT_TRUE(waitFor([&] { return library_.catalog().modules; }));
    QCoreApplication::processEvents();
    EXPECT_FALSE(kindButton(ui::CatalogTab::Kind::Layouts)->isVisibleTo(&tab));
    EXPECT_FALSE(kindButton(ui::CatalogTab::Kind::Venues)->isVisibleTo(&tab));

    // The admin turns them on: the tabs show after the next refresh.
    http_.clear("/api/catalog/settings");
    http_.reply("/api/catalog/settings", 200,
                { { QStringLiteral("modules"), true }, { QStringLiteral("parts"), false }, { QStringLiteral("layouts"), true }, { QStringLiteral("venues"), true } });
    library_.refresh();
    ASSERT_TRUE(waitFor([&] { return library_.catalog().venues; }));
    QCoreApplication::processEvents();
    EXPECT_TRUE(kindButton(ui::CatalogTab::Kind::Layouts)->isVisibleTo(&tab));
    EXPECT_TRUE(kindButton(ui::CatalogTab::Kind::Venues)->isVisibleTo(&tab));

    // A layout: its size and parts, its cover, and "Open a copy" copies it, then asks to open the copy.
    tab.setKind(ui::CatalogTab::Kind::Layouts);
    ASSERT_TRUE(waitFor([&] { return tab.row(QStringLiteral("l1")); }));
    QStringList rowTexts;
    for (QLabel* l : tab.row(QStringLiteral("l1"))->findChildren<QLabel*>()) rowTexts << l->text();
    // In the machine's own number format (1,204 / 1.204 / 1204).
    const QLocale here;
    EXPECT_TRUE(rowTexts.join(QLatin1Char('|'))
                    .contains(QStringLiteral("960 × 480 studs (%1 × %2 m) · %3 parts")
                                  .arg(here.toString(7.68, 'f', 1), here.toString(3.84, 'f', 1), here.toString(1204))))
        << rowTexts.join(QLatin1Char('|')).toStdString();
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "GET", "/api/catalog/items/l1/cover"); }));
    EXPECT_EQ(tab.row(QStringLiteral("l1"))->findChild<QPushButton*>(QStringLiteral("catalogInsert")), nullptr);
    QString opened;
    QObject::connect(&library_, &ui::ServerLibrary::catalogLayoutCopied, [&](const QString& id, const QString&) { opened = id; });
    QPushButton* copy = tab.row(QStringLiteral("l1"))->findChild<QPushButton*>(QStringLiteral("catalogAdd"));
    EXPECT_EQ(copy->text(), QStringLiteral("Open a copy"));
    copy->click();
    ASSERT_TRUE(waitFor([&] { return opened == QStringLiteral("L9"); }));
    EXPECT_TRUE(tab.row(QStringLiteral("l1"))->findChild<QPushButton*>(QStringLiteral("catalogWeb")));

    // A venue: "Use this venue" copies it, then asks to use the copy.
    tab.setKind(ui::CatalogTab::Kind::Venues);
    ASSERT_TRUE(waitFor([&] { return tab.row(QStringLiteral("v1")); }));
    QString used;
    QObject::connect(&library_, &ui::ServerLibrary::catalogVenueCopied, [&](const QString& id, const QString&) { used = id; });
    QPushButton* use = tab.row(QStringLiteral("v1"))->findChild<QPushButton*>(QStringLiteral("catalogAdd"));
    EXPECT_EQ(use->text(), QStringLiteral("Use this venue"));
    use->click();
    ASSERT_TRUE(waitFor([&] { return used == QStringLiteral("V9"); }));
}

TEST(LibraryApi, ReadsALayoutsSizePartsAndCover) {
    const auto it = sync::LibraryApi::catalogItemFromJson(
        { { QStringLiteral("id"), QStringLiteral("l1") }, { QStringLiteral("kind"), QStringLiteral("layout") },
          { QStringLiteral("previewUrl"), QStringLiteral("/p") }, { QStringLiteral("coverUrl"), QStringLiteral("/c") },
          { QStringLiteral("summary"), QJsonObject{ { QStringLiteral("widthStuds"), 96 }, { QStringLiteral("heightStuds"), 8 }, { QStringLiteral("partCount"), 3 } } } });
    EXPECT_EQ(it.picturePath(), QStringLiteral("/c"));
    EXPECT_EQ(it.widthStuds, 96);
    EXPECT_EQ(it.heightStuds, 8);
    EXPECT_EQ(it.partCount, 3);
    // A module from an older server: its drawn picture, no size.
    const auto old = sync::LibraryApi::catalogItemFromJson({ { QStringLiteral("previewUrl"), QStringLiteral("/p") } });
    EXPECT_EQ(old.picturePath(), QStringLiteral("/p"));
    EXPECT_EQ(old.partCount, -1);
}

TEST_F(LibraryTabs, ACatalogThatIsOffSaysSo) {
    serveModules();
    http_.clear("/api/catalog/settings");
    http_.reply("/api/catalog/settings", 200, catalogOn(false));
    ui::CatalogTab tab(library_);
    connectSignedIn();
    EXPECT_TRUE(tab.note()->text().contains(QStringLiteral("isn't turned on")));
}

// ---------- The main window --------------------------------------------------

namespace {

class Win : public ui::MainWindow {
public:
    using ui::MainWindow::MainWindow;
    using ui::MainWindow::setTokenStore;
};

}  // namespace

class ServerModulesWindow : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ui::UpdateCheck::setCheckAtStartupEnabled(false);
        QSettings().remove(QStringLiteral("sync/serverAddress"));
        {
            sync::ServerList list;
            list.add(http_.base(), QStringLiteral("Club server"));
            // A server without parts to download (the bundled library has these).
            sync::ServerInfo info;
            info.features = QStringList{ QStringLiteral("liveSync") };
            list.remember(http_.base(), info);
            list.touch(http_.base());
            list.save();
        }
        tokens_->save(http_.base(), QStringLiteral("bld_pat_win"));
        http_.reply("/api/orgs", 200, orgs());
        http_.reply("/api/catalog/settings", 200, catalogOn());
        http_.reply("/api/notices", 200, { { QStringLiteral("notices"), QJsonArray{} } });
        // The live hints: nothing, then the sign-in no longer works there.
        http_.reply("/api/events", 401, { { QStringLiteral("error"), QStringLiteral("invalid_token") } });
        beforeOpen();
        window_ = std::make_unique<Win>(bundledParts());
        window_->setTokenStore(tokens_);
        window_->ensureDocument();
        view_ = window_->findChild<ui::MapView*>();
        library_ = window_->serverLibrary();
        ASSERT_NE(library_, nullptr);
        ASSERT_TRUE(waitFor([&] { return library_->state() == ui::ServerLibrary::State::Ready; })) << library_->stateText().toStdString();
    }
    void TearDown() override {
        if (view_) view_->undoStack()->setClean();
        window_.reset();
        QSettings().setValue(QLatin1String(sync::ServerList::kKey), QByteArray("[]"));
        QStandardPaths::setTestModeEnabled(false);
    }
    virtual void beforeOpen() {
        http_.reply("/api/modules", 200,
                    modules({ moduleJson(QStringLiteral("m1"), QStringLiteral("Freight yard"), QStringLiteral("owner")),
                              moduleJson(QStringLiteral("c1"), QStringLiteral("Engine shed"), QStringLiteral("editor"), QStringLiteral("o1")) }));
        http_.replyRaw("/api/modules/m1/snapshot", 200, snapshotOf(*sampleModule()), "application/octet-stream");
    }
    int bricksInMap() { return sync::partCount(*view_->currentMap()); }

    FakeHttp http_;
    std::shared_ptr<sync::MemoryTokenStore> tokens_ = std::make_shared<sync::MemoryTokenStore>();
    std::unique_ptr<Win> window_;
    ui::MapView* view_ = nullptr;
    ui::ServerLibrary* library_ = nullptr;
};

TEST_F(ServerModulesWindow, TheLibraryShowsTheServersModulesAsItsOwnTab) {
    auto* panel = window_->findChild<ui::ModuleLibraryPanel*>();
    ASSERT_EQ(panel->tabs()->count(), 3);
    EXPECT_EQ(panel->tabs()->tabText(1), QStringLiteral("Club server"));
    EXPECT_EQ(library_->modules().size(), 2);
    EXPECT_EQ(lastRequest(http_, "GET", "/api/modules")->authorization, QByteArray("Bearer bld_pat_win"));
}

TEST_F(ServerModulesWindow, TheStatusBarSaysHowTheServerIsAndOpensTheServerWindow) {
    auto* status = window_->findChild<QToolButton*>(QStringLiteral("serverStatus"));
    ASSERT_NE(status, nullptr);
    EXPECT_EQ(status->text(), QStringLiteral("Connected to Club server"));
    EXPECT_EQ(status->property("tourTarget").toString(), QStringLiteral("servers.status"));
    // Not signed in.
    library_->setServer(http_.base(), QStringLiteral("Club server"), QString());
    EXPECT_EQ(status->text(), QStringLiteral("Signed out of Club server"));
    // Can't be reached.
    QTcpServer gone;
    gone.listen(QHostAddress::LocalHost, 0);
    const QUrl unreachable(QStringLiteral("http://127.0.0.1:%1").arg(gone.serverPort()));
    gone.close();
    library_->setServer(unreachable, QStringLiteral("Away"), QStringLiteral("bld_pat_t"));
    ASSERT_TRUE(waitFor([&] { return library_->state() == ui::ServerLibrary::State::Offline; }));
    EXPECT_EQ(status->text(), QStringLiteral("Away is offline"));
    // None at all.
    library_->setServer({}, {}, {});
    EXPECT_EQ(status->text(), QStringLiteral("No server"));

    // A click opens the Server window, on the server it shows.
    library_->setServer(http_.base(), QStringLiteral("Club server"), QStringLiteral("bld_pat_win"));
    ASSERT_TRUE(waitFor([&] { return status->text() == QStringLiteral("Connected to Club server"); }));
    status->click();
    ASSERT_TRUE(waitFor([&] { return window_->serverWindow() && window_->serverWindow()->isVisible(); }));
    window_->serverWindow()->close();
}

TEST_F(ServerModulesWindow, UseThisVenueKeepsTheCopyAndStartsALayoutInIt) {
    auto* venues = window_->findChild<ui::VenueLibraryPanel*>();
    ASSERT_NE(venues, nullptr);
    const QDir dir(venues->libraryPath());
    const QStringList before = dir.entryList({ QStringLiteral("*.bld-venue") }, QDir::Files);
    const QJsonObject venue{ { QStringLiteral("name"), QStringLiteral("Town hall") },
                             { QStringLiteral("enabled"), true },
                             { QStringLiteral("minWalkwayStuds"), 30 },
                             { QStringLiteral("bounds"), QJsonObject{ { QStringLiteral("x"), 0 }, { QStringLiteral("y"), 0 }, { QStringLiteral("w"), 400 }, { QStringLiteral("h"), 300 } } },
                             { QStringLiteral("edges"), QJsonArray{ QJsonObject{ { QStringLiteral("kind"), 0 }, { QStringLiteral("doorWidthStuds"), 0 }, { QStringLiteral("label"), QString() },
                                                                                  { QStringLiteral("poly"), QJsonArray{ QJsonObject{ { QStringLiteral("x"), 0 }, { QStringLiteral("y"), 0 } },
                                                                                                                        QJsonObject{ { QStringLiteral("x"), 400 }, { QStringLiteral("y"), 0 } } } } } } },
                             { QStringLiteral("obstacles"), QJsonArray{} } };
    http_.reply("/api/venues/V9", 200, { { QStringLiteral("id"), QStringLiteral("V9") }, { QStringLiteral("name"), QStringLiteral("Town hall") }, { QStringLiteral("data"), venue } });
    emit library_->catalogVenueCopied(QStringLiteral("V9"), QStringLiteral("Town hall"));
    ASSERT_TRUE(waitFor([&] { return view_->currentMap()->sidecar.venue.has_value(); }));
    EXPECT_EQ(view_->currentMap()->sidecar.venue->name, QStringLiteral("Town hall"));
    EXPECT_EQ(view_->currentMap()->sidecar.venue->edges.size(), 1);
    // The copy is kept in the Venue library too.
    QStringList added = dir.entryList({ QStringLiteral("*.bld-venue") }, QDir::Files);
    for (const QString& f : before) added.removeAll(f);
    ASSERT_EQ(added.size(), 1);
    EXPECT_TRUE(added.front().startsWith(QStringLiteral("Town hall")));
    QFile::remove(dir.filePath(added.front()));
}

// "Where should these go?" (the layout lacks the module's sheets): new
// sheets by their names, whenever it opens while the timer lives.
static std::unique_ptr<QTimer> answerNewSheets() {
    auto t = std::make_unique<QTimer>();
    QObject::connect(t.get(), &QTimer::timeout, [] {
        auto* d = qobject_cast<ui::SheetChoiceDialog*>(QApplication::activeModalWidget());
        if (!d) return;
        for (auto* box : d->findChildren<QComboBox*>(QStringLiteral("sheetChoice"))) box->setCurrentIndex(box->count() - 1);
        d->placeButton()->click();
    });
    t->start(20);
    return t;
}

TEST_F(ServerModulesWindow, AddToLayoutPutsTheModulesPartsInAsOneModule) {
    const auto answers = answerNewSheets();
    emit library_->insertRequested(QStringLiteral("m1"));
    ASSERT_TRUE(waitFor([&] { return bricksInMap() == 3; }));
    const core::Map& m = *view_->currentMap();
    ASSERT_EQ(m.sidecar.modules.size(), 1u);
    EXPECT_EQ(m.sidecar.modules.front().name, QStringLiteral("Freight yard"));
    EXPECT_EQ(m.sidecar.modules.front().memberIds.size(), 3);
    EXPECT_TRUE(m.sidecar.modules.front().sourceFile.endsWith(QStringLiteral("/modules/m1")));
    // Its sheets came along, and the parts are selected to move or turn.
    QStringList names;
    for (const auto& l : m.layers()) names << l->name;
    EXPECT_TRUE(names.contains(QStringLiteral("Tracks")));
    int selected = 0;
    for (QGraphicsItem* it : view_->scene()->selectedItems())
        if (it->data(2).toString() == QStringLiteral("brick")) ++selected;
    EXPECT_EQ(selected, 3);
    EXPECT_TRUE(library_->inserting().isEmpty());
    // Undo takes it all back.
    view_->undoStack()->undo();
    EXPECT_EQ(bricksInMap(), 0);
}

// Two parts on the map, picked, in no module yet.
static void placeLoose(ui::MapView* view, int tracks) {
    auto module = sampleModule(tracks);
    const auto answers = answerNewSheets();
    view->placeModule(*module, QStringLiteral("x"), QString(), QPointF(4000, 4000));
    core::Map& m = *view->currentMap();
    view->undoStack()->push(new edit::DeleteModuleCommand(m, m.sidecar.modules.front().id));
    for (QGraphicsItem* it : view->scene()->items())
        if (it->data(2).toString() == QStringLiteral("brick")) it->setSelected(true);
}

// Make a module: the picked parts become a module in this layout, and
// nothing goes to the server.
TEST_F(ServerModulesWindow, MakeAModuleKeepsItInThisLayoutOnly) {
    placeLoose(view_, 1);
    ASSERT_EQ(bricksInMap(), 2);
    QTimer::singleShot(0, [&] {
        auto* d = qobject_cast<ui::MakeModuleDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(d, nullptr);
        EXPECT_FALSE(d->alsoSaveBox()->isChecked());
        d->nameEdit()->setText(QStringLiteral("Yard"));
        d->accept();
    });
    QMetaObject::invokeMethod(window_.get(), "onMakeModule", Qt::DirectConnection);
    const core::Map& m = *view_->currentMap();
    ASSERT_EQ(m.sidecar.modules.size(), 1u);
    EXPECT_EQ(m.sidecar.modules.front().name, QStringLiteral("Yard"));
    EXPECT_EQ(m.sidecar.modules.front().memberIds.size(), 2);
    EXPECT_TRUE(m.sidecar.modules.front().libraryModuleId.isEmpty());
    EXPECT_EQ(count(http_, "POST", "/api/modules"), 0);
    EXPECT_EQ(count(http_, "PUT", "/api/modules"), 0);
}

// "Also save to my Module library": made, saved as a new module (centred, with its
// picture), and linked to it.
TEST_F(ServerModulesWindow, MakeAndSaveMakesANewModuleOnTheServerAndLinksIt) {
    // The server's empty layout for a new module.
    core::Map empty;
    http_.replyRaw("/api/modules/new1/snapshot", 200, snapshotOf(empty), "application/octet-stream");
    http_.reply("/api/modules/new1/snapshot", 200, { { QStringLiteral("version"), 1 } });
    http_.reply("/api/modules/new1/thumbnail", 200, { { QStringLiteral("ok"), true } });
    // POST and GET share the path: the create answers first.
    http_.clear("/api/modules");
    http_.reply("/api/modules", 201, { { QStringLiteral("id"), QStringLiteral("new1") }, { QStringLiteral("title"), QStringLiteral("Yard") } });
    http_.reply("/api/modules", 200, modules({ moduleJson(QStringLiteral("new1"), QStringLiteral("Yard"), QStringLiteral("owner"), {}, 5) }));
    placeLoose(view_, 1);
    QTimer::singleShot(0, [&] {
        auto* d = qobject_cast<ui::MakeModuleDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(d, nullptr);
        d->nameEdit()->setText(QStringLiteral("Yard"));
        d->alsoSaveBox()->setChecked(true);
        EXPECT_EQ(d->makeButton()->text(), QStringLiteral("Make and save"));
        EXPECT_EQ(d->saveTo()->itemText(1), QStringLiteral("Train Club"));
        d->saveTo()->setCurrentIndex(0);
        d->accept();
    });
    QMetaObject::invokeMethod(window_.get(), "onMakeModule", Qt::DirectConnection);
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "PUT", "/api/modules/new1/thumbnail"); }));
    const auto* created = lastRequest(http_, "POST", "/api/modules");
    ASSERT_NE(created, nullptr);
    EXPECT_TRUE(created->body.contains("\"title\":\"Yard\""));
    EXPECT_FALSE(created->body.contains("orgSlug"));
    // The saved contents are the module, centred on the origin as the web saves them.
    const auto* put = lastRequest(http_, "PUT", "/api/modules/new1/snapshot");
    ASSERT_NE(put, nullptr);
    auto saved = sync::mapFromModuleSnapshot(put->body);
    ASSERT_TRUE(saved);
    EXPECT_EQ(sync::partCount(*saved), 2);
    QRectF box;
    for (const auto& l : saved->layers())
        for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks) box = box.isNull() ? b.displayArea : box.united(b.displayArea);
    EXPECT_NEAR(box.center().x(), 0, 0.01);
    EXPECT_NEAR(box.center().y(), 0, 0.01);
    // With its picture, as PNG.
    const QJsonObject thumb = QJsonDocument::fromJson(lastRequest(http_, "PUT", "/api/modules/new1/thumbnail")->body).object();
    EXPECT_EQ(thumb.value(QLatin1String("mime")).toString(), QStringLiteral("image/png"));
    QImage img;
    ASSERT_TRUE(img.loadFromData(QByteArray::fromBase64(thumb.value(QLatin1String("data")).toString().toLatin1())));
    EXPECT_GT(std::max(img.width(), img.height()), 256);
    // Linked.
    ASSERT_TRUE(waitFor([&] { return !view_->currentMap()->sidecar.modules.front().libraryModuleId.isEmpty(); }));
    EXPECT_EQ(view_->currentMap()->sidecar.modules.front().libraryModuleId, QStringLiteral("new1"));
    EXPECT_EQ(view_->currentMap()->sidecar.modules.front().libraryVersion, 1);
}

// Save to Module library… from a module's menu: parts on two sheets, the dialog
// says so, and "Put everything on one sheet" saves one sheet named after
// the one with the most parts. The module is linked to what was saved.
TEST_F(ServerModulesWindow, SaveToLibrarySaysWhichSheetsAndLinksTheModule) {
    core::Map empty;
    http_.replyRaw("/api/modules/new2/snapshot", 200, snapshotOf(empty), "application/octet-stream");
    http_.reply("/api/modules/new2/snapshot?note=Two", 200, { { QStringLiteral("version"), 1 } });
    http_.reply("/api/modules/new2/thumbnail", 200, { { QStringLiteral("ok"), true } });
    http_.clear("/api/modules");
    http_.reply("/api/modules", 201, { { QStringLiteral("id"), QStringLiteral("new2") }, { QStringLiteral("title"), QStringLiteral("Depot") } });
    http_.reply("/api/modules", 200, modules({ moduleJson(QStringLiteral("new2"), QStringLiteral("Depot"), QStringLiteral("owner"), {}, 5) }));
    auto module = sampleModule(2);  // Baseplates: 1 part, Tracks: 2
    const auto answers = answerNewSheets();
    view_->placeModule(*module, QStringLiteral("Depot here"), QString(), QPointF(4000, 4000));
    ASSERT_EQ(bricksInMap(), 3);
    const QString id = view_->currentMap()->sidecar.modules.front().id;
    // The module's menu offers Save to Module library….
    auto* panel = window_->findChild<ui::ModulesPanel*>();
    {
        QMenu menu;
        panel->fillMenu(menu, id);
        auto* save = menu.findChild<QAction*>(QStringLiteral("moduleLibrary_save"));
        ASSERT_NE(save, nullptr);
        EXPECT_EQ(menu.findChild<QAction*>(QStringLiteral("moduleLibrary_publish")), nullptr);
    }
    QString shown, oneShown, title;
    QTimer::singleShot(0, [&] {
        auto* d = qobject_cast<ui::SaveModuleDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(d, nullptr);
        title = d->windowTitle();
        EXPECT_EQ(d->nameEdit()->text(), QStringLiteral("Depot here"));
        EXPECT_TRUE(d->sheetsBox()->isVisibleTo(d));
        shown = d->sheetsLabel()->text();
        d->oneSheetBox()->setChecked(true);
        oneShown = d->sheetsLabel()->text();
        d->nameEdit()->setText(QStringLiteral("Depot"));
        d->noteEdit()->setText(QStringLiteral("Two"));
        d->accept();
    });
    emit panel->libraryActionRequested(id, QStringLiteral("save"));
    EXPECT_EQ(title, QStringLiteral("Save “Depot here” to the Module library"));
    EXPECT_EQ(shown, QStringLiteral("This module uses 2 sheets: <b>Baseplates, Tracks</b>."));
    EXPECT_EQ(oneShown, QStringLiteral("All its parts go on one sheet: <b>Tracks</b>."));
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "PUT", "/api/modules/new2/snapshot?note=Two"); }));
    auto saved = sync::mapFromModuleSnapshot(lastRequest(http_, "PUT", "/api/modules/new2/snapshot?note=Two")->body);
    ASSERT_TRUE(saved);
    ASSERT_EQ(saved->layers().size(), 1u);
    EXPECT_EQ(saved->layers().front()->name, QStringLiteral("Tracks"));
    EXPECT_EQ(saved->layers().front()->transparency, 100);
    EXPECT_EQ(sync::partCount(*saved), 3);
    ASSERT_TRUE(waitFor([&] { return view_->currentMap()->sidecar.modules.front().libraryModuleId == QStringLiteral("new2"); }));
    // The layout's sheets stay as they were.
    EXPECT_EQ(sync::partCount(*view_->currentMap()), 3);
    // Linked: the menu now offers Update Module library version.
    ASSERT_TRUE(waitFor([&] { return library_->module(QStringLiteral("new2")); }));
    QMenu menu;
    panel->fillMenu(menu, id);
    EXPECT_NE(menu.findChild<QAction*>(QStringLiteral("moduleLibrary_publish")), nullptr);
}

// The library module m1 at version `latest`, whose version 1 is `v1` and
// whose newest contents are `now`.
static void serveVersions(FakeHttp& http, int latest, const core::Map& v1, const core::Map& now) {
    http.clear("/api/modules");
    http.reply("/api/modules", 200, modules({ moduleJson(QStringLiteral("m1"), QStringLiteral("Freight yard"), QStringLiteral("owner"), {}, 0, latest) }));
    http.clear("/api/modules/m1/snapshot");
    http.replyRaw("/api/modules/m1/snapshot", 200, snapshotOf(now), "application/octet-stream");
    http.replyRaw("/api/modules/m1/versions/1/snapshot", 200, snapshotOf(v1), "application/octet-stream");
}

// Added from the Module library, the module is linked; Update Module library version
// saves the next version with its note; Update from Module library brings a newer
// version in where the module sits, without asking when it wasn't changed.
TEST_F(ServerModulesWindow, LinkedModulesUpdateTheLibraryAndFromIt) {
    const auto answers = answerNewSheets();
    emit library_->insertRequested(QStringLiteral("m1"));
    ASSERT_TRUE(waitFor([&] { return bricksInMap() == 3; }));
    const QString id = view_->currentMap()->sidecar.modules.front().id;
    EXPECT_EQ(view_->currentMap()->sidecar.modules.front().libraryModuleId, QStringLiteral("m1"));
    EXPECT_EQ(view_->currentMap()->sidecar.modules.front().libraryVersion, 1);

    // Update Module library version: version 2, with its note.
    http_.reply("/api/modules/m1/snapshot?note=More%20track", 200, { { QStringLiteral("version"), 2 } });
    http_.reply("/api/modules/m1/thumbnail", 200, { { QStringLiteral("ok"), true } });
    QString text;
    QTimer::singleShot(0, [&] {
        auto* d = qobject_cast<ui::PublishModuleDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(d, nullptr);
        text = d->text()->text();
        EXPECT_FALSE(d->newerWarning()->isVisibleTo(d));
        d->noteEdit()->setText(QStringLiteral("More track"));
        d->accept();
    });
    emit window_->findChild<ui::ModulesPanel*>()->libraryActionRequested(id, QStringLiteral("publish"));
    EXPECT_TRUE(text.contains(QStringLiteral("version 2 of <b>Freight yard</b>"))) << text.toStdString();
    ASSERT_TRUE(waitFor([&] { return view_->currentMap()->sidecar.modules.front().libraryVersion == 2; }));
    ASSERT_NE(lastRequest(http_, "PUT", "/api/modules/m1/snapshot?note=More%20track"), nullptr);

    // The library moves on to version 3, with a fourth part; this copy is version 1's.
    {
        core::Map& m = *view_->currentMap();
        core::Module linked = m.sidecar.modules.front();
        linked.libraryVersion = 1;
        view_->undoStack()->push(new edit::UpdateModuleCommand(m, linked, QStringLiteral("t")));
    }
    serveVersions(http_, 3, *sampleModule(2), *sampleModule(3));
    library_->refresh();
    ASSERT_TRUE(waitFor([&] { return library_->module(QStringLiteral("m1")) && library_->module(QStringLiteral("m1"))->latestVersion == 3; }));
    const auto before = edit::placedParts(*view_->currentMap(), view_->currentMap()->sidecar.modules.front().memberIds);
    int asked = 0;
    window_->confirmModuleLibrary_ = [&](const ui::ConfirmOptions&) { ++asked; return true; };
    emit window_->findChild<ui::ModulesPanel*>()->libraryActionRequested(id, QStringLiteral("pull"));
    ASSERT_TRUE(waitFor([&] { return bricksInMap() == 4; }));
    EXPECT_EQ(asked, 0);
    const core::Module& mod = view_->currentMap()->sidecar.modules.front();
    EXPECT_EQ(mod.id, id);
    EXPECT_EQ(mod.libraryVersion, 3);
    EXPECT_EQ(mod.memberIds.size(), 4);
    // The parts it had stay where they were.
    const auto after = edit::placedParts(*view_->currentMap(), mod.memberIds);
    for (const auto& b : before) {
        bool same = false;
        for (const auto& a : after)
            same = same || (a.partNumber == b.partNumber && std::abs(a.displayArea.x() - b.displayArea.x()) < 0.01 &&
                            std::abs(a.displayArea.y() - b.displayArea.y()) < 0.01);
        QString all;
        for (const auto& a : after) all += QStringLiteral(" %1@%2,%3").arg(a.partNumber).arg(a.displayArea.x()).arg(a.displayArea.y());
        EXPECT_TRUE(same) << b.partNumber.toStdString() << " at " << b.displayArea.x() << "," << b.displayArea.y() << " in" << all.toStdString();
    }
    // Undo puts the old parts back.
    view_->undoStack()->undo();
    EXPECT_EQ(bricksInMap(), 3);
    EXPECT_EQ(view_->currentMap()->sidecar.modules.front().libraryVersion, 1);
}

// Changed in this layout: Update from Module library asks first, and No keeps it.
TEST_F(ServerModulesWindow, UpdateFromLibraryAsksWhenTheModuleWasChangedHere) {
    const auto answers = answerNewSheets();
    emit library_->insertRequested(QStringLiteral("m1"));
    ASSERT_TRUE(waitFor([&] { return bricksInMap() == 3; }));
    const QString id = view_->currentMap()->sidecar.modules.front().id;
    serveVersions(http_, 3, *sampleModule(2), *sampleModule(3));
    library_->refresh();
    ASSERT_TRUE(waitFor([&] { return library_->module(QStringLiteral("m1")) && library_->module(QStringLiteral("m1"))->latestVersion == 3; }));
    // A part moves.
    {
        core::Map& m = *view_->currentMap();
        for (auto& l : m.layers())
            if (l->kind() == core::LayerKind::Brick && !static_cast<core::LayerBrick&>(*l).bricks.empty()) {
                static_cast<core::LayerBrick&>(*l).bricks.front().displayArea.translate(8, 0);
                break;
            }
    }
    QStringList asked;
    window_->confirmModuleLibrary_ = [&](const ui::ConfirmOptions& o) { asked << o.title; return false; };
    emit window_->findChild<ui::ModulesPanel*>()->libraryActionRequested(id, QStringLiteral("pull"));
    ASSERT_TRUE(waitFor([&] { return !asked.isEmpty(); }));
    EXPECT_EQ(asked, QStringList{ QStringLiteral("Replace “Freight yard” with version 3?") });
    EXPECT_EQ(bricksInMap(), 3);
    EXPECT_EQ(view_->currentMap()->sidecar.modules.front().libraryVersion, 1);
}

// A new module saved straight into a club asks first: the club will own it.
TEST_F(ServerModulesWindow, SavingANewModuleIntoAClubAsksFirst) {
    ui::SaveModuleDialog d(*library_, QString());
    QStringList asked;
    bool answer = false;
    d.confirmSaveToClub = [&](const QString& club) {
        asked << club;
        return answer;
    };
    d.nameEdit()->setText(QStringLiteral("Yard"));
    // Yours: nothing asked.
    d.accept();
    EXPECT_EQ(d.result(), QDialog::Accepted);
    EXPECT_TRUE(asked.isEmpty());
    d.setResult(QDialog::Rejected);
    const int club = d.saveTo()->findData(QStringLiteral("train"));
    ASSERT_GE(club, 0);
    d.saveTo()->setCurrentIndex(club);
    d.accept();
    EXPECT_EQ(asked, QStringList{ QStringLiteral("Train Club") });
    EXPECT_NE(d.result(), QDialog::Accepted);
    answer = true;
    d.accept();
    EXPECT_EQ(d.result(), QDialog::Accepted);
    EXPECT_EQ(d.choice().orgSlug, QStringLiteral("train"));
}

TEST_F(ServerModulesWindow, OpenToChangeItThenSaveMakesANewVersion) {
    http_.reply("/api/modules/m1/snapshot?note=More%20track", 200, { { QStringLiteral("version"), 2 } });
    http_.reply("/api/modules/m1/thumbnail", 200, { { QStringLiteral("ok"), true } });
    emit library_->openRequested(QStringLiteral("m1"));
    ASSERT_TRUE(waitFor([&] { return window_->editingModule().id == QStringLiteral("m1"); }));
    EXPECT_EQ(library_->editingModule(), QStringLiteral("m1"));
    EXPECT_EQ(bricksInMap(), 3);
    EXPECT_TRUE(window_->windowTitle().contains(QStringLiteral("Freight yard — Module on")));
    EXPECT_TRUE(window_->notices()->isShown(QStringLiteral("editing-module")));
    // Take one part out, then Save: a new version with its note.
    {
        core::Map& m = *view_->currentMap();
        for (int i = 0; i < static_cast<int>(m.layers().size()); ++i)
            if (m.layers()[i]->name == QStringLiteral("Baseplates")) {
                const auto& b = static_cast<core::LayerBrick&>(*m.layers()[i]).bricks;
                view_->undoStack()->push(new edit::DeleteBricksCommand(m, { { i, 0, b.front() } }));
                break;
            }
    }
    ASSERT_EQ(bricksInMap(), 2);
    QTimer::singleShot(0, [] {
        auto* d = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(d, nullptr);
        d->setTextValue(QStringLiteral("More track"));
        d->accept();
    });
    QAction* save = nullptr;
    for (QAction* a : window_->findChildren<QAction*>())
        if (a->shortcut() == QKeySequence::Save) save = a;
    ASSERT_NE(save, nullptr);
    save->trigger();
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "PUT", "/api/modules/m1/snapshot?note=More%20track"); }));
    auto saved = sync::mapFromModuleSnapshot(lastRequest(http_, "PUT", "/api/modules/m1/snapshot?note=More%20track")->body);
    ASSERT_TRUE(saved);
    EXPECT_EQ(sync::partCount(*saved), 2);
    ASSERT_TRUE(waitFor([&] { return view_->undoStack()->isClean(); }));
    // Another document: not changing the module any more.
    QMetaObject::invokeMethod(window_.get(), "onNew", Qt::DirectConnection);
    EXPECT_TRUE(window_->editingModule().id.isEmpty());
    EXPECT_TRUE(library_->editingModule().isEmpty());
}

class ServerModulesWarning : public ServerModulesWindow {
protected:
    void beforeOpen() override {
        ServerModulesWindow::beforeOpen();
        http_.clear("/api/notices");
        auto warning = [](bool acked) {
            return QJsonObject{ { QStringLiteral("notices"),
                                  QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("w1") }, { QStringLiteral("scope"), QStringLiteral("site") },
                                                           { QStringLiteral("severity"), QStringLiteral("warning") },
                                                           { QStringLiteral("reason"), QStringLiteral("Please keep titles friendly.") },
                                                           { QStringLiteral("link"), QStringLiteral("/catalog") },
                                                           { QStringLiteral("acknowledgedAt"), acked ? QJsonValue(1.0) : QJsonValue() } } } } };
        };
        http_.reply("/api/notices", 200, warning(false));
        http_.reply("/api/notices", 200, warning(true));
        http_.reply("/api/notices/w1/acknowledge", 200, { { QStringLiteral("ok"), true } });
    }
};

TEST_F(ServerModulesWarning, AWarningStaysUntilIUnderstand) {
    ui::NoticeArea* notices = window_->notices();
    ASSERT_TRUE(waitFor([&] { return notices->isShown(QStringLiteral("server-warning")); }));
    QFrame* card = notices->card(QStringLiteral("server-warning"));
    QStringList texts;
    for (QLabel* l : card->findChildren<QLabel*>()) texts << l->text();
    EXPECT_TRUE(texts.contains(QStringLiteral("Important: Warning · From the site team"))) << texts.join(QLatin1Char('|')).toStdString();
    EXPECT_TRUE(texts.contains(QStringLiteral("Please keep titles friendly.")));
    QPushButton* understand = nullptr;
    QStringList buttons;
    for (QPushButton* b : card->findChildren<QPushButton*>()) {
        buttons << b->text();
        if (b->text() == QStringLiteral("I understand")) understand = b;
    }
    EXPECT_TRUE(buttons.contains(QStringLiteral("See what it's about")));
    ASSERT_NE(understand, nullptr);
    understand->click();
    ASSERT_TRUE(waitFor([&] { return lastRequest(http_, "POST", "/api/notices/w1/acknowledge"); }));
    ASSERT_TRUE(waitFor([&] { return !notices->isShown(QStringLiteral("server-warning")); }));
}

class ServerModulesLive : public ServerModulesWindow {
protected:
    void beforeOpen() override {
        // Someone saves a module on the web: the hint, then the new list.
        http_.reply("/api/modules", 200, modules({ moduleJson(QStringLiteral("m1"), QStringLiteral("Freight yard"), QStringLiteral("owner")) }));
        http_.reply("/api/modules", 200,
                    modules({ moduleJson(QStringLiteral("m1"), QStringLiteral("Freight yard"), QStringLiteral("owner")),
                              moduleJson(QStringLiteral("m3"), QStringLiteral("Saved on the web"), QStringLiteral("owner")) }));
        http_.clear("/api/events");
        http_.replyRaw("/api/events", 200, "retry: 5000\n\n: connected\n\ndata: {\"kind\":\"module\",\"action\":\"create\"}\n\n",
                       "text/event-stream");
        http_.reply("/api/events", 401, { { QStringLiteral("error"), QStringLiteral("invalid_token") } });
    }
};

TEST_F(ServerModulesLive, AHintFromTheServerListsTheModulesAgain) {
    ASSERT_TRUE(waitFor([&] { return library_->module(QStringLiteral("m3")); }));
    EXPECT_GE(count(http_, "GET", "/api/events"), 1);
    EXPECT_EQ(lastRequest(http_, "GET", "/api/events")->authorization, QByteArray("Bearer bld_pat_win"));
}
