#include "ConnectDialog.h"

#include "OwnerFilter.h"
#include "TokenStore.h"
#include "core/Version.h"
#include "ui/help/HelpButton.h"

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
#include <QTreeWidget>
#include <QVBoxLayout>

namespace bld::sync {

namespace {
const char* kAddressKey = "sync/serverAddress";
const char* kShowKey = "sync/ownerFilter";  // the Show filter, kept between runs

QString rememberedShow() { return QSettings().value(QLatin1String(kShowKey), kShowAll).toString(); }
enum Page { AddressPage, CodePage, LayoutsPage, PublishPage };
enum Column { TitleCol, OwnerCol, AccessCol, UpdatedCol };

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

    // Address
    auto* addressPage = new QWidget(pages_);
    auto* a = new QVBoxLayout(addressPage);
    a->addWidget(new QLabel(tr("Server address:"), addressPage));
    auto* row = new QHBoxLayout();
    address_ = new QLineEdit(addressPage);
    address_->setObjectName(QStringLiteral("serverAddress"));
    address_->setPlaceholderText(QStringLiteral("layouts.example.org"));
    address_->setText(QSettings().value(QLatin1String(kAddressKey)).toString());
    connectBtn_ = new QPushButton(tr("Connect"), addressPage);
    connectBtn_->setObjectName(QStringLiteral("connect"));
    connectBtn_->setDefault(true);
    row->addWidget(address_, 1);
    row->addWidget(connectBtn_);
    row->addWidget(new bld::ui::help::HelpButton(QStringLiteral("connect.server"), addressPage, address_));
    a->addLayout(row);
    a->addStretch(1);
    pages_->addWidget(addressPage);

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
    bottom->addStretch(1);
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
    pf->addRow(tr("Title"), publishTitle_);
    pf->addRow(tr("Save to"), bld::ui::help::withHelp(owner_, QStringLiteral("publish.owner"), publishPage));
    pf->addRow(QString(), publishBtn_);
    pages_->addWidget(publishPage);
    connect(publishBtn_, &QPushButton::clicked, this, &ConnectDialog::publishNow);
    if (purpose_ == Purpose::Publish) setWindowTitle(tr("Publish to Server"));

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
    connect(layouts_, &QTreeWidget::itemSelectionChanged, this,
            [this] { openBtn_->setEnabled(!layouts_->selectedItems().isEmpty()); });
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
    QSettings().setValue(QLatin1String(kAddressKey), address_->text().trimmed());
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

void ConnectDialog::onFailed(const QString& what, const QString& message, bool unauthorized) {
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

void ConnectDialog::showVenues(const QList<VenueEntry>& venues) {
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
    pages_->setCurrentIndex(LayoutsPage);
    showMessage(venues.isEmpty() ? tr("No saved venues on this server yet.") : QString());
}

void ConnectDialog::showLayouts(const QList<LayoutEntry>& layouts) {
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
        item->setData(UpdatedCol, Qt::UserRole, e.updatedAt);
    }
    setShowChoices(clubs);
    layouts_->setSortingEnabled(true);
    filterLayouts(filter_->text());
    pages_->setCurrentIndex(LayoutsPage);
    showMessage(layouts.isEmpty() ? tr("No layouts yet. Create one on the web, or publish one from here.")
                                  : QString());
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
