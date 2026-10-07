// File › Open from Server…: the Server window against the in-process HTTP
// server. Every tab's rows (picture, name, credit, size, date), every row
// action, the Show filter and its memory per server, the friendly states
// (no server, signed out, offline), live hints, the catalog tabs asking only
// when shown, and the main window's side: the menu item, the toolbar button,
// opening a layout live, keeping a venue and a module on this computer.

#include "FakeHttp.h"

#include "EventStream.h"
#include "LibraryApi.h"
#include "ModuleDoc.h"
#include "ServerList.h"
#include "TokenStore.h"
#include "core/Ids.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "ui/ConfirmDialog.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/ModuleLibraryPanel.h"
#include "ui/ServerLibrary.h"
#include "ui/SendToServerDialogs.h"
#include "ui/ServerWindow.h"
#include "ui/UpdateCheck.h"
#include "ui/VenueLibraryPanel.h"
#include "core/Venue.h"
#include "saveload/BbmWriter.h"
#include "saveload/VenueIO.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QBuffer>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTimer>
#include <QUndoStack>

using namespace bld;
using bld::synctest::FakeHttp;
using Tab = ui::ServerWindow::Tab;

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

int count(const FakeHttp& http, const QByteArray& method, const QByteArray& path) {
    int n = 0;
    for (const auto& r : http.requests)
        if (r.method == method && r.path == path) ++n;
    return n;
}

// Every request had an answer ready: nothing the window asked for drew a 404.
QStringList unanswered(const FakeHttp& http) {
    QStringList out;
    for (const auto& r : http.requests)
        if (!http.answers(r.path)) out << QString::fromLatin1(r.method + ' ' + r.path);
    return out;
}

QJsonObject credit(const char* by, const char* club = "", bool take = false, bool give = false) {
    return { { QStringLiteral("by"), QString::fromUtf8(by) },
             { QStringLiteral("authorName"), QStringLiteral("Sam") },
             { QStringLiteral("club"), QString::fromUtf8(club) },
             { QStringLiteral("canTakeBack"), take },
             { QStringLiteral("canGiveBack"), give } };
}

QJsonObject clubOwner() {
    return { { QStringLiteral("kind"), QStringLiteral("org") }, { QStringLiteral("id"), QStringLiteral("o1") },
             { QStringLiteral("name"), QStringLiteral("Train Club") }, { QStringLiteral("slug"), QStringLiteral("train") } };
}
QJsonObject myOwner() {
    return { { QStringLiteral("kind"), QStringLiteral("user") }, { QStringLiteral("id"), QStringLiteral("u1") },
             { QStringLiteral("name"), QStringLiteral("Ann") } };
}

constexpr double kWhen = 1790000000000.0;

QJsonObject layoutsJson() {
    return { { QStringLiteral("layouts"),
               QJsonArray{
                   QJsonObject{ { QStringLiteral("id"), QStringLiteral("L1") }, { QStringLiteral("title"), QStringLiteral("Fordyce 2026") },
                                { QStringLiteral("role"), QStringLiteral("owner") }, { QStringLiteral("updatedAt"), kWhen },
                                { QStringLiteral("owner"), myOwner() }, { QStringLiteral("credit"), credit("you") } },
                   QJsonObject{ { QStringLiteral("id"), QStringLiteral("L2") }, { QStringLiteral("title"), QStringLiteral("Club yard") },
                                { QStringLiteral("role"), QStringLiteral("editor") }, { QStringLiteral("updatedAt"), kWhen - 1000 },
                                { QStringLiteral("ownerOrgName"), QStringLiteral("Train Club") },
                                { QStringLiteral("ownerOrgSlug"), QStringLiteral("train") }, { QStringLiteral("owner"), clubOwner() },
                                { QStringLiteral("credit"), credit("you", "Train Club", true) } },
                   QJsonObject{ { QStringLiteral("id"), QStringLiteral("L3") }, { QStringLiteral("title"), QStringLiteral("Shared with me") },
                                { QStringLiteral("role"), QStringLiteral("viewer") }, { QStringLiteral("updatedAt"), kWhen - 2000 } } } } };
}

QJsonObject venuesJson() {
    return { { QStringLiteral("venues"),
               QJsonArray{
                   QJsonObject{ { QStringLiteral("id"), QStringLiteral("V1") }, { QStringLiteral("name"), QStringLiteral("Town hall") },
                                { QStringLiteral("ownerOrgId"), QJsonValue() }, { QStringLiteral("canManage"), true },
                                { QStringLiteral("createdAt"), kWhen }, { QStringLiteral("widthStuds"), 1250 },
                                { QStringLiteral("heightStuds"), 625 }, { QStringLiteral("credit"), credit("you") } },
                   QJsonObject{ { QStringLiteral("id"), QStringLiteral("V2") }, { QStringLiteral("name"), QStringLiteral("Club hall") },
                                { QStringLiteral("ownerOrgId"), QStringLiteral("o1") },
                                { QStringLiteral("ownerOrgName"), QStringLiteral("Train Club") },
                                { QStringLiteral("ownerOrgSlug"), QStringLiteral("train") }, { QStringLiteral("canManage"), false },
                                { QStringLiteral("credit"), credit("Sam", "Train Club", false, false) } } } } };
}

QJsonObject moduleJson(const char* id, const char* title, const char* role, bool club, qint64 thumbnailAt = 0) {
    return { { QStringLiteral("id"), QString::fromLatin1(id) }, { QStringLiteral("title"), QString::fromLatin1(title) },
             { QStringLiteral("role"), QString::fromLatin1(role) }, { QStringLiteral("owner"), club ? clubOwner() : myOwner() },
             { QStringLiteral("docVersion"), 3 }, { QStringLiteral("latestVersion"), 3 }, { QStringLiteral("updatedAt"), kWhen },
             { QStringLiteral("thumbnailAt"), thumbnailAt ? QJsonValue(static_cast<double>(thumbnailAt)) : QJsonValue() },
             { QStringLiteral("credit"), credit(club ? "Sam" : "you", club ? "Train Club" : "") } };
}

QJsonObject modulesJson() {
    return { { QStringLiteral("modules"),
               QJsonArray{ moduleJson("m1", "Freight yard", "owner", false, 77), moduleJson("c1", "Engine shed", "editor", true),
                           moduleJson("m2", "Station", "viewer", false) } } };
}

QJsonObject partsJson() {
    return { { QStringLiteral("parts"),
               QJsonArray{
                   QJsonObject{ { QStringLiteral("id"), QStringLiteral("p1") }, { QStringLiteral("partNumber"), QStringLiteral("CLD-SIG1") },
                                { QStringLiteral("displayName"), QStringLiteral("Signal box") }, { QStringLiteral("role"), QStringLiteral("owner") },
                                { QStringLiteral("owner"), myOwner() }, { QStringLiteral("spriteMime"), QStringLiteral("image/png") },
                                { QStringLiteral("updatedAt"), kWhen } },
                   QJsonObject{ { QStringLiteral("id"), QStringLiteral("p2") }, { QStringLiteral("partNumber"), QStringLiteral("CLD-TREE") },
                                { QStringLiteral("displayName"), QStringLiteral("Club tree") }, { QStringLiteral("role"), QStringLiteral("viewer") },
                                { QStringLiteral("owner"), clubOwner() }, { QStringLiteral("spriteMime"), QJsonValue() },
                                { QStringLiteral("updatedAt"), kWhen } } } } };
}

QJsonObject orgsJson() {
    return { { QStringLiteral("orgs"), QJsonArray{ QJsonObject{ { QStringLiteral("slug"), QStringLiteral("train") },
                                                                 { QStringLiteral("name"), QStringLiteral("Train Club") },
                                                                 { QStringLiteral("myRole"), QStringLiteral("member") } } } } };
}

QByteArray png() {
    QImage img(8, 8, QImage::Format_ARGB32);
    img.fill(Qt::red);
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes;
}

QStringList titles(ui::ServerWindow& w, Tab tab) {
    QCoreApplication::processEvents();
    QStringList out;
    const char* prefix = tab == Tab::Layouts ? "layoutRow:" : tab == Tab::Venues ? "venueRow:" : tab == Tab::Modules ? "moduleRow:" : "partRow:";
    for (QFrame* f : w.findChildren<QFrame*>()) {
        // The other tabs' pages are hidden, rows going away are too.
        if (!f->objectName().startsWith(QLatin1String(prefix)) || f->isHidden()) continue;
        if (w.row(tab, f->objectName().mid(static_cast<int>(qstrlen(prefix)))) != f) continue;
        out << f->findChild<QLabel*>(QStringLiteral("rowTitle"))->text();
    }
    out.sort();
    return out;
}

QStringList lines(QWidget* row) {
    QStringList out;
    for (QLabel* l : row->findChildren<QLabel*>(QStringLiteral("rowLine"))) out << l->text();
    return out;
}

