// A club's members on the desktop (the Server window's Members… for the
// club picked in Show), read only, as the website lists them: each member's
// name and roles (several each), last active only where the server sends
// it, the club's logo at the top, "Manage members on the website" only for
// people whose roles may manage members, and a role change showing without
// a restart.

#include "FakeHttp.h"

#include "ClubLogos.h"
#include "ServerList.h"
#include "ui/ClubMembersDialog.h"
#include "ui/ServerLibrary.h"
#include "ui/ServerWindow.h"

#include <gtest/gtest.h>

#include <QBuffer>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

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

QByteArray picture(Qt::GlobalColor color) {
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(color);
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes;
}

const QByteArray kMembers = QByteArrayLiteral("/api/orgs/train/members");
const QByteArray kLogo = QByteArrayLiteral("/api/orgs/train/logo?v=1");

// Your roles in Train Club: an admin, someone whose role invites people, or a member.
enum class Me { Admin, Inviter, Member };

QJsonObject orgsJson(Me me) {
    QJsonArray perms{ QStringLiteral("view_items"), QStringLiteral("edit_layouts") };
    if (me != Me::Member) perms.append(QStringLiteral("invite"));
    const QJsonObject train{ { QStringLiteral("slug"), QStringLiteral("train") },
                             { QStringLiteral("name"), QStringLiteral("Train Club") },
                             { QStringLiteral("myRole"),
                               me == Me::Admin ? QStringLiteral("admin") : QStringLiteral("member") },
                             { QStringLiteral("isAdmin"), me == Me::Admin },
                             { QStringLiteral("permissions"), perms },
                             { QStringLiteral("logoUrl"), QString::fromLatin1(kLogo) } };
    return { { QStringLiteral("orgs"), QJsonArray{ train } } };
}

QJsonObject member(const QString& id, const QString& name, const QStringList& roles,
                   const QJsonValue& lastActive) {
    return { { QStringLiteral("userId"), id },
             { QStringLiteral("displayName"), name },
             { QStringLiteral("role"), QStringLiteral("member") },
             { QStringLiteral("roleIds"), QJsonArray::fromStringList(roles) },
             { QStringLiteral("joinedAt"), 1767225600000.0 },
             { QStringLiteral("email"), QString() },
             { QStringLiteral("lastActive"), lastActive } };
}

// What the server sends: as an admin sees it (everyone's last active), or as
// a member does (only their own). `benAdmin`: Ben has been given Admin since.
QJsonObject membersJson(bool asAdmin, bool benAdmin = false) {
    const QJsonValue none = QJsonValue::Null;
    QStringList bens{ QStringLiteral("o1:member"), QStringLiteral("crew") };
    if (benAdmin) bens.prepend(QStringLiteral("o1:admin"));
    return { { QStringLiteral("roles"),
               QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("o1:admin") },
                                        { QStringLiteral("name"), QStringLiteral("Admin") } },
                           QJsonObject{ { QStringLiteral("id"), QStringLiteral("o1:manager") },
                                        { QStringLiteral("name"), QStringLiteral("Manager") } },
                           QJsonObject{ { QStringLiteral("id"), QStringLiteral("o1:member") },
                                        { QStringLiteral("name"), QStringLiteral("Member") } },
                           QJsonObject{ { QStringLiteral("id"), QStringLiteral("o1:viewer") },
                                        { QStringLiteral("name"), QStringLiteral("Viewer") } },
                           QJsonObject{ { QStringLiteral("id"), QStringLiteral("crew") },
                                        { QStringLiteral("name"), QStringLiteral("Track crew") } } } },
             { QStringLiteral("members"),
               QJsonArray{
                   member(QStringLiteral("ann"), QStringLiteral("Ann Admin"), { QStringLiteral("o1:admin") },
                          asAdmin ? QJsonValue(QStringLiteral("today")) : none),
                   member(QStringLiteral("ben"), QStringLiteral("Ben Builder"), bens,
                          asAdmin ? QJsonValue(QStringLiteral("week")) : QJsonValue(QStringLiteral("today"))),
                   member(QStringLiteral("cy"), QStringLiteral("Cy Quiet"), { QStringLiteral("o1:viewer") },
                          none) } },
             { QStringLiteral("invites"), QJsonArray{} } };
}

