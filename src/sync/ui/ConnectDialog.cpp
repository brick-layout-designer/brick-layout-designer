#include "ConnectDialog.h"

#include "OwnerFilter.h"
#include "RefreshOnFocus.h"
#include "ServerList.h"
#include "ServersDialog.h"
#include "TokenStore.h"
#include "core/Version.h"
#include "ui/ConfirmDialog.h"
#include "ui/help/HelpButton.h"
#include "ui/help/SourceLinks.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <utility>

namespace bld::sync {

namespace {
const char* kShowKey = "sync/ownerFilter";  // the Show filter, kept between runs

QString rememberedShow() { return QSettings().value(QLatin1String(kShowKey), kShowAll).toString(); }
enum Page { AddressPage, CodePage, LayoutsPage, PublishPage };
enum Column { TitleCol, OwnerCol, AccessCol, UpdatedCol };
// On the server list: the server's address, and a recent layout's id.
constexpr int kServerRole = Qt::UserRole;
constexpr int kLayoutRole = Qt::UserRole + 1;

// Sorts the Updated column by date, not by its text.
class LayoutItem : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem& other) const override {
        const int c = treeWidget() ? treeWidget()->sortColumn() : TitleCol;
        if (c == UpdatedCol)
            return data(UpdatedCol, Qt::UserRole).toDateTime()
                   < other.data(UpdatedCol, Qt::UserRole).toDateTime();
        return text(c).localeAwareCompare(other.text(c)) < 0;
    }
};
} // namespace

