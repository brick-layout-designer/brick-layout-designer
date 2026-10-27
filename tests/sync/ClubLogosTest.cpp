// Club logos (the web's club logos): each club's logo beside its name in the
// Server window's Show filter, the Save to pickers (Save to Server, the
// layout's Save to Server…), the Module library's club groups; kept per
// server in the data folder; a new or removed logo showing without a
// restart; nothing asked of a server without logos, and a logo it won't
// send left out quietly; and, for a club's admins, "Change the logo on the
// website".

#include "FakeHttp.h"

#include "ClubLogos.h"
#include "ConnectDialog.h"
#include "ServerList.h"
#include "TokenStore.h"
#include "ui/SendToServerDialogs.h"
#include "ui/ServerLibrary.h"
#include "ui/ServerWindow.h"

#include <gtest/gtest.h>

#include <QBuffer>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QLabel>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>

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

QByteArray picture(Qt::GlobalColor color, const char* format = "PNG") {
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(color);
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, format);
    return bytes;
}

// What the server says about a club's logo: nothing (a server before
// logos), null (none) or its address.
enum class Logo { Absent, None, V1, V2 };

const QByteArray kLogoV1 = QByteArrayLiteral("/api/orgs/train/logo?v=1");
const QByteArray kLogoV2 = QByteArrayLiteral("/api/orgs/train/logo?v=2");

QJsonObject orgsJson(Logo logo, bool admin = false) {
    QJsonObject train{ { QStringLiteral("slug"), QStringLiteral("train") },
                       { QStringLiteral("name"), QStringLiteral("Train Club") },
                       { QStringLiteral("myRole"), admin ? QStringLiteral("admin") : QStringLiteral("member") },
                       { QStringLiteral("isAdmin"), admin },
                       { QStringLiteral("permissions"), QJsonArray{ QStringLiteral("start_layouts"), QStringLiteral("add_modules"),
                                                                     QStringLiteral("add_venues") } } };
    if (logo == Logo::None) train.insert(QStringLiteral("logoUrl"), QJsonValue::Null);
    if (logo == Logo::V1) train.insert(QStringLiteral("logoUrl"), QString::fromLatin1(kLogoV1));
    if (logo == Logo::V2) train.insert(QStringLiteral("logoUrl"), QString::fromLatin1(kLogoV2));
    return { { QStringLiteral("orgs"), QJsonArray{ train } } };
}

QJsonObject modulesJson() {
    const QJsonObject owner{ { QStringLiteral("kind"), QStringLiteral("org") }, { QStringLiteral("id"), QStringLiteral("o1") },
                             { QStringLiteral("name"), QStringLiteral("Train Club") }, { QStringLiteral("slug"), QStringLiteral("train") } };
    return { { QStringLiteral("modules"),
               QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("c1") }, { QStringLiteral("title"), QStringLiteral("Engine shed") },
                                        { QStringLiteral("role"), QStringLiteral("editor") }, { QStringLiteral("owner"), owner },
                                        { QStringLiteral("docVersion"), 1 }, { QStringLiteral("latestVersion"), 1 } } } } };
}

int requests(const FakeHttp& http, const QByteArray& path) {
    int n = 0;
    for (const auto& r : http.requests)
        if (r.path == path) ++n;
    return n;
}

int logoRequests(const FakeHttp& http) {
    int n = 0;
    for (const auto& r : http.requests)
        if (r.path.contains("/logo")) ++n;
    return n;
}

QColor iconColor(const QIcon& icon) {
    if (icon.isNull()) return {};
    return icon.pixmap(16, 16).toImage().pixelColor(8, 8);
}

}  // namespace