QAction* moreAction(QWidget* row, const char* name) {
    auto* more = row->findChild<QToolButton*>(QStringLiteral("rowMore"));
    if (!more || !more->menu()) return nullptr;
    return more->menu()->findChild<QAction*>(QString::fromLatin1(name));
}

void pickShow(ui::ServerWindow& w, const QString& text) {
    QComboBox* show = w.showFilter();
    const int at = show->findText(text);
    ASSERT_GE(at, 0) << text.toStdString();
    show->setCurrentIndex(at);
    emit show->activated(at);
}

}  // namespace

class ServerWindowTest : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        cache_ = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/server-window-test");
        QDir(cache_).removeRecursively();
        library_.setPictureCacheRoot(cache_);
        QSettings().remove(QStringLiteral("serverWindow"));
        {
            sync::ServerList list;
            list.add(http_.base(), QStringLiteral("Club server"));
            sync::ServerInfo info;
            info.features = features();
            list.remember(http_.base(), info);
            list.rememberUser(http_.base(), QStringLiteral("Ann"));
            list.save();
        }
        http_.reply("/api/orgs", 200, orgsJson());
        http_.reply("/api/catalog/settings", 200,
                    { { QStringLiteral("modules"), true }, { QStringLiteral("parts"), true }, { QStringLiteral("layouts"), true },
                      { QStringLiteral("venues"), true } });
        http_.reply("/api/modules", 200, modulesJson());
        http_.reply("/api/layouts", 200, layoutsJson());
        http_.reply("/api/venues", 200, venuesJson());
        http_.reply("/api/custom-parts", 200, partsJson());
        http_.replyRaw("/api/modules/m1/thumbnail?v=77&size=small", 200, png(), "image/png");
        http_.replyRaw("/api/custom-parts/p1/sprite?v=1790000000000", 200, png(), "image/png");
        window_ = std::make_unique<ui::ServerWindow>(library_);
        window_->confirm = [this](const ui::ConfirmOptions& o) {
            asked_ << o.title;
            return answer_;
        };
    }
    void TearDown() override {
        window_.reset();
        QDir(cache_).removeRecursively();
        QSettings().setValue(QLatin1String(sync::ServerList::kKey), QByteArray("[]"));
        QSettings().remove(QStringLiteral("serverWindow"));
        QStandardPaths::setTestModeEnabled(false);
    }
    virtual QStringList features() { return { QStringLiteral("venues"), QStringLiteral("layoutDownload") }; }
    void signIn() {
        library_.setServer(http_.base(), QStringLiteral("Club server"), QStringLiteral("bld_pat_w"));
        ASSERT_TRUE(waitFor([&] { return library_.state() == ui::ServerLibrary::State::Ready; })) << library_.stateText().toStdString();
    }
    // Signed in, the window open on `tab`, every list in.
    void open(Tab tab = Tab::Layouts) {
        signIn();
        window_->showTab(tab);
        ASSERT_TRUE(waitFor([&] {
            return window_->row(Tab::Layouts, QStringLiteral("L1")) && window_->row(Tab::Venues, QStringLiteral("V1")) &&
                   window_->row(Tab::Parts, QStringLiteral("p1")) && window_->row(Tab::Modules, QStringLiteral("m1"));
        }));
    }

    FakeHttp http_;
    ui::ServerLibrary library_;
    std::unique_ptr<ui::ServerWindow> window_;
    QString cache_;
    QStringList asked_;
    bool answer_ = true;
};

TEST_F(ServerWindowTest, EveryTabListsYoursAndYourClubsWithPictureCreditSizeAndDate) {
    open();
    EXPECT_EQ(window_->windowTitle(), QStringLiteral("Open from Server"));
    EXPECT_EQ(window_->property("tourTarget").toString(), QStringLiteral("server.window"));
    QStringList tabs;
    for (int i = 0; i < window_->tabs()->count(); ++i) tabs << window_->tabs()->tabText(i);
    EXPECT_EQ(tabs, (QStringList{ QStringLiteral("Layouts"), QStringLiteral("Venues"), QStringLiteral("Modules"), QStringLiteral("Parts"),
                                  QStringLiteral("Catalog"), QStringLiteral("Collections") }));
    EXPECT_EQ(window_->serverPicker()->currentText(), QStringLiteral("Club server (Main)"));
    EXPECT_TRUE(window_->account()->text().startsWith(QStringLiteral("Signed in as Ann")));
    EXPECT_FALSE(window_->statusLine()->isVisible());

    EXPECT_EQ(titles(*window_, Tab::Layouts),
              (QStringList{ QStringLiteral("Club yard"), QStringLiteral("Fordyce 2026"), QStringLiteral("Shared with me") }));
    const QStringList l1 = lines(window_->row(Tab::Layouts, QStringLiteral("L1")));
    ASSERT_EQ(l1.size(), 2);
    EXPECT_EQ(l1[0], QStringLiteral("by you"));
    EXPECT_TRUE(l1[1].startsWith(QStringLiteral("updated "))) << l1[1].toStdString();
    EXPECT_EQ(lines(window_->row(Tab::Layouts, QStringLiteral("L2")))[0], QStringLiteral("by you · in Train Club"));
    // No credit from the server: whose it is.
    EXPECT_EQ(lines(window_->row(Tab::Layouts, QStringLiteral("L3")))[0], QStringLiteral("Yours"));
    EXPECT_TRUE(lines(window_->row(Tab::Layouts, QStringLiteral("L3")))[1].startsWith(QStringLiteral("View only · updated ")));

    const QStringList v1 = lines(window_->row(Tab::Venues, QStringLiteral("V1")));
    ASSERT_EQ(v1.size(), 2);
    EXPECT_TRUE(v1[1].startsWith(QStringLiteral("1250 × 625 studs (10.0 × 5.0 m) · saved "))) << v1[1].toStdString();
    // A venue with no outline or date (an older server): just whose it is.
    EXPECT_EQ(lines(window_->row(Tab::Venues, QStringLiteral("V2"))), QStringList{ QStringLiteral("by Sam · in Train Club") });

    const QStringList m1 = lines(window_->row(Tab::Modules, QStringLiteral("m1")));
    EXPECT_TRUE(m1[1].startsWith(QStringLiteral("version 3 · updated ")));
    EXPECT_EQ(lines(window_->row(Tab::Parts, QStringLiteral("p1")))[1].left(8), QStringLiteral("CLD-SIG1"));

    // Pictures: the module's and the part's, from the server; the others keep the placeholder.
    auto* modulePic = window_->row(Tab::Modules, QStringLiteral("m1"))->findChild<QLabel*>(QStringLiteral("rowPicture"));
    auto* partPic = window_->row(Tab::Parts, QStringLiteral("p1"))->findChild<QLabel*>(QStringLiteral("rowPicture"));
    ASSERT_TRUE(waitFor([&] { return !modulePic->pixmap().isNull() && !partPic->pixmap().isNull(); }));
    EXPECT_TRUE(window_->row(Tab::Parts, QStringLiteral("p2"))->findChild<QLabel*>(QStringLiteral("rowPicture"))->pixmap().isNull());
    EXPECT_FALSE(window_->row(Tab::Layouts, QStringLiteral("L1"))->findChild<QLabel*>(QStringLiteral("rowPicture"))->text().isEmpty());
    // Everything it asked for was there: no 404s from normal use.
    EXPECT_TRUE(unanswered(http_).isEmpty()) << unanswered(http_).join(QStringLiteral(", ")).toStdString();
    // Each list once, and the catalog not until its tab shows.
    EXPECT_EQ(count(http_, "GET", "/api/layouts"), 1);
    EXPECT_EQ(count(http_, "GET", "/api/venues"), 1);
    EXPECT_EQ(count(http_, "GET", "/api/custom-parts"), 1);
    int catalog = 0;
    for (const auto& r : http_.requests) catalog += r.path.startsWith("/api/catalog/items") || r.path.startsWith("/api/catalog/collections");
    EXPECT_EQ(catalog, 0);
}