QStringList chips(QWidget* row) {
    QStringList out;
    if (!row) return out;
    for (auto* l : row->findChildren<QLabel*>(QStringLiteral("memberRole"))) out << l->text();
    return out;
}

QLabel* lastActive(QWidget* row) {
    return row ? row->findChild<QLabel*>(QStringLiteral("lastActive")) : nullptr;
}

int requests(const FakeHttp& http, const QByteArray& path) {
    int n = 0;
    for (const auto& r : http.requests)
        if (r.path == path) ++n;
    return n;
}

} // namespace

class ClubMembersTest : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ASSERT_TRUE(root_.isValid());
        sync::ClubLogos::shared().forget();
        sync::ClubLogos::shared().setRoot(root_.path());
        library_.setPictureCacheRoot(root_.path() + QStringLiteral("/pictures"));
        QSettings().remove(QStringLiteral("serverWindow"));
        sync::ServerList list;
        list.add(http_.base(), QStringLiteral("Club server"));
        list.save();
        http_.reply("/api/catalog/settings", 200, {});
        http_.reply("/api/modules", 200, { { QStringLiteral("modules"), QJsonArray{} } });
        http_.reply("/api/layouts", 200, { { QStringLiteral("layouts"), QJsonArray{} } });
        http_.reply("/api/venues", 200, { { QStringLiteral("venues"), QJsonArray{} } });
        http_.reply("/api/custom-parts", 200, { { QStringLiteral("parts"), QJsonArray{} } });
        http_.replyRaw(kLogo, 200, picture(Qt::red), "image/png");
    }
    void TearDown() override {
        window_.reset();
        sync::ClubLogos::shared().forget();
        sync::ClubLogos::shared().setRoot({});
        QSettings().setValue(QLatin1String(sync::ServerList::kKey), QByteArray("[]"));
        QSettings().remove(QStringLiteral("serverWindow"));
        QSettings().remove(QStringLiteral("sync/ownerFilter"));
        QStandardPaths::setTestModeEnabled(false);
    }

    // Signed in as `me`, the Server window open on Train Club, and its Members… opened.
    ui::ClubMembersDialog* openMembers(Me me, const QJsonObject& members) {
        http_.reply("/api/orgs", 200, orgsJson(me));
        http_.reply(kMembers, 200, members);
        library_.setServer(http_.base(), QStringLiteral("Club server"), QStringLiteral("bld_pat_members"));
        EXPECT_TRUE(waitFor([&] { return library_.state() == ui::ServerLibrary::State::Ready; }));
        window_ = std::make_unique<ui::ServerWindow>(library_);
        window_->openLink = [this](const QUrl& url) { opened_ << url; };
        window_->showTab(ui::ServerWindow::Tab::Layouts);
        QCoreApplication::processEvents();
        // All: no club picked, so no Members….
        EXPECT_TRUE(window_->clubMembersButton()->isHidden());
        QComboBox* show = window_->showFilter();
        const int at = show->findData(QStringLiteral("train"));
        EXPECT_GE(at, 0);
        show->setCurrentIndex(at);
        emit show->activated(at);
        EXPECT_FALSE(window_->clubMembersButton()->isHidden());
        window_->clubMembersButton()->click();
        ui::ClubMembersDialog* dialog = window_->membersDialog();
        EXPECT_NE(dialog, nullptr);
        if (!dialog) return nullptr;
        EXPECT_TRUE(waitFor([&] { return dialog->row(QStringLiteral("cy")) != nullptr; }));
        return dialog;
    }

    QTemporaryDir root_;
    FakeHttp http_;
    ui::ServerLibrary library_;
    std::unique_ptr<ui::ServerWindow> window_;
    QList<QUrl> opened_;
};