class ClubLogosTest : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ASSERT_TRUE(root_.isValid());
        logos().forget();
        logos().setRoot(root_.path());
        library_.setPictureCacheRoot(root_.path() + QStringLiteral("/pictures"));
        QSettings().remove(QStringLiteral("serverWindow"));
        sync::ServerList list;
        list.add(http_.base(), QStringLiteral("Club server"));
        list.save();
        http_.reply("/api/catalog/settings", 200, {});
        http_.reply("/api/modules", 200, modulesJson());
        http_.reply("/api/layouts", 200, { { QStringLiteral("layouts"), QJsonArray{} } });
        http_.reply("/api/venues", 200, { { QStringLiteral("venues"), QJsonArray{} } });
        http_.reply("/api/custom-parts", 200, { { QStringLiteral("parts"), QJsonArray{} } });
        http_.replyRaw(kLogoV1, 200, picture(Qt::red), "image/png");
        http_.replyRaw(kLogoV2, 200, picture(Qt::blue), "image/png");
    }
    void TearDown() override {
        window_.reset();
        logos().forget();
        logos().setRoot({});
        QSettings().setValue(QLatin1String(sync::ServerList::kKey), QByteArray("[]"));
        QSettings().remove(QStringLiteral("serverWindow"));
        QSettings().remove(QStringLiteral("sync/ownerFilter"));
        QStandardPaths::setTestModeEnabled(false);
    }
    static sync::ClubLogos& logos() { return sync::ClubLogos::shared(); }

    // Signed in with the club list answering as `logo` says, and the Server window open.
    void open(Logo logo, bool admin = false) {
        http_.reply("/api/orgs", 200, orgsJson(logo, admin));
        library_.setServer(http_.base(), QStringLiteral("Club server"), QStringLiteral("bld_pat_logo"));
        ASSERT_TRUE(waitFor([&] { return library_.state() == ui::ServerLibrary::State::Ready; })) << library_.stateText().toStdString();
        window_ = std::make_unique<ui::ServerWindow>(library_);
        window_->openLink = [this](const QUrl& url) { opened_ << url; };
        window_->showTab(ui::ServerWindow::Tab::Layouts);
        QCoreApplication::processEvents();
    }
    // The club list again (a club hint from the server), now answering as `logo` says.
    void clubChanged(Logo logo, bool admin = false) {
        http_.clear("/api/orgs");
        http_.reply("/api/orgs", 200, orgsJson(logo, admin));
        library_.refresh();
    }
    QIcon showIcon() const {
        QComboBox* show = window_->showFilter();
        const int at = show->findData(QStringLiteral("train"));
        return at < 0 ? QIcon() : show->itemIcon(at);
    }
    QStringList keptFiles() const {
        return QDir(logos().folder(http_.base())).entryList({ QStringLiteral("*.img") }, QDir::Files);
    }
    void pickTrain() {
        QComboBox* show = window_->showFilter();
        const int at = show->findData(QStringLiteral("train"));
        ASSERT_GE(at, 0);
        show->setCurrentIndex(at);
        emit show->activated(at);
    }

    QTemporaryDir root_;
    FakeHttp http_;
    ui::ServerLibrary library_;
    std::unique_ptr<ui::ServerWindow> window_;
    QList<QUrl> opened_;
};

TEST_F(ClubLogosTest, ALogoShowsBesideTheClubWhereverClubsArePicked) {
    open(Logo::V1);
    ASSERT_TRUE(waitFor([&] { return !logos().logo(http_.base(), QStringLiteral("train")).isNull(); }));
    // Fetched once, signed in.
    EXPECT_EQ(requests(http_, kLogoV1), 1);
    for (const auto& r : http_.requests) {
        if (r.path == kLogoV1) {
            EXPECT_EQ(r.authorization, QByteArray("Bearer bld_pat_logo"));
        }
    }

    // The Server window's Show filter (the Open from Server club picker); All and Mine have none.
    ASSERT_TRUE(waitFor([&] { return !showIcon().isNull(); }));
    EXPECT_EQ(iconColor(showIcon()), QColor(Qt::red));
    EXPECT_TRUE(window_->showFilter()->itemIcon(0).isNull());
    EXPECT_TRUE(window_->showFilter()->itemIcon(1).isNull());

    // Save to Server's "Save to".
    ui::SaveToServerDialog save(QStringLiteral("venue"), sync::AddKind::Venue, QStringLiteral("Gym"), QStringLiteral("Club server"),
                                library_.orgs(), QString(), nullptr);
    save.showLogosFrom(http_.base());
    ASSERT_EQ(save.owner()->count(), 2);
    EXPECT_TRUE(save.owner()->itemIcon(0).isNull());
    EXPECT_EQ(iconColor(save.owner()->itemIcon(1)), QColor(Qt::red));

    // The Module library's group for the club.
    ui::ServerModulesTab tab(library_);
    tab.rebuild();
    auto* header = tab.findChild<QWidget*>(QStringLiteral("clubHeader:train"));
    ASSERT_NE(header, nullptr);
    auto* logo = header->findChild<QLabel*>(QStringLiteral("clubLogo"));
    ASSERT_NE(logo, nullptr);
    EXPECT_FALSE(logo->isHidden());
    EXPECT_FALSE(logo->pixmap().isNull());

    // Kept in the data folder, under this server's folder.
    EXPECT_TRUE(logos().folder(http_.base()).startsWith(root_.path()));
    EXPECT_TRUE(logos().folder(http_.base()).endsWith(sync::ServerList::folderName(http_.base())));
    EXPECT_EQ(keptFiles().size(), 1);
}