TEST_F(ServerWindowTest, ShowNarrowsToMineOrAClubAndIsRememberedForEachServer) {
    open();
    QStringList choices;
    for (int i = 0; i < window_->showFilter()->count(); ++i) choices << window_->showFilter()->itemText(i);
    EXPECT_EQ(choices, (QStringList{ QStringLiteral("All"), QStringLiteral("Mine"), QStringLiteral("Train Club") }));
    pickShow(*window_, QStringLiteral("Mine"));
    EXPECT_EQ(titles(*window_, Tab::Layouts), (QStringList{ QStringLiteral("Fordyce 2026"), QStringLiteral("Shared with me") }));
    EXPECT_EQ(titles(*window_, Tab::Venues), QStringList{ QStringLiteral("Town hall") });
    EXPECT_EQ(titles(*window_, Tab::Modules), (QStringList{ QStringLiteral("Freight yard"), QStringLiteral("Station") }));
    EXPECT_EQ(titles(*window_, Tab::Parts), QStringList{ QStringLiteral("Signal box") });
    pickShow(*window_, QStringLiteral("Train Club"));
    EXPECT_EQ(titles(*window_, Tab::Layouts), QStringList{ QStringLiteral("Club yard") });
    EXPECT_EQ(titles(*window_, Tab::Venues), QStringList{ QStringLiteral("Club hall") });
    EXPECT_EQ(titles(*window_, Tab::Modules), QStringList{ QStringLiteral("Engine shed") });
    EXPECT_EQ(titles(*window_, Tab::Parts), QStringList{ QStringLiteral("Club tree") });

    // Kept for this server: a new window opens on the club…
    EXPECT_EQ(ui::ServerWindow::rememberedShow(http_.base()), QStringLiteral("train"));
    window_ = std::make_unique<ui::ServerWindow>(library_);
    window_->showTab(Tab::Layouts);
    ASSERT_TRUE(waitFor([&] { return window_->row(Tab::Layouts, QStringLiteral("L2")); }));
    EXPECT_EQ(window_->showFilter()->currentText(), QStringLiteral("Train Club"));
    EXPECT_EQ(titles(*window_, Tab::Layouts), QStringList{ QStringLiteral("Club yard") });
    // …and another server keeps its own (All until picked).
    EXPECT_EQ(ui::ServerWindow::rememberedShow(QUrl(QStringLiteral("https://other.example.org"))), QStringLiteral("all"));

    // Search narrows within it.
    pickShow(*window_, QStringLiteral("All"));
    window_->search()->setText(QStringLiteral("ford"));
    EXPECT_EQ(titles(*window_, Tab::Layouts), QStringList{ QStringLiteral("Fordyce 2026") });
    EXPECT_TRUE(titles(*window_, Tab::Venues).isEmpty());
    EXPECT_EQ(window_->emptyLine(Tab::Venues)->text(), QStringLiteral("No venues match."));
}

TEST_F(ServerWindowTest, EmptyListsSayWhoseAndWhatToDoNext) {
    http_.clear("/api/layouts");
    http_.reply("/api/layouts", 200, { { QStringLiteral("layouts"), QJsonArray{} } });
    http_.clear("/api/custom-parts");
    http_.reply("/api/custom-parts", 200, { { QStringLiteral("parts"), QJsonArray{} } });
    signIn();
    window_->showTab(Tab::Layouts);
    ASSERT_TRUE(waitFor([&] { return window_->row(Tab::Venues, QStringLiteral("V1")) && count(http_, "GET", "/api/custom-parts") == 1; }));
    ASSERT_TRUE(waitFor([&] { return !window_->emptyLine(Tab::Parts)->text().startsWith(QStringLiteral("Loading")); }));
    EXPECT_EQ(window_->emptyLine(Tab::Layouts)->text(),
              QStringLiteral("There are no layouts here yet. Send one with Save a file to the server…, or start one on the web."));
    pickShow(*window_, QStringLiteral("Mine"));
    EXPECT_TRUE(window_->emptyLine(Tab::Layouts)->text().startsWith(QStringLiteral("You have no layouts yet.")));
    EXPECT_TRUE(window_->emptyLine(Tab::Parts)->text().startsWith(QStringLiteral("You have no custom parts yet.")));
    pickShow(*window_, QStringLiteral("Train Club"));
    EXPECT_TRUE(window_->emptyLine(Tab::Layouts)->text().startsWith(QStringLiteral("Train Club has no layouts yet.")));
    EXPECT_TRUE(window_->emptyLine(Tab::Venues)->isHidden());
    EXPECT_TRUE(window_->emptyLine(Tab::Modules)->isHidden());
}

TEST_F(ServerWindowTest, NoServerSignedOutAndOfflineAreFriendlyWithTheButtonThatHelps) {
    window_->showTab(Tab::Layouts);
    // No server at all.
    library_.setServer({}, {}, {});
    EXPECT_TRUE(window_->statusLine()->isVisible());
    EXPECT_EQ(window_->statusLine()->button()->text(), QStringLiteral("Add a server…"));
    EXPECT_FALSE(window_->tabs()->isVisible());
    bool addServer = false, signInAsked = false;
    QObject::connect(&library_, &ui::ServerLibrary::addServerRequested, [&] { addServer = true; });
    QObject::connect(&library_, &ui::ServerLibrary::signInRequested, [&] { signInAsked = true; });
    window_->statusLine()->button()->click();
    EXPECT_TRUE(addServer);

    // Signed out.
    library_.setServer(http_.base(), QStringLiteral("Club server"), QString());
    EXPECT_EQ(window_->statusLine()->text()->text(),
              QStringLiteral("Sign in to Club server to see your layouts, venues, modules and parts, your clubs' and the catalog."));
    EXPECT_EQ(window_->statusLine()->button()->text(), QStringLiteral("Sign in"));
    window_->statusLine()->button()->click();
    EXPECT_TRUE(signInAsked);
    EXPECT_FALSE(window_->account()->isVisible());

    // Offline, then back.
    QTcpServer gone;
    gone.listen(QHostAddress::LocalHost, 0);
    const QUrl away(QStringLiteral("http://127.0.0.1:%1").arg(gone.serverPort()));
    gone.close();
    library_.setServer(away, QStringLiteral("Away"), QStringLiteral("bld_pat_w"));
    ASSERT_TRUE(waitFor([&] { return library_.state() == ui::ServerLibrary::State::Offline; }));
    EXPECT_EQ(window_->statusLine()->button()->text(), QStringLiteral("Try again"));
    EXPECT_FALSE(window_->tabs()->isVisible());
    // Signed in again: the lists come.
    signIn();
    ASSERT_TRUE(waitFor([&] { return window_->row(Tab::Layouts, QStringLiteral("L1")); }));
    EXPECT_TRUE(window_->tabs()->isVisible());
    EXPECT_FALSE(window_->statusLine()->isVisible());
}

TEST_F(ServerWindowTest, ALayoutOpensLiveDownloadsAsACopyAndItsOwnerCanDeleteIt) {
    open();
    std::optional<sync::LayoutEntry> opened;
    QObject::connect(window_.get(), &ui::ServerWindow::openLayoutRequested, [&](const sync::LayoutEntry& l) { opened = l; });
    window_->row(Tab::Layouts, QStringLiteral("L3"))->findChild<QPushButton*>(QStringLiteral("openLayout"))->click();
    ASSERT_TRUE(opened);
    EXPECT_EQ(opened->id, QStringLiteral("L3"));
    EXPECT_EQ(opened->role, QStringLiteral("viewer"));

    // Download a copy: the whole layout as a .bld-layout from a server that has it.
    QTemporaryDir dir;
    bool native = false;
    QString path = QDir(dir.path()).filePath(QStringLiteral("Fordyce 2026.bld-layout"));
    window_->askSavePath = [&](const QString& title, bool n) {
        EXPECT_EQ(title, QStringLiteral("Fordyce 2026"));
        native = n;
        return path;
    };
    http_.replyRaw("/api/layouts/L1/export.bld-layout", 200, QByteArray("PK\x03\x04zip", 7), "application/zip");
    QAction* download = moreAction(window_->row(Tab::Layouts, QStringLiteral("L1")), "downloadLayout");
    ASSERT_TRUE(download);
    download->trigger();
    ASSERT_TRUE(waitFor([&] { return QFile::exists(path); }));
    EXPECT_TRUE(native);
    QFile f(path);
    ASSERT_TRUE(waitFor([&] { return f.open(QIODevice::ReadOnly) && f.size() == 7; }));
    EXPECT_EQ(f.readAll(), QByteArray("PK\x03\x04zip", 7));
    // Picked .bbm in the save box: the BlueBrick map instead.
    path = QDir(dir.path()).filePath(QStringLiteral("Fordyce 2026.bbm"));
    http_.replyRaw("/api/layouts/L1/export.bbm", 200, QByteArray("<map/>"), "application/xml");
    moreAction(window_->row(Tab::Layouts, QStringLiteral("L1")), "downloadLayout")->trigger();
    ASSERT_TRUE(waitFor([&] { return QFile::exists(path); }));

    // Only its owner may delete it: asked first, then gone from the list.
    EXPECT_FALSE(moreAction(window_->row(Tab::Layouts, QStringLiteral("L2")), "deleteLayout"));
    EXPECT_FALSE(moreAction(window_->row(Tab::Layouts, QStringLiteral("L3")), "deleteLayout"));
    answer_ = false;
    moreAction(window_->row(Tab::Layouts, QStringLiteral("L1")), "deleteLayout")->trigger();
    ASSERT_TRUE(waitFor([&] { return asked_.size() == 1; }));
    EXPECT_EQ(asked_.back(), QStringLiteral("Delete “Fordyce 2026”?"));
    EXPECT_EQ(count(http_, "DELETE", "/api/layouts/L1"), 0);
    answer_ = true;
    http_.reply("/api/layouts/L1", 200, { { QStringLiteral("ok"), true } });
    moreAction(window_->row(Tab::Layouts, QStringLiteral("L1")), "deleteLayout")->trigger();
    ASSERT_TRUE(waitFor([&] { return count(http_, "DELETE", "/api/layouts/L1") == 1; }));
    ASSERT_TRUE(waitFor([&] { return !window_->row(Tab::Layouts, QStringLiteral("L1")); }));
}