ConnectDialog::ConnectDialog(ServerApi& api, TokenStore& tokens, std::function<void(const QUrl&)> openUrl,
                             QWidget* parent, Purpose purpose)
    : QDialog(parent), api_(api), tokens_(tokens), openUrl_(std::move(openUrl)), purpose_(purpose) {
    const bool venues = purpose_ == Purpose::DownloadVenues;
    setWindowTitle(venues ? tr("Download Venues from Server") : tr("Connect to Server"));
    resize(560, 420);
    auto* col = new QVBoxLayout(this);
    pages_ = new QStackedWidget(this);
    col->addWidget(pages_, 1);
    // A quiet line at the foot: where the app's code lives.
    col->addWidget(bld::ui::help::makeSourceLinksLabel(this), 0, Qt::AlignRight);

    // Your servers (with each one's recent layouts), or a new address
    auto* addressPage = new QWidget(pages_);
    auto* a = new QVBoxLayout(addressPage);
    auto* head = new QHBoxLayout();
    auto* yours = new QLabel(tr("Your servers"), addressPage);
    QFont bold = yours->font();
    bold.setBold(true);
    yours->setFont(bold);
    auto* manage = new QPushButton(tr("Manage Servers..."), addressPage);
    manage->setObjectName(QStringLiteral("manageServers"));
    manage->setToolTip(tr("Add, rename or remove servers, sign in or out, and pick your Main one"));
    head->addWidget(yours);
    head->addStretch(1);
    head->addWidget(manage);
    a->addLayout(head);
    servers_ = new QTreeWidget(addressPage);
    servers_->setObjectName(QStringLiteral("servers"));
    servers_->setHeaderHidden(true);
    servers_->setColumnCount(2);
    servers_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    servers_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    servers_->header()->setStretchLastSection(false);
    head->insertWidget(1, new bld::ui::help::HelpButton(QStringLiteral("servers.pick"), addressPage, servers_));
    a->addWidget(servers_, 1);
    a->addWidget(new QLabel(tr("Or connect to a new server by its web address:"), addressPage));
    auto* row = new QHBoxLayout();
    address_ = new QLineEdit(addressPage);
    address_->setObjectName(QStringLiteral("serverAddress"));
    address_->setPlaceholderText(QStringLiteral("layouts.example.org"));
    connectBtn_ = new QPushButton(tr("Connect"), addressPage);
    connectBtn_->setObjectName(QStringLiteral("connect"));
    connectBtn_->setDefault(true);
    row->addWidget(address_, 1);
    row->addWidget(connectBtn_);
    row->addWidget(new bld::ui::help::HelpButton(QStringLiteral("connect.server"), addressPage, address_));
    a->addLayout(row);
    pages_->addWidget(addressPage);
    manageServers_ = [this] {
        ServersDialog servers(tokens_, openUrl_, this);
        servers.exec();
    };
    // A nested dialog's event loop must not run inside the click's signal.
    connect(manage, &QPushButton::clicked, this, [this] {
        QTimer::singleShot(0, this, [this] {
            if (manageServers_) manageServers_();
            refreshServers();
        });
    });
    connect(servers_, &QTreeWidget::itemSelectionChanged, this, &ConnectDialog::onServerPicked);
    connect(servers_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        const QUrl url = item->data(0, kServerRole).toUrl();
        const QString layout = item->data(0, kLayoutRole).toString();
        if (layout.isEmpty()) connectToServer();
        else openRecent(url, layout);
    });
    refreshServers();

    // Sign-in code
    auto* codePage = new QWidget(pages_);
    auto* c = new QVBoxLayout(codePage);
    codeHint_ = new QLabel(codePage);
    codeHint_->setWordWrap(true);
    codeHint_->setOpenExternalLinks(true);
    code_ = new QLabel(codePage);
    code_->setObjectName(QStringLiteral("userCode"));
    code_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont big = code_->font();
    big.setPointSizeF(big.pointSizeF() * 2.2);
    big.setBold(true);
    code_->setFont(big);
    code_->setAlignment(Qt::AlignCenter);
    c->addStretch(1);
    c->addWidget(codeHint_);
    c->addWidget(code_);
    c->addWidget(new QLabel(tr("Waiting for you to approve it in the browser…"), codePage));
    c->addStretch(1);
    pages_->addWidget(codePage);

    // Layouts
    auto* layoutsPage = new QWidget(pages_);
    auto* l = new QVBoxLayout(layoutsPage);
    filter_ = new QLineEdit(layoutsPage);
    filter_->setPlaceholderText(tr("Filter layouts"));
    filter_->setClearButtonEnabled(true);
    layouts_ = new QTreeWidget(layoutsPage);
    layouts_->setObjectName(QStringLiteral("layouts"));
    layouts_->setRootIsDecorated(false);
    layouts_->setHeaderLabels({ tr("Layout"), tr("Owner"), tr("Access"), tr("Updated") });
    layouts_->header()->setSectionResizeMode(TitleCol, QHeaderView::Stretch);
    layouts_->setSortingEnabled(true);
    layouts_->sortByColumn(UpdatedCol, Qt::DescendingOrder);
    if (venues) {
        filter_->setPlaceholderText(tr("Filter venues"));
        layouts_->setHeaderLabels({ tr("Venue"), tr("Owner") });
        layouts_->setSelectionMode(QAbstractItemView::ExtendedSelection);
        layouts_->sortByColumn(TitleCol, Qt::AscendingOrder);
    }
    auto* bottom = new QHBoxLayout();
    auto* signOut = new QPushButton(tr("Sign Out"), layoutsPage);
    openBtn_ = new QPushButton(venues ? tr("Download") : tr("Open"), layoutsPage);
    openBtn_->setObjectName(QStringLiteral("open"));
    openBtn_->setEnabled(false);
    bottom->addWidget(signOut);
    // Clubs that let people find them are listed on the server's Clubs page.
    auto* findClub = new QPushButton(tr("Find a Club on the Web..."), layoutsPage);
    findClub->setObjectName(QStringLiteral("findClub"));
    findClub->setToolTip(tr("Open this server's Clubs page in your browser, to find a club and join it"));
    bottom->addWidget(bld::ui::help::withHelp(findClub, QStringLiteral("club.find"), layoutsPage));
    connect(findClub, &QPushButton::clicked, this, [this] {
        QUrl url = server_;
        url.setPath(QStringLiteral("/orgs"));
        url.setQuery(QString());
        url.setFragment(QStringLiteral("find"));
        openUrl_(url);
    });
    bottom->addStretch(1);
    deleteBtn_ = new QPushButton(tr("Delete…"), layoutsPage);
    deleteBtn_->setObjectName(QStringLiteral("deleteItem"));
    deleteBtn_->setToolTip(venues ? tr("Delete the picked venue from the server") : tr("Delete the picked layout from the server (you own it)"));
    deleteBtn_->setEnabled(false);
    bottom->addWidget(deleteBtn_);
    bottom->addWidget(openBtn_);
    show_ = new QComboBox(layoutsPage);
    show_->setObjectName(QStringLiteral("ownerFilter"));
    show_->setToolTip(tr("Show everything, only yours, or one club's"));
    setShowChoices({});
    auto* filters = new QHBoxLayout();
    filters->addWidget(filter_, 1);
    filters->addWidget(new QLabel(tr("Show:"), layoutsPage));
    filters->addWidget(bld::ui::help::withHelp(show_, QStringLiteral("owners.filter"), layoutsPage));
    l->addLayout(filters);
    l->addWidget(layouts_, 1);
    l->addLayout(bottom);
    pages_->addWidget(layoutsPage);

    // Publish: title and where it's saved
    auto* publishPage = new QWidget(pages_);
    auto* pf = new QFormLayout(publishPage);
    publishTitle_ = new QLineEdit(publishPage);
    publishTitle_->setObjectName(QStringLiteral("publishTitle"));
    owner_ = new QComboBox(publishPage);
    owner_->setObjectName(QStringLiteral("publishOwner"));
    publishBtn_ = new QPushButton(tr("Publish"), publishPage);
    publishBtn_->setObjectName(QStringLiteral("publish"));
    publishServer_ = new QLabel(publishPage);
    publishServer_->setObjectName(QStringLiteral("publishServer"));
    pf->addRow(tr("Server"), publishServer_);
    pf->addRow(tr("Title"), publishTitle_);
    pf->addRow(tr("Save to"), bld::ui::help::withHelp(owner_, QStringLiteral("publish.owner"), publishPage));
    pf->addRow(QString(), publishBtn_);
    pages_->addWidget(publishPage);
    connect(publishBtn_, &QPushButton::clicked, this, &ConnectDialog::publishNow);
    if (purpose_ == Purpose::Publish) setWindowTitle(tr("Publish to Server"));
    if (purpose_ == Purpose::SignIn) setWindowTitle(tr("Sign In to a Server"));

    message_ = new QLabel(this);
    message_->setObjectName(QStringLiteral("message"));
    message_->setWordWrap(true);
    col->addWidget(message_);
    updateBtn_ = new QPushButton(tr("Download the New Version"), this);
    updateBtn_->setObjectName(QStringLiteral("downloadUpdate"));
    updateBtn_->setProperty("accent", true);
    updateBtn_->hide();
    col->addWidget(updateBtn_, 0, Qt::AlignLeft);
    connect(updateBtn_, &QPushButton::clicked, this, [this] { openUrl_(downloadUrl_); });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    col->addWidget(buttons);

    connect(connectBtn_, &QPushButton::clicked, this, &ConnectDialog::connectToServer);
    connect(address_, &QLineEdit::returnPressed, this, &ConnectDialog::connectToServer);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(signOut, &QPushButton::clicked, this, &ConnectDialog::signOut);
    connect(openBtn_, &QPushButton::clicked, this, &ConnectDialog::openSelected);
    if (!venues) connect(layouts_, &QTreeWidget::itemActivated, this, &ConnectDialog::openSelected);
    connect(layouts_, &QTreeWidget::itemSelectionChanged, this, [this] {
        openBtn_->setEnabled(!layouts_->selectedItems().isEmpty());
        updateDeleteButton();
    });
    connect(deleteBtn_, &QPushButton::clicked, this, &ConnectDialog::deleteSelected);
    confirmDelete_ = [this](const QString& name, bool layout) {
        return bld::ui::ConfirmDialog::confirmDelete(this, name, layout ? bld::ui::ConfirmDialog::layoutWording()
                                                                        : bld::ui::ConfirmDialog::venueWording());
    };
    connect(&api_, &ServerApi::deleted, this, [this] {
        afterList_ = tr("Deleted “%1”").arg(deleting_);
        showMessage(afterList_);
        deleting_.clear();
        refreshList();
    });
    connect(filter_, &QLineEdit::textChanged, this, &ConnectDialog::filterLayouts);
    connect(show_, &QComboBox::activated, this, [this] {
        QSettings().setValue(QLatin1String(kShowKey), show_->currentData().toString());
        filterLayouts(filter_->text());
    });

    connect(&api_, &ServerApi::versionReady, this, &ConnectDialog::onVersion);
    connect(&api_, &ServerApi::requestFailed, this, &ConnectDialog::onFailed);
    connect(&api_, &ServerApi::layoutsReady, this, &ConnectDialog::showLayouts);
    connect(&api_, &ServerApi::venuesReady, this, &ConnectDialog::showVenues);
    connect(&api_, &ServerApi::orgsReady, this, &ConnectDialog::showOrgs);
    // Back from the web (where a layout or venue may have just been added): list again.
    new RefreshOnFocus(this, [this] { refreshList(); });
    connect(&api_, &ServerApi::published, this, [this](const QString& id, const QString& title) {
        if (purpose_ != Purpose::Publish) return;
        result_ = ConnectResult{ server_, token_, id, title, false, info_ };
        accept();
    });
    connect(&api_, &ServerApi::venueReady, this,
            [this](const QString&, const QString& name, const QByteArray& file) {
                if (venuesPending_ <= 0) return;
                venues_ << DownloadedVenue{ name, file };
                if (--venuesPending_ == 0) accept();
            });
    connect(&api_, &ServerApi::signInCode, this, [this](const DeviceCode& dc) {
        codeHint_->setText(tr("Your browser should open the sign-in page. If it doesn't, go to <a "
                              "href=\"%1\">%1</a> and enter this code:")
                               .arg(dc.verificationUri.toString().toHtmlEscaped()));
        code_->setText(dc.userCode);
        pages_->setCurrentIndex(CodePage);
        showMessage({});
        openUrl_(dc.verificationUriComplete.isValid() ? dc.verificationUriComplete : dc.verificationUri);
    });
    connect(&api_, &ServerApi::signedIn, this, [this](const QString& token) {
        tokens_.save(server_, token);
        haveToken(token);
    });
    connect(&api_, &ServerApi::signInFailed, this, [this](const QString& reason) {
        pages_->setCurrentIndex(AddressPage);
        connectBtn_->setEnabled(true);
        if (reason == QLatin1String("access_denied")) showMessage(tr("Sign-in was declined in the browser."));
        else if (reason == QLatin1String("expired_token"))
            showMessage(tr("The sign-in code expired. Connect again for a new one."));
        else showMessage(tr("Could not sign in: %1").arg(reason));
    });
}