TEST_F(ClubLogosTest, AClubWithoutALogoKeepsItsPlainLook) {
    open(Logo::None);
    EXPECT_TRUE(logos().offered(http_.base()));
    EXPECT_FALSE(waitFor([&] { return !showIcon().isNull(); }, 300));
    EXPECT_GE(window_->showFilter()->findData(QStringLiteral("train")), 0);
    EXPECT_EQ(logoRequests(http_), 0);

    ui::ServerModulesTab tab(library_);
    tab.rebuild();
    auto* header = tab.findChild<QWidget*>(QStringLiteral("clubHeader:train"));
    ASSERT_NE(header, nullptr);
    EXPECT_TRUE(header->findChild<QLabel*>(QStringLiteral("clubLogo"))->isHidden());
    EXPECT_TRUE(keptFiles().isEmpty());
}

TEST_F(ClubLogosTest, AServerWithoutLogosIsNeverAskedForOne) {
    open(Logo::Absent, true);
    EXPECT_FALSE(logos().offered(http_.base()));
    EXPECT_FALSE(waitFor([&] { return !showIcon().isNull(); }, 300));
    EXPECT_EQ(logoRequests(http_), 0);
    // Not even its admins are sent to the website for one.
    pickTrain();
    EXPECT_TRUE(window_->clubLogoLink()->isHidden());
    EXPECT_FALSE(window_->clubLogoPage().isValid());
}

TEST_F(ClubLogosTest, ALogoTheServerWontSendIsLeftOutQuietly) {
    http_.clear(kLogoV1);  // 404
    open(Logo::V1);
    ASSERT_TRUE(waitFor([&] { return requests(http_, kLogoV1) == 1; }));
    EXPECT_FALSE(waitFor([&] { return !showIcon().isNull(); }, 300));
    EXPECT_EQ(library_.state(), ui::ServerLibrary::State::Ready);
    EXPECT_TRUE(window_->statusLine()->isHidden());
    // Not asked again with the next club list.
    clubChanged(Logo::V1);
    ASSERT_TRUE(waitFor([&] { return requests(http_, "/api/orgs") >= 2; }));
    EXPECT_FALSE(waitFor([&] { return requests(http_, kLogoV1) > 1; }, 300));
}

TEST_F(ClubLogosTest, ANewOrRemovedLogoShowsWithoutARestart) {
    open(Logo::V1);
    ASSERT_TRUE(waitFor([&] { return iconColor(showIcon()) == QColor(Qt::red); }));
    ui::ServerModulesTab tab(library_);
    tab.rebuild();

    // A club admin uploads a new one on the web: the club hint asks for the list again.
    clubChanged(Logo::V2);
    ASSERT_TRUE(waitFor([&] { return iconColor(showIcon()) == QColor(Qt::blue); }));
    EXPECT_EQ(requests(http_, kLogoV2), 1);
    // Only the new picture is kept.
    EXPECT_EQ(keptFiles().size(), 1);
    auto* logo = tab.findChild<QWidget*>(QStringLiteral("clubHeader:train"))->findChild<QLabel*>(QStringLiteral("clubLogo"));
    EXPECT_EQ(logo->pixmap().toImage().pixelColor(5, 5), QColor(Qt::blue));

    // And removes it: the plain look again, and nothing kept.
    clubChanged(Logo::None);
    ASSERT_TRUE(waitFor([&] { return showIcon().isNull(); }));
    EXPECT_TRUE(keptFiles().isEmpty());
    logo = tab.findChild<QWidget*>(QStringLiteral("clubHeader:train"))->findChild<QLabel*>(QStringLiteral("clubLogo"));
    EXPECT_TRUE(logo->isHidden());
}

TEST_F(ClubLogosTest, AKeptLogoShowsNextTimeWithoutAskingTheServer) {
    open(Logo::V1);
    ASSERT_TRUE(waitFor([&] { return !showIcon().isNull(); }));
    // The next run: nothing in memory, the server would say 404 now.
    window_.reset();
    logos().forget();
    http_.clear(kLogoV1);
    window_ = std::make_unique<ui::ServerWindow>(library_);
    window_->showTab(ui::ServerWindow::Tab::Layouts);
    clubChanged(Logo::V1);
    ASSERT_TRUE(waitFor([&] { return iconColor(showIcon()) == QColor(Qt::red); }));
    EXPECT_EQ(requests(http_, kLogoV1), 1);
}