class OlderServerWindow : public ServerWindowTest {
protected:
    QStringList features() override { return { QStringLiteral("liveSync") }; }
};

TEST_F(OlderServerWindow, WithoutLayoutDownloadsACopyIsABbmAndWithoutVenuesNothingIsAsked) {
    signIn();
    window_->showTab(Tab::Layouts);
    ASSERT_TRUE(waitFor([&] { return window_->row(Tab::Layouts, QStringLiteral("L1")); }));
    std::optional<bool> native;
    window_->askSavePath = [&](const QString&, bool n) {
        native = n;
        return QString();
    };
    moreAction(window_->row(Tab::Layouts, QStringLiteral("L1")), "downloadLayout")->trigger();
    ASSERT_TRUE(waitFor([&] { return native.has_value(); }));
    EXPECT_FALSE(*native);
    // No venue library there: it says so instead of asking.
    EXPECT_EQ(count(http_, "GET", "/api/venues"), 0);
    EXPECT_EQ(window_->emptyLine(Tab::Venues)->text(), QStringLiteral("This server doesn't have a venue library yet."));
}

TEST_F(ServerWindowTest, AVenueStartsALayoutGoesToYourVenueLibraryAndItsManagersCanDeleteIt) {
    open(Tab::Venues);
    QString used, added;
    QObject::connect(window_.get(), &ui::ServerWindow::useVenueRequested, [&](const QString& id, const QString&) { used = id; });
    QObject::connect(window_.get(), &ui::ServerWindow::addVenueRequested, [&](const QString& id, const QString&) { added = id; });
    window_->row(Tab::Venues, QStringLiteral("V2"))->findChild<QPushButton*>(QStringLiteral("useVenue"))->click();
    EXPECT_EQ(used, QStringLiteral("V2"));
    moreAction(window_->row(Tab::Venues, QStringLiteral("V1")), "addVenue")->trigger();
    ASSERT_TRUE(waitFor([&] { return added == QStringLiteral("V1"); }));
    EXPECT_FALSE(moreAction(window_->row(Tab::Venues, QStringLiteral("V2")), "deleteVenue"));
    http_.reply("/api/venues/V1", 200, { { QStringLiteral("ok"), true } });
    moreAction(window_->row(Tab::Venues, QStringLiteral("V1")), "deleteVenue")->trigger();
    ASSERT_TRUE(waitFor([&] { return count(http_, "DELETE", "/api/venues/V1") == 1; }));
    EXPECT_EQ(asked_.back(), QStringLiteral("Delete “Town hall”?"));
    ASSERT_TRUE(waitFor([&] { return !window_->row(Tab::Venues, QStringLiteral("V1")); }));
}

TEST_F(ServerWindowTest, AModuleGoesIntoTheLayoutOpensToChangeSavesACopyAndItsOwnerCanDeleteIt) {
    open(Tab::Modules);
    QString inserted, opened, saved;
    QObject::connect(&library_, &ui::ServerLibrary::insertRequested, [&](const QString& id) { inserted = id; });
    QObject::connect(&library_, &ui::ServerLibrary::openRequested, [&](const QString& id) { opened = id; });
    QObject::connect(window_.get(), &ui::ServerWindow::saveModuleCopyRequested, [&](const QString& id) { saved = id; });
    QWidget* m1 = window_->row(Tab::Modules, QStringLiteral("m1"));
    m1->findChild<QPushButton*>(QStringLiteral("insertModule"))->click();
    EXPECT_EQ(inserted, QStringLiteral("m1"));
    moreAction(m1, "openModule")->trigger();
    moreAction(m1, "saveModuleCopy")->trigger();
    ASSERT_TRUE(waitFor([&] { return opened == QStringLiteral("m1") && saved == QStringLiteral("m1"); }));
    // A viewer's: look, don't change; and only the owner deletes.
    QWidget* m2 = window_->row(Tab::Modules, QStringLiteral("m2"));
    EXPECT_FALSE(moreAction(m2, "openModule")->isEnabled());
    EXPECT_FALSE(moreAction(m2, "deleteModule"));
    EXPECT_FALSE(moreAction(window_->row(Tab::Modules, QStringLiteral("c1")), "deleteModule"));
    // Adding one: the buttons wait.
    library_.setInserting(QStringLiteral("m1"));
    ASSERT_TRUE(waitFor([&] {
        QWidget* r = window_->row(Tab::Modules, QStringLiteral("m1"));
        return r && r->findChild<QPushButton*>(QStringLiteral("insertModule"))->text() == QStringLiteral("Adding…");
    }));
    EXPECT_FALSE(window_->row(Tab::Modules, QStringLiteral("c1"))->findChild<QPushButton*>(QStringLiteral("insertModule"))->isEnabled());
    library_.setInserting({});
    // The one being changed in this window says so.
    library_.setEditingModule(QStringLiteral("m1"));
    ASSERT_TRUE(waitFor([&] {
        QWidget* r = window_->row(Tab::Modules, QStringLiteral("m1"));
        return r && r->findChild<QLabel*>(QStringLiteral("rowPill"));
    }));
    library_.setEditingModule({});
    http_.reply("/api/modules/m1", 200, { { QStringLiteral("ok"), true } });
    ASSERT_TRUE(waitFor([&] { return window_->row(Tab::Modules, QStringLiteral("m1")) && moreAction(window_->row(Tab::Modules, QStringLiteral("m1")), "deleteModule"); }));
    const int lists = count(http_, "GET", "/api/modules");
    moreAction(window_->row(Tab::Modules, QStringLiteral("m1")), "deleteModule")->trigger();
    ASSERT_TRUE(waitFor([&] { return count(http_, "DELETE", "/api/modules/m1") == 1; }));
    EXPECT_EQ(asked_.back(), QStringLiteral("Delete “Freight yard”?"));
    // The library asks again.
    ASSERT_TRUE(waitFor([&] { return count(http_, "GET", "/api/modules") > lists; }));
}

TEST_F(ServerWindowTest, APartIsAddedToYourPartsOrShownWhenYouHaveIt) {
    window_->hasPart = [](const QString& n) { return n == QStringLiteral("CLD-TREE"); };
    open(Tab::Parts);
    QString add, show;
    QObject::connect(window_.get(), &ui::ServerWindow::addPartRequested, [&](const QString& n) { add = n; });
    QObject::connect(window_.get(), &ui::ServerWindow::showPartRequested, [&](const QString& n) { show = n; });
    auto* addBtn = window_->row(Tab::Parts, QStringLiteral("p1"))->findChild<QPushButton*>(QStringLiteral("addPart"));
    ASSERT_TRUE(addBtn);
    EXPECT_EQ(addBtn->text(), QStringLiteral("Add to my parts"));
    addBtn->click();
    EXPECT_EQ(add, QStringLiteral("CLD-SIG1"));
    auto* showBtn = window_->row(Tab::Parts, QStringLiteral("p2"))->findChild<QPushButton*>(QStringLiteral("showPart"));
    ASSERT_TRUE(showBtn);
    showBtn->click();
    EXPECT_EQ(show, QStringLiteral("CLD-TREE"));
    // Once it's in the library, the row says so.
    window_->hasPart = [](const QString&) { return true; };
    window_->partsChanged();
    EXPECT_TRUE(window_->row(Tab::Parts, QStringLiteral("p1"))->findChild<QPushButton*>(QStringLiteral("showPart")));
}

TEST_F(ServerWindowTest, AClubsLayoutGoesBackToItsAuthorAfterAsking) {
    open();
    QWidget* l2 = window_->row(Tab::Layouts, QStringLiteral("L2"));
    EXPECT_FALSE(moreAction(l2, "giveBack"));
    EXPECT_FALSE(moreAction(window_->row(Tab::Layouts, QStringLiteral("L1")), "takeBack"));
    http_.reply("/api/layouts/L2/take-back", 200, { { QStringLiteral("keptCopyId"), QStringLiteral("L9") } });
    moreAction(l2, "takeBack")->trigger();
    ASSERT_TRUE(waitFor([&] { return count(http_, "POST", "/api/layouts/L2/take-back") == 1; }));
    EXPECT_EQ(asked_.back(), QStringLiteral("Take “Club yard” back?"));
    // The list is asked for again.
    ASSERT_TRUE(waitFor([&] { return count(http_, "GET", "/api/layouts") == 2; }));
}