void ConnectDialog::setAddress(const QString& address) {
    address_->setText(address);
}

void ConnectDialog::refreshServers() {
    const QSignalBlocker block(servers_);
    servers_->clear();
    const ServerList list = ServerList::load();
    const ServerEntry* last = list.lastUsed();
    QTreeWidgetItem* current = nullptr;
    for (const ServerEntry& e : list.servers()) {
        auto* item = new QTreeWidgetItem(servers_);
        item->setText(0, e.main ? tr("%1 (Main)").arg(e.label()) : e.label());
        item->setText(1, e.address());
        item->setToolTip(0, e.url.toString());
        item->setData(0, kServerRole, e.url);
        QFont f = item->font(0);
        f.setBold(true);
        item->setFont(0, f);
        // Recent layouts only where opening one makes sense.
        if (purpose_ == Purpose::OpenLayout)
            for (const RecentLayout& r : e.recent) {
                auto* child = new QTreeWidgetItem(item);
                child->setText(0, r.title.isEmpty() ? tr("Untitled Layout") : r.title);
                child->setText(1, r.readOnly ? tr("View only") : QString());
                child->setToolTip(0, tr("Open \"%1\" from %2").arg(child->text(0), e.label()));
                child->setData(0, kServerRole, e.url);
                child->setData(0, kLayoutRole, r.id);
            }
        item->setExpanded(true);
        if (last && e.key() == last->key()) current = item;
    }
    if (list.isEmpty()) {
        auto* none = new QTreeWidgetItem(servers_);
        none->setText(0, tr("No servers yet. Type one's web address below."));
        none->setFlags(Qt::NoItemFlags);
    }
    if (current) {
        servers_->setCurrentItem(current);
        address_->setText(current->data(0, kServerRole).toUrl().toString());
    }
}