TEST_F(ClubMembersTest, ListsEachMembersRolesAndLastActiveWithTheClubsLogo) {
    ui::ClubMembersDialog* d = openMembers(Me::Admin, membersJson(true));
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->windowTitle(), QStringLiteral("Members of Train Club"));
    // Several roles each, in the club's order.
    EXPECT_EQ(chips(d->row(QStringLiteral("ben"))),
              (QStringList{ QStringLiteral("Member"), QStringLiteral("Track crew") }));
    EXPECT_EQ(chips(d->row(QStringLiteral("ann"))), QStringList{ QStringLiteral("Admin") });
    EXPECT_EQ(chips(d->row(QStringLiteral("cy"))), QStringList{ QStringLiteral("Viewer") });
    // Last active as the server sent it; nothing where it sent none.
    ASSERT_NE(lastActive(d->row(QStringLiteral("ben"))), nullptr);
    EXPECT_FALSE(lastActive(d->row(QStringLiteral("ben")))->isHidden());
    EXPECT_EQ(lastActive(d->row(QStringLiteral("ben")))->text(), QStringLiteral("Active this week"));
    EXPECT_EQ(lastActive(d->row(QStringLiteral("ann")))->text(), QStringLiteral("Active today"));
    EXPECT_TRUE(lastActive(d->row(QStringLiteral("cy")))->isHidden());
    EXPECT_EQ(d->findChild<QLabel*>(QStringLiteral("memberCount"))->text(), QStringLiteral("3 members"));
    EXPECT_TRUE(d->statusLine()->isHidden());
    // Asked with the desktop's sign-in.
    bool signedIn = false;
    for (const auto& r : http_.requests)
        if (r.path == kMembers) signedIn = r.authorization == "Bearer bld_pat_members";
    EXPECT_TRUE(signedIn);
    // The club's logo heads it.
    ASSERT_TRUE(waitFor([&] { return !d->logo()->pixmap().isNull(); }));
    EXPECT_GE(d->logo()->pixmap().toImage().pixelColor(20, 20).red(), 240);
    // An admin may manage members: the link goes to the club's People page.
    EXPECT_FALSE(d->manageLink()->isHidden());
    emit d->manageLink()->linkActivated(QStringLiteral("manage"));
    ASSERT_EQ(opened_.size(), 1);
    QUrl expected = http_.base();
    expected.setPath(QStringLiteral("/orgs/train/admin"));
    expected.setQuery(QStringLiteral("tab=people"));
    EXPECT_EQ(opened_.first(), expected);
}