// Closing the Server window while its club's logo and its lists are still on
// their way: the answers that land afterwards must not reach the closed
// window (a crash before), and the logo is still kept for next time.
TEST_F(ClubLogosTest, ClosingTheWindowWhileALogoIsOnItsWayIsSafe) {
    const QList<QByteArray> slow{ kLogoV1, "/api/layouts", "/api/venues", "/api/custom-parts" };
    for (const QByteArray& p : slow) http_.hold.insert(p);
    open(Logo::V1);
    ASSERT_TRUE(waitFor([&] {
        return std::all_of(slow.begin(), slow.end(), [&](const QByteArray& p) { return requests(http_, p) == 1; });
    }));
    // Closed with all four in flight.
    window_.reset();
    for (const QByteArray& p : slow) http_.release(p);
    ASSERT_TRUE(waitFor([&] { return !logos().logo(http_.base(), QStringLiteral("train")).isNull(); }));
    ASSERT_TRUE(waitFor([&] { return library_.api().pending() == 0; }));
    QCoreApplication::processEvents();
    EXPECT_EQ(keptFiles().size(), 1);

    // Opened again: the logo shows without asking for it again.
    window_ = std::make_unique<ui::ServerWindow>(library_);
    window_->showTab(ui::ServerWindow::Tab::Layouts);
    ASSERT_TRUE(waitFor([&] { return iconColor(showIcon()) == QColor(Qt::red); }));
    EXPECT_EQ(requests(http_, kLogoV1), 1);
}

TEST_F(ClubLogosTest, AClubsAdminsAreSentToTheWebsiteToChangeIt) {
    open(Logo::None, true);
    // All: no club picked.
    EXPECT_TRUE(window_->clubLogoLink()->isHidden());
    pickTrain();
    EXPECT_FALSE(window_->clubLogoLink()->isHidden());
    EXPECT_EQ(window_->clubLogoLink()->text().contains(QStringLiteral("Change the logo on the website")), true);
    emit window_->clubLogoLink()->linkActivated(QStringLiteral("logo"));
    ASSERT_EQ(opened_.size(), 1);
    QUrl expected = http_.base();
    expected.setPath(QStringLiteral("/orgs/train/admin"));
    expected.setQuery(QStringLiteral("tab=settings"));
    EXPECT_EQ(opened_.first(), expected);

    // No longer its admin (roles changed on the web): the link goes.
    clubChanged(Logo::None, false);
    ASSERT_TRUE(waitFor([&] { return window_->clubLogoLink()->isHidden(); }));
}

TEST_F(ClubLogosTest, AMemberIsNotOfferedTheLink) {
    open(Logo::V1, false);
    pickTrain();
    EXPECT_TRUE(window_->clubLogoLink()->isHidden());
}

// File › Save to Server…'s "Save to" (the connect dialog) and the logo the server really sends (WebP).
TEST_F(ClubLogosTest, TheLayoutsSaveToPickerShowsTheServersWebpLogo) {
    if (!QImageReader::supportedImageFormats().contains("webp")) GTEST_SKIP() << "no Qt WebP reader (qtimageformats)";
    http_.clear(kLogoV1);
    http_.replyRaw(kLogoV1, 200, picture(Qt::red, "WEBP"), "image/webp");
    http_.reply("/api/version", 200,
                { { QStringLiteral("version"), QStringLiteral("2.0.0") },
                  { QStringLiteral("schemaVersion"), 1 },
                  { QStringLiteral("protocols"), QJsonArray{ QStringLiteral("y-websocket/1") } } });
    http_.reply("/api/orgs", 200, orgsJson(Logo::V1));
    sync::ServerApi api;
    sync::MemoryTokenStore tokens;
    tokens.save(http_.base(), QStringLiteral("bld_pat_saved"));
    sync::ConnectDialog dialog(api, tokens, [](const QUrl&) {}, nullptr, sync::ConnectDialog::Purpose::Publish);
    dialog.setAddress(http_.base().toString());
    dialog.setPublishContent(QByteArrayLiteral("<Map/>"), {}, QStringLiteral("Yard"));
    dialog.connectToServer();
    auto* owner = dialog.findChild<QComboBox*>(QStringLiteral("publishOwner"));
    ASSERT_TRUE(waitFor([&] { return owner->count() == 2; }));
    ASSERT_TRUE(waitFor([&] { return !owner->itemIcon(1).isNull(); }));
    EXPECT_GE(iconColor(owner->itemIcon(1)).red(), 240);
    EXPECT_TRUE(owner->itemIcon(0).isNull());
}