void ConnectDialog::onServerPicked() {
    const auto items = servers_->selectedItems();
    if (items.isEmpty()) return;
    const QUrl url = items.first()->data(0, kServerRole).toUrl();
    if (url.isValid()) address_->setText(url.toString());
}

void ConnectDialog::openRecent(const QUrl& server, const QString& layoutId) {
    address_->setText(server.toString());
    pendingLayout_ = layoutId;
    connectToServer();
}

void ConnectDialog::showMessage(const QString& text) {
    message_->setText(text);
    updateBtn_->hide();
}

void ConnectDialog::showUpdateNeeded(const QString& text, const QString& downloadUrl) {
    message_->setText(text);
    downloadUrl_ = QUrl(downloadUrl.isEmpty() ? core::desktopDownloadUrl() : downloadUrl);
    updateBtn_->show();
    connectBtn_->setEnabled(true);
}

void ConnectDialog::connectToServer() {
    QString error;
    const auto base = ServerApi::normalizeBase(address_->text(), &error);
    if (!base) {
        showMessage(error);
        return;
    }
    server_ = *base;
    api_.setBase(server_);
    connectBtn_->setEnabled(false);
    showMessage(tr("Connecting to %1…").arg(server_.host()));
    api_.fetchVersion();
}

void ConnectDialog::onVersion(const ServerInfo& info) {
    info_ = info;
    const QString mine = QCoreApplication::applicationVersion();
    // Too old for this server: stop here, before signing in or syncing anything.
    if (info.standing(mine) == Standing::UpdateRequired) {
        showUpdateNeeded(tr("This server needs Brick Layout Designer %1 or newer, and you have %2. "
                            "Download the new version, install it, then connect again.")
                             .arg(info.desktopMinimum, mine),
                         info.downloadUrl);
        return;
    }
    if (!info.compatible()) {
        showUpdateNeeded(tr("This server keeps its layouts in a newer form than this version of Brick Layout "
                            "Designer can read. Download the new version, then connect again."),
                         info.downloadUrl);
        return;
    }
    // A server without what this window is for.
    if (purpose_ == Purpose::DownloadVenues && !info.has(QStringLiteral("venues"))) {
        connectBtn_->setEnabled(true);
        showMessage(tr("This server doesn't have a venue library yet."));
        return;
    }
    if (purpose_ == Purpose::Publish && !info.has(QStringLiteral("publish"))) {
        connectBtn_->setEnabled(true);
        showMessage(tr("This server can't take layouts from the desktop app yet."));
        return;
    }
    // One of your servers from now on (the first one is Main), with what it said.
    ServerList list = ServerList::load();
    list.touch(server_);
    list.remember(server_, info);
    list.save();
    // Signing in (File › Servers…): always a new sign-in, whatever is saved.
    if (purpose_ == Purpose::SignIn) {
        api_.startSignIn(tr("Brick Layout Designer %1").arg(QCoreApplication::applicationVersion()).trimmed());
        return;
    }
    tokens_.load(server_, [this](const QString& token) {
        if (token.isEmpty()) {
            api_.startSignIn(
                tr("Brick Layout Designer %1").arg(QCoreApplication::applicationVersion()).trimmed());
            return;
        }
        haveToken(token);
    });
}