TEST_F(ServerWindowTest, ALiveHintAsksForThatListAgainAndAHiddenWindowWaitsUntilItShows) {
    open();
    http_.clear("/api/layouts");
    QJsonObject more = layoutsJson();
    QJsonArray list = more.value(QStringLiteral("layouts")).toArray();
    list.append(QJsonObject{ { QStringLiteral("id"), QStringLiteral("L4") }, { QStringLiteral("title"), QStringLiteral("Made on the web") },
                             { QStringLiteral("role"), QStringLiteral("owner") }, { QStringLiteral("updatedAt"), kWhen + 5000 } });
    more.insert(QStringLiteral("layouts"), list);
    http_.reply("/api/layouts", 200, more);
    // A burst of hints, one request.
    for (int i = 0; i < 3; ++i)
        window_->hint(QJsonObject{ { QStringLiteral("kind"), QStringLiteral("layout") }, { QStringLiteral("action"), QStringLiteral("created") } });
    ASSERT_TRUE(waitFor([&] { return window_->row(Tab::Layouts, QStringLiteral("L4")); }));
    EXPECT_EQ(count(http_, "GET", "/api/layouts"), 2);
    EXPECT_EQ(count(http_, "GET", "/api/venues"), 1);
    EXPECT_EQ(count(http_, "GET", "/api/custom-parts"), 1);
    // Hints it doesn't list change nothing.
    window_->hint(QJsonObject{ { QStringLiteral("kind"), QStringLiteral("warning") } });
    // Closed: the next hint waits for the window to open again.
    window_->close();
    window_->hint(QJsonObject{ { QStringLiteral("kind"), QStringLiteral("venue") } });
    waitFor([&] { return count(http_, "GET", "/api/venues") > 1; }, 600);
    EXPECT_EQ(count(http_, "GET", "/api/venues"), 1);
    window_->showTab(Tab::Venues);
    ASSERT_TRUE(waitFor([&] { return count(http_, "GET", "/api/venues") == 2; }));
}

TEST_F(ServerWindowTest, TheCatalogAndCollectionsTabsAskOnlyWhenShown) {
    http_.reply("/api/catalog/items?kind=module&sort=popular", 200, { { QStringLiteral("items"), QJsonArray{} } });
    http_.reply("/api/catalog/collections", 200, { { QStringLiteral("collections"), QJsonArray{} } });
    open();
    EXPECT_EQ(count(http_, "GET", "/api/catalog/items?kind=module&sort=popular"), 0);
    window_->showTab(Tab::Catalog);
    ASSERT_TRUE(waitFor([&] { return count(http_, "GET", "/api/catalog/items?kind=module&sort=popular") == 1; }));
    // Its kinds: modules, parts, layouts and venues; collections have their own tab.
    QStringList kinds;
    for (QPushButton* b : window_->catalogTab()->findChildren<QPushButton*>())
        if (b->property("catalogKind").isValid() && b->isVisibleTo(window_->catalogTab())) kinds << b->text();
    EXPECT_EQ(kinds, (QStringList{ QStringLiteral("Modules"), QStringLiteral("Parts"), QStringLiteral("Layouts"), QStringLiteral("Venues") }));
    EXPECT_EQ(count(http_, "GET", "/api/catalog/collections"), 0);
    window_->showTab(Tab::Collections);
    ASSERT_TRUE(waitFor([&] { return count(http_, "GET", "/api/catalog/collections") == 1; }));
    EXPECT_FALSE(window_->collectionsTab()->findChild<QFrame*>(QStringLiteral("Segmented"))->isVisible());
    EXPECT_TRUE(unanswered(http_).isEmpty()) << unanswered(http_).join(QStringLiteral(", ")).toStdString();
}

TEST_F(ServerWindowTest, PickingAnotherServerAsksForIt) {
    {
        sync::ServerList list = sync::ServerList::load();
        list.add(QUrl(QStringLiteral("https://other.example.org")), QStringLiteral("Other"));
        list.save();
    }
    open();
    window_->refreshServers();
    ASSERT_EQ(window_->serverPicker()->count(), 2);
    QUrl picked;
    QObject::connect(window_.get(), &ui::ServerWindow::serverPicked, [&](const QUrl& u) { picked = u; });
    window_->serverPicker()->setCurrentIndex(1);
    emit window_->serverPicker()->activated(1);
    EXPECT_EQ(picked, QUrl(QStringLiteral("https://other.example.org")));
}

// ---------- Sending ---------------------------------------------------------

TEST_F(ServerWindowTest, WhatsOnlyOnThisComputerSaysSoAndSendsOnRequest) {
    open();
    using Item = ui::ServerWindow::LocalItem;
    window_->setLocalItems(Tab::Layouts, { Item{ QStringLiteral("open"), QStringLiteral("My new layout"), QStringLiteral("Open now") },
                                           // Already on the server under its name: not offered.
                                           Item{ QStringLiteral("/tmp/f.bld-layout"), QStringLiteral("fordyce 2026"), QStringLiteral("Recent file") } });
    EXPECT_EQ(window_->localShown(Tab::Layouts), QStringList{ QStringLiteral("My new layout") });
    QFrame* local = window_->findChild<QFrame*>(QStringLiteral("localRow:open"));
    ASSERT_TRUE(local);
    EXPECT_EQ(lines(local), (QStringList{ QStringLiteral("Open now"), QStringLiteral("Not on the server yet") }));
    std::optional<std::pair<Tab, QString>> sent;
    QObject::connect(window_.get(), &ui::ServerWindow::sendRequested, [&](Tab t, const QString& id) { sent = std::make_pair(t, id); });
    local->findChild<QPushButton*>(QStringLiteral("sendLocal"))->click();
    ASSERT_TRUE(sent);
    EXPECT_EQ(sent->first, Tab::Layouts);
    EXPECT_EQ(sent->second, QStringLiteral("open"));
    // Yours only: a club's view leaves them out; Mine shows them.
    pickShow(*window_, QStringLiteral("Train Club"));
    EXPECT_TRUE(window_->localShown(Tab::Layouts).isEmpty());
    pickShow(*window_, QStringLiteral("Mine"));
    EXPECT_EQ(window_->localShown(Tab::Layouts), QStringList{ QStringLiteral("My new layout") });
    // A venue, a module and a part too.
    window_->setLocalItems(Tab::Venues, { Item{ QStringLiteral("/v/Gym.bld-venue"), QStringLiteral("Gym"), QStringLiteral("Venue library") },
                                          Item{ QStringLiteral("/v/Town hall.bld-venue"), QStringLiteral("Town hall"), QStringLiteral("Venue library") } });
    EXPECT_EQ(window_->localShown(Tab::Venues), QStringList{ QStringLiteral("Gym") });
    window_->setLocalItems(Tab::Parts, { Item{ QStringLiteral("MY.1"), QStringLiteral("My bridge"), QStringLiteral("Your part MY.1") } });
    EXPECT_EQ(window_->localShown(Tab::Parts), QStringList{ QStringLiteral("My bridge") });

    // The tabs' own buttons: a layout file, and every part the server lacks.
    bool file = false, parts = false;
    QObject::connect(window_.get(), &ui::ServerWindow::saveLayoutFileRequested, [&] { file = true; });
    QObject::connect(window_.get(), &ui::ServerWindow::uploadPartsRequested, [&] { parts = true; });
    window_->findChild<QPushButton*>(QStringLiteral("saveLayoutFile"))->click();
    window_->findChild<QPushButton*>(QStringLiteral("uploadParts"))->click();
    ASSERT_TRUE(waitFor([&] { return file && parts; }));
}

class SharingServerWindow : public ServerWindowTest {
protected:
    QStringList features() override { return { QStringLiteral("venues"), QStringLiteral("layoutDownload"), QStringLiteral("catalogShare") }; }
};

