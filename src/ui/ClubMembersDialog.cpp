#include "ClubMembersDialog.h"

#include "ServerLibrary.h"

#include "ClubLogoViews.h"

#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace bld::ui {

namespace {

// The website's wording ("active this week"), with a capital to start the line.
QString lastActiveText(const QString& bucket) {
    if (bucket == QLatin1String("today")) return ClubMembersDialog::tr("Active today");
    if (bucket == QLatin1String("week")) return ClubMembersDialog::tr("Active this week");
    if (bucket == QLatin1String("month")) return ClubMembersDialog::tr("Active this month");
    if (bucket == QLatin1String("older")) return ClubMembersDialog::tr("Not active for a month or more");
    return {};
}

// The club's People page on the website, where members are managed.
QUrl peoplePage(const QUrl& server, const QString& slug) {
    QUrl url = server;
    url.setPath(QStringLiteral("/orgs/") + QString::fromLatin1(QUrl::toPercentEncoding(slug))
                    + QStringLiteral("/admin"),
                QUrl::StrictMode);
    url.setQuery(QStringLiteral("tab=people"));
    return sync::ServerApi::webLink(url.toString());
}

} // namespace

ClubMembersDialog::ClubMembersDialog(ServerLibrary& library, const QString& slug, QWidget* parent)
    : QWidget(parent, Qt::Window), library_(library), slug_(slug), server_(library.server()) {
    setObjectName(QStringLiteral("ClubMembersDialog"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(460, 560);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(16, 14, 16, 14);
    col->setSpacing(8);

    // The club: its logo and name, and how many are in it.
    auto* head = new QHBoxLayout();
    head->setSpacing(10);
    logo_ = sync::clubLogoLabel(server_, slug_, 40, this);
    head->addWidget(logo_, 0, Qt::AlignVCenter);
    auto* names = new QVBoxLayout();
    names->setSpacing(0);
    name_ = new QLabel(this);
    name_->setObjectName(QStringLiteral("clubName"));
    name_->setTextFormat(Qt::PlainText);
    QFont f = name_->font();
    f.setBold(true);
    if (f.pointSizeF() > 0) f.setPointSizeF(f.pointSizeF() * 1.25);
    name_->setFont(f);
    count_ = libraryMutedLabel(QString(), this);
    count_->setObjectName(QStringLiteral("memberCount"));
    names->addWidget(name_);
    names->addWidget(count_);
    head->addLayout(names, 1);
    col->addLayout(head);
    auto* intro = libraryMutedLabel(
        tr("Who is in the club and the roles they hold. Roles are given and changed on the website."), this);
    col->addWidget(intro);

    status_ = libraryMutedLabel(tr("Loading the members…"), this);
    status_->setObjectName(QStringLiteral("membersStatus"));
    col->addWidget(status_);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("membersList"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    host_ = new QWidget(scroll);
    list_ = new QVBoxLayout(host_);
    list_->setContentsMargins(0, 0, 0, 0);
    list_->setSpacing(4);
    list_->addStretch(1);
    scroll->setWidget(host_);
    col->addWidget(scroll, 1);

    auto* foot = new QHBoxLayout();
    manage_ = new QLabel(this);
    manage_->setObjectName(QStringLiteral("manageMembersLink"));
    manage_->setTextFormat(Qt::RichText);
    manage_->setText(
        QStringLiteral("<a href=\"manage\">%1</a>").arg(tr("Manage members on the website").toHtmlEscaped()));
    manage_->setToolTip(
        tr("Invite people, answer requests to join, and give or take roles on the club's People page"));
    manage_->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    manage_->setVisible(false);
    connect(manage_, &QLabel::linkActivated, this, [this] {
        const QUrl url = managePage();
        if (url.isValid() && openLink) openLink(url);
    });
    auto* closeButton = new QPushButton(tr("Close"), this);
    closeButton->setObjectName(QStringLiteral("closeMembers"));
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    foot->addWidget(manage_);
    foot->addStretch(1);
    foot->addWidget(closeButton);
    col->addLayout(foot);

    openLink = [](const QUrl& url) { QDesktopServices::openUrl(url); };
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, &QWidget::close);

    soon_ = new QTimer(this);
    soon_->setSingleShot(true);
    soon_->setInterval(300);
    connect(soon_, &QTimer::timeout, this, &ClubMembersDialog::reload);
    // The club list again (your roles changed, the club was renamed, you
    // left it): the header and the link follow it at once. The members
    // themselves are asked again on the club's hints (ServerWindow::hint).
    connect(&library_, &ServerLibrary::changed, this, [this] {
        updateHeader();
        if (!club() || !loaded_) reloadSoon();
    });
    connect(&library_, &ServerLibrary::stateChanged, this, [this] {
        // Another server, or signed out: this club's list is not shown any more.
        if (library_.server() != server_) close();
    });

    updateHeader();
    reload();
}

const sync::OrgEntry* ClubMembersDialog::club() const {
    const auto& orgs = library_.orgs();
    const auto it =
        std::find_if(orgs.begin(), orgs.end(), [&](const sync::OrgEntry& o) { return o.slug == slug_; });
    return it == orgs.end() ? nullptr : &*it;
}

QUrl ClubMembersDialog::managePage() const {
    const sync::OrgEntry* o = club();
    if (!o || !o->managesMembers()) return {};
    return peoplePage(server_, slug_);
}

void ClubMembersDialog::updateHeader() {
    const sync::OrgEntry* o = club();
    const QString name = o ? o->name : name_->text();
    name_->setText(name.isEmpty() ? slug_ : name);
    setWindowTitle(tr("Members of %1").arg(name_->text()));
    manage_->setVisible(managePage().isValid());
}

void ClubMembersDialog::reloadSoon() {
    soon_->start();
}

void ClubMembersDialog::reload() {
    soon_->stop();
    if (!club()) {
        // Not in the club any more (or the club is gone).
        ++generation_;
        members_ = {};
        loaded_ = false;
        status_->setText(tr("You're no longer in this club, so its members aren't shown."));
        status_->setVisible(true);
        rebuild();
        return;
    }
    const int gen = ++generation_;
    ++loads_;
    if (!loaded_) {
        status_->setText(tr("Loading the members…"));
        status_->setVisible(true);
    }
    library_.api().clubMembers(
        slug_,
        [this, gen](const sync::ClubMembers& m) {
            if (gen != generation_) return;
            members_ = m;
            loaded_ = true;
            status_->setVisible(members_.members.isEmpty());
            status_->setText(tr("Nobody is in this club yet."));
            rebuild();
        },
        [this, gen](const sync::ServerRefusal& r) {
            if (gen != generation_) return;
            if (r.status == 401) {
                library_.refresh();
                return;
            }
            members_ = {};
            loaded_ = false;
            // A server from before the app could list members refuses its sign-in here.
            if (r.code == QLatin1String("token_not_allowed")) {
                status_->setText(tr("This server doesn't list club members in the app yet. You can see them "
                                    "on the website."));
            } else {
                status_->setText(tr("The members couldn't be loaded. %1").arg(ServerLibrary::refusalText(r)));
            }
            status_->setVisible(true);
            rebuild();
        });
}

QWidget* ClubMembersDialog::row(const QString& userId) const {
    return host_->findChild<QWidget*>(QStringLiteral("memberRow:") + userId);
}

void ClubMembersDialog::rebuild() {
    // Everything but the stretch at the end.
    while (list_->count() > 1) {
        QLayoutItem* item = list_->takeAt(0);
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
    const auto n = members_.members.size();
    count_->setText(!loaded_ ? QString() : n == 1 ? tr("1 member") : tr("%1 members").arg(n));
    QList<sync::ClubMember> sorted = members_.members;
    std::stable_sort(sorted.begin(), sorted.end(), [](const sync::ClubMember& a, const sync::ClubMember& b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    int at = 0;
    for (const sync::ClubMember& m : sorted) {
        QFrame* frame = libraryRowFrame(host_);
        frame->setObjectName(QStringLiteral("memberRow:") + m.userId);
        auto* text = new QVBoxLayout(frame);
        text->setContentsMargins(10, 8, 10, 8);
        text->setSpacing(4);
        auto* name = new QLabel(m.name, frame);
        name->setObjectName(QStringLiteral("memberName"));
        name->setTextFormat(Qt::PlainText);
        name->setProperty("rowTitle", true);
        name->setWordWrap(true);
        text->addWidget(name);
        // The roles they hold, as the website's chips.
        auto* roles = new QHBoxLayout();
        roles->setSpacing(4);
        for (const QString& role : members_.roleNames(m)) {
            auto* chip = new QLabel(role, frame);
            chip->setObjectName(QStringLiteral("memberRole"));
            chip->setTextFormat(Qt::PlainText);
            chip->setProperty("pill", true);
            roles->addWidget(chip);
        }
        roles->addStretch(1);
        text->addLayout(roles);
        const QString since =
            m.joinedAt.isValid()
                ? tr("Joined %1")
                      .arg(QLocale().toString(m.joinedAt.toLocalTime().date(), QLocale::ShortFormat))
                : QString();
        auto* joined = libraryMutedLabel(since, frame);
        joined->setObjectName(QStringLiteral("memberJoined"));
        joined->setVisible(!since.isEmpty());
        // Only what the server sent: nothing for someone you may not see it for.
        auto* last = libraryMutedLabel(lastActiveText(m.lastActive), frame);
        last->setObjectName(QStringLiteral("lastActive"));
        last->setVisible(!m.lastActive.isEmpty());
        auto* info = new QHBoxLayout();
        info->setSpacing(12);
        info->addWidget(joined);
        info->addWidget(last);
        info->addStretch(1);
        text->addLayout(info);
        list_->insertWidget(at++, frame);
    }
}

} // namespace bld::ui