void ConnectDialog::haveToken(const QString& token) {
    token_ = token;
    api_.setToken(token);
    if (purpose_ == Purpose::SignIn) {
        result_ = ConnectResult{ server_, token_, QString(), QString(), false, info_ };
        accept();
        return;
    }
    if (purpose_ == Purpose::Publish) {
        showMessage(tr("Loading your clubs…"));
        api_.fetchOrgs();
        return;
    }
    if (purpose_ == Purpose::DownloadVenues) {
        showMessage(tr("Loading venues…"));
        api_.fetchVenues();
        return;
    }
    showMessage(tr("Loading layouts…"));
    api_.fetchLayouts();
}

void ConnectDialog::signInAgain() {
    tokens_.remove(server_);
    api_.startSignIn(tr("Brick Layout Designer %1").arg(QCoreApplication::applicationVersion()).trimmed());
}

void ConnectDialog::updateDeleteButton() {
    const auto items = layouts_->selectedItems();
    const bool venues = purpose_ == Purpose::DownloadVenues;
    deleteBtn_->setEnabled(deleting_.isEmpty() && items.size() == 1
                           && (venues || items.first()->data(AccessCol, Qt::UserRole + 1).toBool()));
}

void ConnectDialog::deleteSelected() {
    const auto items = layouts_->selectedItems();
    if (items.size() != 1 || !deleting_.isEmpty()) return;
    const bool layout = purpose_ != Purpose::DownloadVenues;
    const QString id = items.first()->data(TitleCol, Qt::UserRole).toString();
    const QString name = items.first()->text(TitleCol);
    if (!confirmDelete_ || !confirmDelete_(name, layout)) return;
    deleting_ = name;
    updateDeleteButton();
    showMessage(tr("Deleting “%1”…").arg(name));
    if (layout) api_.deleteLayout(id);
    else api_.deleteVenue(id);
}