TEST_F(SharingServerWindow, WhatYouOwnCanBeSharedToTheCatalogAndSaysWhereItStands) {
    http_.clear("/api/catalog/settings");
    http_.reply("/api/catalog/settings", 200,
                { { QStringLiteral("modules"), true }, { QStringLiteral("parts"), true }, { QStringLiteral("layouts"), true },
                  { QStringLiteral("venues"), false }, { QStringLiteral("review"), QStringLiteral("moderator") } });
    http_.reply("/api/catalog/mine", 200,
                { { QStringLiteral("items"),
                    QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("CI1") }, { QStringLiteral("kind"), QStringLiteral("module") },
                                             { QStringLiteral("sourceId"), QStringLiteral("m1") }, { QStringLiteral("status"), QStringLiteral("public") },
                                             { QStringLiteral("pendingVersion"), 2 } } } } });
    open(Tab::Modules);
    ASSERT_TRUE(waitFor([&] { return count(http_, "GET", "/api/catalog/mine") == 1; }));
    ASSERT_TRUE(waitFor([&] {
        QWidget* r = window_->row(Tab::Modules, QStringLiteral("m1"));
        return r && lines(r).contains(QStringLiteral("In the catalog: Public · update in review"));
    }));
    QAction* update = moreAction(window_->row(Tab::Modules, QStringLiteral("m1")), "shareToCatalog");
    ASSERT_TRUE(update);
    EXPECT_EQ(update->text(), QStringLiteral("Publish this update…"));
    // Only what you may share: not a club module you only edit, nor one you only see.
    EXPECT_FALSE(moreAction(window_->row(Tab::Modules, QStringLiteral("c1")), "shareToCatalog"));
    EXPECT_FALSE(moreAction(window_->row(Tab::Modules, QStringLiteral("m2")), "shareToCatalog"));
    // Venues aren't in this server's catalog.
    EXPECT_FALSE(moreAction(window_->row(Tab::Venues, QStringLiteral("V1")), "shareToCatalog"));
    std::optional<sync::CatalogShare> asked;
    bool wasUpdate = true;
    QObject::connect(window_.get(), &ui::ServerWindow::shareRequested, [&](const sync::CatalogShare& sh, bool u, const QString&) {
        asked = sh;
        wasUpdate = u;
    });
    QAction* share = moreAction(window_->row(Tab::Layouts, QStringLiteral("L1")), "shareToCatalog");
    ASSERT_TRUE(share);
    EXPECT_EQ(share->text(), QStringLiteral("Share to the public catalog…"));
    share->trigger();
    ASSERT_TRUE(waitFor([&] { return asked.has_value(); }));
    EXPECT_EQ(asked->kind, QStringLiteral("layout"));
    EXPECT_EQ(asked->sourceId, QStringLiteral("L1"));
    EXPECT_EQ(asked->title, QStringLiteral("Fordyce 2026"));
    EXPECT_FALSE(wasUpdate);
    // A hint that the catalog changed asks again.
    window_->hint(QJsonObject{ { QStringLiteral("kind"), QStringLiteral("catalog") } });
    ASSERT_TRUE(waitFor([&] { return count(http_, "GET", "/api/catalog/mine") == 2; }));
}

TEST_F(ServerWindowTest, AServerThatCantTakeSharesOffersNone) {
    open();
    EXPECT_FALSE(moreAction(window_->row(Tab::Layouts, QStringLiteral("L1")), "shareToCatalog"));
    EXPECT_EQ(count(http_, "GET", "/api/catalog/mine"), 0);
}

TEST(SaveToServerDialog, OffersMeAndTheClubsThatTakeThingsAndAsksBeforeAClub) {
    sync::OrgEntry train{ QStringLiteral("train"), QStringLiteral("Train Club"), QStringLiteral("member") };
    sync::OrgEntry closed{ QStringLiteral("closed"), QStringLiteral("Closed Club"), QStringLiteral("member") };
    closed.canAdd = false;
    ui::SaveToServerDialog d(QStringLiteral("venue"), QStringLiteral("Gym"), QStringLiteral("Club server"), { train, closed },
                             QStringLiteral("train"));
    QStringList owners;
    for (int i = 0; i < d.owner()->count(); ++i) owners << d.owner()->itemText(i);
    EXPECT_EQ(owners, (QStringList{ QStringLiteral("Me"), QStringLiteral("Train Club") }));
    // It starts at the club the window shows.
    EXPECT_EQ(d.orgSlug(), QStringLiteral("train"));
    QStringList asked;
    bool yes = false;
    d.confirmClub = [&](const QString& club) {
        asked << club;
        return yes;
    };
    d.nameEdit()->clear();
    d.save();
    EXPECT_EQ(d.error()->text(), QStringLiteral("Give it a name."));
    EXPECT_NE(d.result(), QDialog::Accepted);
    d.nameEdit()->setText(QStringLiteral(" Gym 2 "));
    d.save();
    EXPECT_EQ(asked, QStringList{ QStringLiteral("Train Club") });
    EXPECT_NE(d.result(), QDialog::Accepted);
    yes = true;
    d.save();
    EXPECT_EQ(d.result(), QDialog::Accepted);
    EXPECT_EQ(d.name(), QStringLiteral("Gym 2"));
    // Me: no question.
    d.owner()->setCurrentIndex(0);
    d.save();
    EXPECT_EQ(asked.size(), 2);
    EXPECT_TRUE(d.orgSlug().isEmpty());
}

TEST(ShareToCatalogDialog, SendsWhatTheWebSendsWithTheLayoutsPictureAndSaysWhatHappened) {
    FakeHttp http;
    sync::LibraryApi api;
    api.setBase(http.base());
    api.setToken(QStringLiteral("bld_pat_s"));
    http.reply("/api/catalog/submissions", 201, { { QStringLiteral("id"), QStringLiteral("CI9") }, { QStringLiteral("status"), QStringLiteral("in_review") } });
    sync::CatalogShare base;
    base.kind = QStringLiteral("layout");
    base.sourceId = QStringLiteral("L1");
    base.title = QStringLiteral("Fordyce 2026");
    ui::ShareToCatalogDialog d(api, base, false, true, QString());
    EXPECT_TRUE(d.findChild<QLabel*>(QStringLiteral("shareAbout"))->text().contains(QStringLiteral("A moderator reviews it first.")));
    EXPECT_FALSE(d.note()->isVisibleTo(&d));
    bool drew = false;
    d.makeThumbnail = [&](std::function<void(const QByteArray&)> done) {
        drew = true;
        done(png());
    };
    QUrl opened;
    d.openUrl = [&](const QUrl& u) { opened = u; };
    // Not until the box is ticked.
    d.share();
    EXPECT_EQ(d.error()->text(), QStringLiteral("Tick the box to agree that anyone can copy it."));
    EXPECT_FALSE(drew);
    d.consent()->setChecked(true);
    d.description()->setPlainText(QStringLiteral("Our 2026 show layout"));
    d.tags()->setText(QStringLiteral("show, 9V, "));
    d.share();
    ASSERT_TRUE(waitFor([&] { return d.itemId() == QStringLiteral("CI9"); }));
    EXPECT_EQ(d.result()->text(), QStringLiteral("Sent for review. A moderator looks at it before it appears in the catalog."));
    const synctest::Request& r = http.requests.back();
    EXPECT_EQ(r.method, QByteArray("POST"));
    EXPECT_EQ(r.authorization, QByteArray("Bearer bld_pat_s"));
    const QJsonObject body = QJsonDocument::fromJson(r.body).object();
    EXPECT_EQ(body.value(QStringLiteral("kind")).toString(), QStringLiteral("layout"));
    EXPECT_EQ(body.value(QStringLiteral("sourceId")).toString(), QStringLiteral("L1"));
    EXPECT_EQ(body.value(QStringLiteral("title")).toString(), QStringLiteral("Fordyce 2026"));
    EXPECT_EQ(body.value(QStringLiteral("description")).toString(), QStringLiteral("Our 2026 show layout"));
    EXPECT_EQ(body.value(QStringLiteral("tags")).toArray(), (QJsonArray{ QStringLiteral("show"), QStringLiteral("9V") }));
    EXPECT_FALSE(body.contains(QStringLiteral("note")));
    const QJsonObject thumb = body.value(QStringLiteral("thumbnail")).toObject();
    EXPECT_EQ(thumb.value(QStringLiteral("mime")).toString(), QStringLiteral("image/png"));
    EXPECT_EQ(QByteArray::fromBase64(thumb.value(QStringLiteral("data")).toString().toLatin1()), png());
    // A cover of your own is chosen on the web.
    d.findChild<QPushButton*>(QStringLiteral("shareCover"))->click();
    EXPECT_EQ(opened.path(), QStringLiteral("/catalog/items/CI9"));
}