namespace {

// The club the website's orgs/test/memberOrder.test.tsx sorts: anyone with
// the Admin role first, then by how many permissions their roles grant
// together, then by name. Listed in the server's order, not the order shown.
QJsonObject strongestRolesClub() {
    const QStringList all{ "view_items",     "edit_layouts",  "start_layouts", "edit_modules",
                           "add_parts",      "manage_venues", "send_catalog",  "invite",
                           "manage_members", "export_data",   "delete_items" };
    QStringList manager = all;
    manager.removeAll(QStringLiteral("manage_members"));
    manager.removeAll(QStringLiteral("export_data"));
    const auto role = [](const char* id, const char* key, const char* name, const QStringList& perms) {
        return QJsonObject{ { QStringLiteral("id"), QString::fromLatin1(id) },
                            { QStringLiteral("key"),
                              *key ? QJsonValue(QString::fromLatin1(key)) : QJsonValue() },
                            { QStringLiteral("name"), QString::fromLatin1(name) },
                            { QStringLiteral("permissions"), QJsonArray::fromStringList(perms) } };
    };
    const QJsonValue none = QJsonValue::Null;
    return { { QStringLiteral("roles"),
               QJsonArray{
                   role("o1:admin", "admin", "Admin", all), role("o1:manager", "manager", "Manager", manager),
                   role("o1:member", "member", "Member",
                        { "view_items", "edit_layouts", "start_layouts", "edit_modules", "add_parts" }),
                   role("o1:viewer", "viewer", "Viewer", { "view_items" }),
                   role("crew", "", "Track crew", { "view_items", "edit_layouts" }),
                   role("door", "", "Door crew", { "view_items", "invite", "manage_members" }) } },
             { QStringLiteral("members"),
               QJsonArray{
                   member(QStringLiteral("zed"), QStringLiteral("Zed"), { "o1:viewer" }, none),
                   member(QStringLiteral("abe"), QStringLiteral("Abe"), { "o1:member" }, none),
                   member(QStringLiteral("cal"), QStringLiteral("Cal"), { "o1:viewer", "crew" }, none),
                   member(QStringLiteral("dee"), QStringLiteral("Dee"), { "crew", "o1:member" }, none),
                   member(QStringLiteral("yan"), QStringLiteral("Yan"), { "o1:admin" }, none),
                   member(QStringLiteral("bob"), QStringLiteral("Bob"), { "o1:member", "o1:manager" }, none),
                   // (Amy's id is "cy": the dialog's helper waits for that row.)
                   member(QStringLiteral("cy"), QStringLiteral("Amy"), { "o1:member", "door" }, none) } },
             { QStringLiteral("invites"), QJsonArray{} } };
}

QStringList names(const QList<sync::ClubMember>& members) {
    QStringList out;
    for (const auto& m : members) out << m.name;
    return out;
}

} // namespace

TEST(ClubMemberOrder, StrongestRolesFirstThenPermissionsThenNameAsTheWebsite) {
    sync::ClubMembers club = sync::LibraryApi::clubMembersFromJson(strongestRolesClub());
    // Admin; then 9, 7, 5, 5 (by name), 2 and 1 permissions.
    EXPECT_EQ(names(club.sorted()), QString("Yan Bob Amy Abe Dee Cal Zed").split(QLatin1Char(' ')));
    // A role edited to grant more moves its holders up.
    for (auto& r : club.roles)
        if (r.id == QLatin1String("crew")) {
            r.permissions = club.roles.first().permissions;
            r.permissions.removeAll(QStringLiteral("export_data"));
        }
    EXPECT_EQ(names(club.sorted()), QString("Yan Cal Dee Bob Amy Abe Zed").split(QLatin1Char(' ')));
}

TEST_F(ClubMembersTest, ListsTheStrongestRolesFirst) {
    ui::ClubMembersDialog* d = openMembers(Me::Admin, strongestRolesClub());
    ASSERT_NE(d, nullptr);
    QStringList shown;
    for (QWidget* w : d->findChildren<QWidget*>())
        if (w->objectName().startsWith(QLatin1String("memberRow:")))
            shown << w->findChild<QLabel*>(QStringLiteral("memberName"))->text();
    EXPECT_EQ(shown, QString("Yan Bob Amy Abe Dee Cal Zed").split(QLatin1Char(' ')));
}

TEST_F(ClubMembersTest, AMemberSeesOnlyWhatTheServerSendsAndNoManageLink) {
    ui::ClubMembersDialog* d = openMembers(Me::Member, membersJson(false));
    ASSERT_NE(d, nullptr);
    // Only their own last active came.
    EXPECT_TRUE(lastActive(d->row(QStringLiteral("ann")))->isHidden());
    EXPECT_TRUE(lastActive(d->row(QStringLiteral("cy")))->isHidden());
    EXPECT_FALSE(lastActive(d->row(QStringLiteral("ben")))->isHidden());
    EXPECT_EQ(chips(d->row(QStringLiteral("ben"))),
              (QStringList{ QStringLiteral("Member"), QStringLiteral("Track crew") }));
    // Their roles don't manage members.
    EXPECT_TRUE(d->manageLink()->isHidden());
}