void ConnectDialog::onFailed(const QString& what, const QString& message, bool unauthorized) {
    if (what == QLatin1String("delete")) {
        deleting_.clear();
        updateDeleteButton();
        showMessage(tr("Could not delete it: %1").arg(message));
        return;
    }
    if (what == QLatin1String("publish")) {
        publishBtn_->setEnabled(true);
        if (unauthorized) {
            // Signed in before this app could publish: sign in again.
            signInAgain();
            return;
        }
        showMessage(tr("Could not publish: %1").arg(message));
        return;
    }
    const bool list =
        what == QLatin1String("layouts") || what == QLatin1String("venues") || what == QLatin1String("orgs");
    if (list && unauthorized) {
        // Revoked or expired, or (venues) signed in before this app could
        // ask for the venue library: sign in again.
        signInAgain();
        return;
    }
    if (what == QLatin1String("venue")) {
        // A download failed: stay on the list.
        venuesPending_ = 0;
        venues_.clear();
        openBtn_->setEnabled(true);
        showMessage(tr("Could not download the venue: %1").arg(message));
        return;
    }
    if (what != QLatin1String("version") && !list) return;
    pendingLayout_.clear();
    connectBtn_->setEnabled(true);
    pages_->setCurrentIndex(AddressPage);
    showMessage(what == QLatin1String("version")  ? tr("Could not reach %1: %2").arg(server_.host(), message)
                : what == QLatin1String("venues") ? tr("Could not load the venues: %1").arg(message)
                : what == QLatin1String("orgs")   ? tr("Could not load your clubs: %1").arg(message)
                                                  : tr("Could not load the layouts: %1").arg(message));
}