TEST(ShareToCatalogDialog, AnUpdateKeepsItsDescriptionAndTagsAndSaysWhatChanged) {
    FakeHttp http;
    sync::LibraryApi api;
    api.setBase(http.base());
    http.reply("/api/catalog/submissions", 200, { { QStringLiteral("id"), QStringLiteral("CI1") }, { QStringLiteral("status"), QStringLiteral("public") } });
    sync::CatalogShare base;
    base.kind = QStringLiteral("module");
    base.sourceId = QStringLiteral("m1");
    base.title = QStringLiteral("Freight yard");
    ui::ShareToCatalogDialog d(api, base, true, false, QString());
    EXPECT_EQ(d.windowTitle(), QStringLiteral("Publish this update"));
    EXPECT_FALSE(d.findChild<QLabel*>(QStringLiteral("shareAbout"))->text().contains(QStringLiteral("moderator")));
    d.consent()->setChecked(true);
    d.note()->setText(QStringLiteral("Longer sidings"));
    d.share();
    ASSERT_TRUE(waitFor([&] { return !d.status().isEmpty(); }));
    EXPECT_EQ(d.result()->text(), QStringLiteral("Shared: it’s in the public catalog now."));
    const QJsonObject body = QJsonDocument::fromJson(http.requests.back().body).object();
    EXPECT_FALSE(body.contains(QStringLiteral("description")));
    EXPECT_FALSE(body.contains(QStringLiteral("tags")));
    EXPECT_FALSE(body.contains(QStringLiteral("thumbnail")));
    EXPECT_EQ(body.value(QStringLiteral("note")).toString(), QStringLiteral("Longer sidings"));
}

// ---------- The main window --------------------------------------------------

namespace {

class Win : public ui::MainWindow {
public:
    using ui::MainWindow::MainWindow;
    using ui::MainWindow::setTokenStore;
};

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

std::unique_ptr<core::Map> sampleModule() {
    auto map = std::make_unique<core::Map>();
    auto l = std::make_unique<core::LayerBrick>();
    l->guid = core::newBbmId();
    l->name = QStringLiteral("Tracks");
    for (int i = 0; i < 2; ++i) {
        core::Brick b;
        b.guid = core::newBbmId();
        b.partNumber = QStringLiteral("3857.0");
        b.displayArea = QRectF(i * 32, 0, 32, 32);
        l->bricks.push_back(b);
    }
    map->layers().push_back(std::move(l));
    map->nbItems = sync::partCount(*map);
    return map;
}

}  // namespace

class ServerWindowMain : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ui::UpdateCheck::setCheckAtStartupEnabled(false);
        QSettings().remove(QStringLiteral("sync/serverAddress"));
        {
            sync::ServerList list;
            list.add(http_.base(), QStringLiteral("Club server"));
            sync::ServerInfo info;
            info.features = QStringList{ QStringLiteral("liveSync"), QStringLiteral("venues") };
            list.remember(http_.base(), info);
            list.touch(http_.base());
            list.save();
        }
        tokens_->save(http_.base(), QStringLiteral("bld_pat_win"));
        http_.reply("/api/orgs", 200, orgsJson());
        http_.reply("/api/catalog/settings", 200, { { QStringLiteral("modules"), false } });
        http_.reply("/api/notices", 200, { { QStringLiteral("notices"), QJsonArray{} } });
        http_.reply("/api/events", 401, { { QStringLiteral("error"), QStringLiteral("invalid_token") } });
        http_.reply("/api/modules", 200, modulesJson());
        http_.reply("/api/layouts", 200, layoutsJson());
        http_.reply("/api/venues", 200, venuesJson());
        http_.reply("/api/custom-parts", 200, partsJson());
        http_.replyRaw("/api/modules/m1/thumbnail?v=77&size=small", 200, png(), "image/png");
        http_.replyRaw("/api/custom-parts/p1/sprite?v=1790000000000", 200, png(), "image/png");
        window_ = std::make_unique<Win>(bundledParts());
        window_->setTokenStore(tokens_);
        window_->ensureDocument();
        library_ = window_->serverLibrary();
        ASSERT_TRUE(waitFor([&] { return library_->state() == ui::ServerLibrary::State::Ready; }));
    }
    void TearDown() override {
        if (auto* view = window_->findChild<ui::MapView*>()) view->undoStack()->setClean();
        window_.reset();
        QSettings().setValue(QLatin1String(sync::ServerList::kKey), QByteArray("[]"));
        QStandardPaths::setTestModeEnabled(false);
    }
    ui::ServerWindow* openWindow() {
        window_->showServerWindow();
        ui::ServerWindow* w = window_->serverWindow();
        EXPECT_TRUE(w);
        EXPECT_TRUE(waitFor([&] { return w->row(Tab::Layouts, QStringLiteral("L1")); }));
        return w;
    }

    FakeHttp http_;
    std::shared_ptr<sync::MemoryTokenStore> tokens_ = std::make_shared<sync::MemoryTokenStore>();
    std::unique_ptr<Win> window_;
    ui::ServerLibrary* library_ = nullptr;
};