TEST_F(ClubMembersTest, ARoleThatInvitesPeopleGetsTheManageLink) {
    ui::ClubMembersDialog* d = openMembers(Me::Inviter, membersJson(true));
    ASSERT_NE(d, nullptr);
    EXPECT_FALSE(d->manageLink()->isHidden());
}

TEST_F(ClubMembersTest, ARoleChangeShowsWithoutARestart) {
    ui::ClubMembersDialog* d = openMembers(Me::Admin, membersJson(true));
    ASSERT_NE(d, nullptr);
    const int before = requests(http_, kMembers);
    // On the web: Ben is given Admin, and my own roles stop managing members.
    http_.clear(kMembers);
    http_.reply(kMembers, 200, membersJson(true, true));
    http_.clear("/api/orgs");
    http_.reply("/api/orgs", 200, orgsJson(Me::Member));
    // The server's club hint (#181's access-changed push), as MainWindow passes it on.
    window_->hint({ { QStringLiteral("kind"), QStringLiteral("club") },
                    { QStringLiteral("id"), QStringLiteral("o1") } });
    library_.refresh();
    ASSERT_TRUE(
        waitFor([&] { return chips(d->row(QStringLiteral("ben"))).contains(QStringLiteral("Admin")); }));
    EXPECT_EQ(
        chips(d->row(QStringLiteral("ben"))),
        (QStringList{ QStringLiteral("Admin"), QStringLiteral("Member"), QStringLiteral("Track crew") }));
    ASSERT_TRUE(waitFor([&] { return d->manageLink()->isHidden(); }));
    EXPECT_EQ(window_->membersDialog(), d);
    // A burst of hints asks once.
    QCoreApplication::processEvents();
    EXPECT_LE(requests(http_, kMembers) - before, 2);
}

TEST_F(ClubMembersTest, LeavingTheClubEmptiesTheList) {
    ui::ClubMembersDialog* d = openMembers(Me::Member, membersJson(false));
    ASSERT_NE(d, nullptr);
    http_.clear("/api/orgs");
    http_.reply("/api/orgs", 200, { { QStringLiteral("orgs"), QJsonArray{} } });
    library_.refresh();
    ASSERT_TRUE(waitFor([&] { return d->row(QStringLiteral("cy")) == nullptr; }));
    EXPECT_FALSE(d->statusLine()->isHidden());
    EXPECT_TRUE(d->statusLine()->text().contains(QStringLiteral("no longer in this club")));
    EXPECT_TRUE(d->manageLink()->isHidden());
}

TEST_F(ClubMembersTest, AnOlderServerThatRefusesTheAppSaysSoKindly) {
    http_.reply("/api/orgs", 200, orgsJson(Me::Admin));
    http_.reply(kMembers, 403, { { QStringLiteral("error"), QStringLiteral("token_not_allowed") } });
    library_.setServer(http_.base(), QStringLiteral("Club server"), QStringLiteral("bld_pat_members"));
    ASSERT_TRUE(waitFor([&] { return library_.state() == ui::ServerLibrary::State::Ready; }));
    window_ = std::make_unique<ui::ServerWindow>(library_);
    window_->showMembers(QStringLiteral("train"));
    ui::ClubMembersDialog* d = window_->membersDialog();
    ASSERT_NE(d, nullptr);
    ASSERT_TRUE(waitFor([&] {
        return d->statusLine()->text().contains(QStringLiteral("doesn't list club members in the app yet"));
    }));
    EXPECT_FALSE(d->statusLine()->isHidden());
    // Signed in still: no sign-in prompt for this.
    EXPECT_EQ(library_.state(), ui::ServerLibrary::State::Ready);
    // The admin can still go to the website.
    EXPECT_FALSE(d->manageLink()->isHidden());
}