void ConnectDialog::setPublishContent(const QByteArray& bbm, const QByteArray& sidecarJson,
                                      const QString& title) {
    publishBbm_ = bbm;
    publishSidecar_ = sidecarJson;
    publishTitle_->setText(title);
}

void ConnectDialog::showOrgs(const QList<OrgEntry>& orgs) {
    owner_->clear();
    owner_->addItem(tr("Me"), QString());
    for (const auto& o : orgs) owner_->addItem(o.name, o.slug);
    // Start at the club the lists were last showing, else Me.
    const int shown = owner_->findData(rememberedShow());
    owner_->setCurrentIndex(shown > 0 ? shown : 0);
    const ServerList list = ServerList::load();
    const ServerEntry* entry = list.find(server_);
    publishServer_->setText(entry ? entry->label() : server_.host());
    publishBtn_->setEnabled(true);
    pages_->setCurrentIndex(PublishPage);
    showMessage({});
}

void ConnectDialog::publishNow() {
    if (publishBbm_.isEmpty()) return;
    const QString title = publishTitle_->text().trimmed();
    publishBtn_->setEnabled(false);
    showMessage(tr("Publishing…"));
    api_.publishLayout(title.isEmpty() ? tr("Untitled Layout") : title, publishBbm_, publishSidecar_,
                       owner_->currentData().toString());
}

void ConnectDialog::refreshList() {
    if (token_.isEmpty() || pages_->currentIndex() != LayoutsPage) return;
    if (purpose_ == Purpose::DownloadVenues) api_.fetchVenues();
    else if (purpose_ == Purpose::OpenLayout) api_.fetchLayouts();
}

QStringList ConnectDialog::pickedIds() const {
    QStringList ids;
    for (const auto* item : layouts_->selectedItems()) ids << item->data(TitleCol, Qt::UserRole).toString();
    return ids;
}

void ConnectDialog::pickAgain(const QStringList& ids) {
    if (ids.isEmpty()) return;
    for (int i = 0; i < layouts_->topLevelItemCount(); ++i) {
        auto* item = layouts_->topLevelItem(i);
        if (!ids.contains(item->data(TitleCol, Qt::UserRole).toString())) continue;
        item->setSelected(true);
        if (!layouts_->currentItem() || !layouts_->currentItem()->isSelected()) layouts_->setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
    }
}

void ConnectDialog::showVenues(const QList<VenueEntry>& venues) {
    const QStringList keep = pickedIds();
    layouts_->setSortingEnabled(false);
    layouts_->clear();
    QList<std::pair<QString, QString>> clubs;
    for (const auto& v : venues) {
        auto* item = new LayoutItem(layouts_);
        const ItemOwner owner = itemOwner(v.ownerOrgSlug, v.ownerOrgId, v.ownerOrgName);
        item->setText(TitleCol, v.name);
        item->setText(OwnerCol, owner.label);
        item->setData(OwnerCol, Qt::UserRole, owner.key);
        item->setData(TitleCol, Qt::UserRole, v.id);
        if (owner.key != kShowMine) clubs.append({ owner.key, owner.label });
    }
    setShowChoices(clubs);
    layouts_->setSortingEnabled(true);
    filterLayouts(filter_->text());
    pickAgain(keep);
    pages_->setCurrentIndex(LayoutsPage);
    showMessage(venues.isEmpty() ? tr("No saved venues on this server yet.") : std::exchange(afterList_, QString()));
}