TEST_F(ServerWindowMain, FileOpenFromServerReplacesConnectAndDownloadVenuesAndTheToolbarOpensItToo) {
    QAction* act = window_->findChild<QAction*>(QStringLiteral("file.openFromServer"));
    ASSERT_TRUE(act);
    EXPECT_EQ(act->text(), QStringLiteral("Open from &Server..."));
    EXPECT_EQ(act->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    QStringList file;
    for (QAction* a : window_->menuBar()->actions().first()->menu()->actions()) file << a->text();
    EXPECT_FALSE(file.contains(QStringLiteral("&Connect to Server...")));
    EXPECT_FALSE(file.contains(QStringLiteral("Download &Venues from Server...")));
    // Nothing else claims the shortcut.
    int claims = 0;
    for (QAction* a : window_->findChildren<QAction*>()) claims += a->shortcut() == act->shortcut();
    EXPECT_EQ(claims, 1);

    EXPECT_FALSE(window_->serverWindow());
    act->trigger();
    ASSERT_TRUE(window_->serverWindow());
    EXPECT_TRUE(window_->serverWindow()->isVisible());
    window_->serverWindow()->close();
    QAction* tool = window_->findChild<QAction*>(QStringLiteral("tool.server"));
    ASSERT_TRUE(tool);
    tool->trigger();
    EXPECT_TRUE(window_->serverWindow()->isVisible());
    // The same window each time.
    EXPECT_EQ(window_->findChildren<ui::ServerWindow*>().size(), 1);
}

TEST_F(ServerWindowMain, OpenLiveChecksTheServerThenOpensTheLayout) {
    http_.reply("/api/version", 200,
                { { QStringLiteral("version"), QStringLiteral("1.0.0") }, { QStringLiteral("schemaVersion"), 1 },
                  { QStringLiteral("protocols"), QJsonArray{ QStringLiteral("y-websocket/1") } } });
    ui::ServerWindow* w = openWindow();
    w->row(Tab::Layouts, QStringLiteral("L1"))->findChild<QPushButton*>(QStringLiteral("openLayout"))->click();
    // Opened live: first among the server's recent layouts, as Connect did.
    ASSERT_TRUE(waitFor([&] {
        const sync::ServerList list = sync::ServerList::load();
        const sync::ServerEntry* e = list.find(http_.base());
        return e && !e->recent.isEmpty() && e->recent.first().id == QStringLiteral("L1");
    }));
    EXPECT_EQ(count(http_, "GET", "/api/version"), 1);
    for (const auto& r : http_.requests) {
        if (r.path == "/api/version") {
            EXPECT_EQ(r.authorization, QByteArray("Bearer bld_pat_win"));
        }
    }
}

TEST_F(ServerWindowMain, AVenueGoesToTheVenueLibraryAndAModuleCopyToTheModuleLibrary) {
    const QJsonObject venue{ { QStringLiteral("name"), QStringLiteral("Town hall") }, { QStringLiteral("enabled"), true },
                             { QStringLiteral("minWalkwayStuds"), 30 },
                             { QStringLiteral("bounds"), QJsonObject{ { QStringLiteral("x"), 0 }, { QStringLiteral("y"), 0 },
                                                                      { QStringLiteral("w"), 400 }, { QStringLiteral("h"), 300 } } },
                             { QStringLiteral("edges"), QJsonArray{} }, { QStringLiteral("obstacles"), QJsonArray{} } };
    http_.reply("/api/venues/V1", 200, { { QStringLiteral("id"), QStringLiteral("V1") }, { QStringLiteral("name"), QStringLiteral("Town hall") },
                                         { QStringLiteral("data"), venue } });
    auto* venues = window_->findChild<ui::VenueLibraryPanel*>();
    const QDir vdir(venues->libraryPath());
    const QStringList before = vdir.entryList({ QStringLiteral("*.bld-venue") }, QDir::Files);
    ui::ServerWindow* w = openWindow();
    moreAction(w->row(Tab::Venues, QStringLiteral("V1")), "addVenue")->trigger();
    QStringList added;
    ASSERT_TRUE(waitFor([&] {
        added = vdir.entryList({ QStringLiteral("*.bld-venue") }, QDir::Files);
        for (const QString& f : before) added.removeAll(f);
        return added.size() == 1;
    }));
    EXPECT_TRUE(added.front().startsWith(QStringLiteral("Town hall")));
    QFile::remove(vdir.filePath(added.front()));

    // A module's copy, straight into the Module library folder under its name.
    QTemporaryDir modules;
    window_->findChild<ui::ModuleLibraryPanel*>()->setLibraryPath(modules.path());
    http_.replyRaw("/api/modules/m1/snapshot", 200, sync::moduleSnapshotFor(*sampleModule(), {}), "application/octet-stream");
    moreAction(w->row(Tab::Modules, QStringLiteral("m1")), "saveModuleCopy")->trigger();
    ASSERT_TRUE(waitFor([&] { return QFile::exists(QDir(modules.path()).filePath(QStringLiteral("Freight yard.bbm"))); }));
}

TEST_F(ServerWindowMain, FileSaveToServerReplacesPublishAndUploadMyParts) {
    QStringList file;
    for (QAction* a : window_->menuBar()->actions().first()->menu()->actions()) file << a->text();
    EXPECT_TRUE(file.contains(QStringLiteral("Save &to Server...")));
    EXPECT_FALSE(file.contains(QStringLiteral("&Publish to Server...")));
    EXPECT_FALSE(file.contains(QStringLiteral("&Upload My Parts to Server...")));
    // Right after Open from Server.
    EXPECT_EQ(file.indexOf(QStringLiteral("Save &to Server...")), file.indexOf(QStringLiteral("Open from &Server...")) + 1);
}

TEST_F(ServerWindowMain, AVenueGoesToTheServerFromTheVenueLibraryAsYoursOrAClubs) {
    auto* venues = window_->findChild<ui::VenueLibraryPanel*>();
    QTemporaryDir dir;
    venues->setLibraryPath(dir.path());
    core::Venue v;
    v.name = QStringLiteral("Gym");
    v.minWalkwayStuds = 30;
    ASSERT_TRUE(saveload::writeVenueFile(QDir(dir.path()).filePath(QStringLiteral("Gym.bld-venue")), v));
    venues->refresh();
    std::optional<std::pair<QString, QString>> answer = std::make_pair(QStringLiteral("Gym for shows"), QStringLiteral("train"));
    QString askedFor;
    window_->askSaveToServer = [&](const QString& what, const QString& name) {
        askedFor = what + QLatin1Char(':') + name;
        return answer;
    };
    http_.reply("/api/venues", 201, { { QStringLiteral("id"), QStringLiteral("V7") }, { QStringLiteral("name"), QStringLiteral("Gym for shows") } });
    auto* list = venues->findChild<QListWidget*>();
    ASSERT_TRUE(list && list->count() == 1);
    list->setCurrentRow(0);
    auto* send = venues->findChild<QPushButton*>(QStringLiteral("venueSaveToServer"));
    ASSERT_TRUE(send && !send->isHidden());
    send->click();
    ASSERT_TRUE(waitFor([&] { return count(http_, "POST", "/api/venues") == 1; }));
    EXPECT_EQ(askedFor, QStringLiteral("venue:Gym"));
    QJsonObject body;
    for (const auto& r : http_.requests)
        if (r.method == "POST" && r.path == "/api/venues") body = QJsonDocument::fromJson(r.body).object();
    EXPECT_EQ(body.value(QStringLiteral("name")).toString(), QStringLiteral("Gym for shows"));
    EXPECT_EQ(body.value(QStringLiteral("orgSlug")).toString(), QStringLiteral("train"));
    const QJsonObject data = body.value(QStringLiteral("data")).toObject();
    EXPECT_FALSE(data.contains(QStringLiteral("schema")));
    EXPECT_EQ(data.value(QStringLiteral("name")).toString(), QStringLiteral("Gym"));
    EXPECT_EQ(data.value(QStringLiteral("minWalkwayStuds")).toDouble(), 30.0);
    // Cancelled: nothing goes.
    answer.reset();
    send->click();
    waitFor([&] { return count(http_, "POST", "/api/venues") > 1; }, 300);
    EXPECT_EQ(count(http_, "POST", "/api/venues"), 1);
}

TEST_F(ServerWindowMain, TheServerWindowListsWhatsOnlyHereAndFollowsTheFolders) {
    QTemporaryDir venueDir, moduleDir;
    window_->findChild<ui::VenueLibraryPanel*>()->setLibraryPath(venueDir.path());
    window_->findChild<ui::ModuleLibraryPanel*>()->setLibraryPath(moduleDir.path());
    core::Venue v;
    v.name = QStringLiteral("Gym");
    ASSERT_TRUE(saveload::writeVenueFile(QDir(venueDir.path()).filePath(QStringLiteral("Gym.bld-venue")), v));
    ASSERT_TRUE(saveload::writeVenueFile(QDir(venueDir.path()).filePath(QStringLiteral("Town hall.bld-venue")), v));
    ui::ServerWindow* w = openWindow();
    ASSERT_TRUE(waitFor([&] { return w->localShown(Tab::Venues) == QStringList{ QStringLiteral("Gym") }; }));
    // The open layout isn't on the server yet.
    EXPECT_TRUE(w->findChild<QFrame*>(QStringLiteral("localRow:open")));
    // A new file in the folder shows up by itself.
    ASSERT_TRUE(saveload::writeVenueFile(QDir(venueDir.path()).filePath(QStringLiteral("Barn.bld-venue")), v));
    ASSERT_TRUE(waitFor([&] { return w->localShown(Tab::Venues) == (QStringList{ QStringLiteral("Barn"), QStringLiteral("Gym") }); }));
    // A Module library file goes to the server as a new module, then as its first version.
    ASSERT_TRUE(saveload::writeBbm(*sampleModule(), QDir(moduleDir.path()).filePath(QStringLiteral("Siding.bbm"))).ok);
    ASSERT_TRUE(waitFor([&] { return w->localShown(Tab::Modules).contains(QStringLiteral("Siding")); }));
    window_->askSaveToServer = [](const QString&, const QString& name) { return std::make_optional(std::make_pair(name, QString())); };
    // GET and POST share the path here: from now on it answers the POST.
    http_.clear("/api/modules");
    http_.reply("/api/modules", 201, { { QStringLiteral("id"), QStringLiteral("n1") }, { QStringLiteral("title"), QStringLiteral("Siding") } });
    // GET (what the server holds: nothing yet), then PUT (the first version).
    http_.replyRaw("/api/modules/n1/snapshot", 200, QByteArray(), "application/octet-stream");
    http_.reply("/api/modules/n1/snapshot", 200, { { QStringLiteral("version"), 1 } });
    http_.reply("/api/modules/n1/thumbnail", 200, { { QStringLiteral("ok"), true } });
    // Any box that opens is a failure: say which and close it.
    QStringList boxes;
    QTimer closer;
    QObject::connect(&closer, &QTimer::timeout, [&] {
        if (QWidget* m = QApplication::activeModalWidget()) {
            boxes << m->windowTitle();
            m->close();
        }
    });
    closer.start(50);
    w->findChild<QFrame*>(QStringLiteral("localRow:file:") + QDir(moduleDir.path()).filePath(QStringLiteral("Siding.bbm")))
        ->findChild<QPushButton*>(QStringLiteral("sendLocal"))
        ->click();
    ASSERT_TRUE(waitFor([&] { return count(http_, "POST", "/api/modules") == 1; }));
    QJsonObject body;
    for (const auto& r : http_.requests)
        if (r.method == "POST" && r.path == "/api/modules") body = QJsonDocument::fromJson(r.body).object();
    EXPECT_EQ(body.value(QStringLiteral("title")).toString(), QStringLiteral("Siding"));
    const bool thumbnailed = waitFor([&] { return count(http_, "PUT", "/api/modules/n1/thumbnail") == 1; });
    QStringList seen;
    for (const auto& r : http_.requests) seen << QString::fromLatin1(r.method + ' ' + r.path);
    ASSERT_TRUE(thumbnailed) << seen.join(QStringLiteral(", ")).toStdString() << " / boxes: " << boxes.join(QStringLiteral(", ")).toStdString();
    EXPECT_EQ(count(http_, "PUT", "/api/modules/n1/snapshot"), 1);
    EXPECT_TRUE(boxes.isEmpty()) << boxes.join(QStringLiteral(", ")).toStdString();
}

TEST_F(ServerWindowMain, TheServersLiveHintsReachTheWindow) {
    ui::ServerWindow* w = openWindow();
    const int before = count(http_, "GET", "/api/venues");
    auto* events = window_->findChild<sync::EventStream*>();
    ASSERT_TRUE(events);
    emit events->hint(QJsonObject{ { QStringLiteral("kind"), QStringLiteral("venue") }, { QStringLiteral("action"), QStringLiteral("created") } });
    ASSERT_TRUE(waitFor([&] { return count(http_, "GET", "/api/venues") == before + 1; }));
    EXPECT_TRUE(w->isVisible());
}