void ConnectDialog::showLayouts(const QList<LayoutEntry>& layouts) {
    const QStringList keep = pickedIds();
    layouts_->setSortingEnabled(false);
    layouts_->clear();
    QList<std::pair<QString, QString>> clubs;
    for (const auto& e : layouts) {
        auto* item = new LayoutItem(layouts_);
        const ItemOwner owner = itemOwner(e.ownerOrgSlug, QString(), e.ownerOrgName);
        item->setText(TitleCol, e.title);
        item->setText(OwnerCol, owner.label);
        item->setData(OwnerCol, Qt::UserRole, owner.key);
        if (owner.key != kShowMine) clubs.append({ owner.key, owner.label });
        item->setText(AccessCol, e.role == QLatin1String("viewer")  ? tr("View only")
                                 : e.role == QLatin1String("owner") ? tr("Owner")
                                                                    : tr("Edit"));
        item->setText(UpdatedCol, QLocale().toString(e.updatedAt.toLocalTime(), QLocale::ShortFormat));
        item->setData(TitleCol, Qt::UserRole, e.id);
        item->setData(AccessCol, Qt::UserRole, e.role == QLatin1String("viewer"));
        item->setData(AccessCol, Qt::UserRole + 1, e.role == QLatin1String("owner"));
        item->setData(UpdatedCol, Qt::UserRole, e.updatedAt);
    }
    setShowChoices(clubs);
    layouts_->setSortingEnabled(true);
    filterLayouts(filter_->text());
    pickAgain(keep);
    pages_->setCurrentIndex(LayoutsPage);
    showMessage(layouts.isEmpty() ? tr("No layouts yet. Create one on the web, or publish one from here.")
                                  : std::exchange(afterList_, QString()));
    // A recent layout picked on the server list: open it, if it's still there.
    const QString pending = std::exchange(pendingLayout_, QString());
    if (pending.isEmpty()) return;
    for (int i = 0; i < layouts_->topLevelItemCount(); ++i) {
        auto* item = layouts_->topLevelItem(i);
        if (item->data(TitleCol, Qt::UserRole).toString() != pending) continue;
        layouts_->setCurrentItem(item);
        openSelected();
        return;
    }
    showMessage(tr("That layout isn't on this server any more, or you can't open it now."));
}

void ConnectDialog::filterLayouts(const QString& text) {
    const QString show = show_->currentData().toString();
    for (int i = 0; i < layouts_->topLevelItemCount(); ++i) {
        auto* item = layouts_->topLevelItem(i);
        const bool textOk = text.isEmpty() || item->text(TitleCol).contains(text, Qt::CaseInsensitive)
                            || item->text(OwnerCol).contains(text, Qt::CaseInsensitive);
        item->setHidden(!textOk || !ownerMatches(show, item->data(OwnerCol, Qt::UserRole).toString()));
    }
}

void ConnectDialog::setShowChoices(const QList<std::pair<QString, QString>>& clubs) {
    const QSignalBlocker block(show_);
    show_->clear();
    show_->addItem(tr("All"), kShowAll);
    show_->addItem(tr("Mine"), kShowMine);
    for (const auto& [key, name] : clubs)
        if (show_->findData(key) < 0) show_->addItem(name, key);
    // The last choice, while that club is still in the list; else All.
    const int at = show_->findData(rememberedShow());
    show_->setCurrentIndex(at >= 0 ? at : 0);
}

void ConnectDialog::openSelected() {
    const auto items = layouts_->selectedItems();
    if (items.isEmpty()) return;
    if (purpose_ == Purpose::DownloadVenues) {
        if (venuesPending_ > 0) return;
        venues_.clear();
        venuesPending_ = static_cast<int>(items.size());
        openBtn_->setEnabled(false);
        showMessage(tr("Downloading %n venue(s)…", nullptr, venuesPending_));
        for (const auto* item : items) api_.fetchVenue(item->data(TitleCol, Qt::UserRole).toString());
        return;
    }
    const auto* item = items.first();
    result_ = ConnectResult{ server_, token_, item->data(TitleCol, Qt::UserRole).toString(),
                             item->text(TitleCol), item->data(AccessCol, Qt::UserRole).toBool(), info_ };
    accept();
}

void ConnectDialog::signOut() {
    tokens_.remove(server_);
    token_.clear();
    api_.setToken({});
    layouts_->clear();
    pages_->setCurrentIndex(AddressPage);
    connectBtn_->setEnabled(true);
    showMessage(tr("Signed out of %1.").arg(server_.host()));
}

} // namespace bld::sync
