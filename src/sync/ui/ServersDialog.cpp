#include "ServersDialog.h"
#include "ui/ConfirmDialog.h"
#include "RefreshOnFocus.h"

#include "ConnectDialog.h"
#include "ServerApi.h"
#include "TokenStore.h"
#include "ui/help/HelpButton.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace bld::sync {

namespace {
constexpr int kUrlRole = Qt::UserRole;

QLabel* mutedLabel(QWidget* parent, const char* name) {
    auto* l = new QLabel(parent);
    l->setObjectName(QLatin1String(name));
    // Readable on the picked row too: smaller, not greyed.
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * 0.92);
    l->setFont(f);
    return l;
}
}  // namespace

ServersDialog::ServersDialog(TokenStore& tokens, std::function<void(const QUrl&)> openUrl, QWidget* parent)
    : QDialog(parent), tokens_(tokens), openUrl_(std::move(openUrl)) {
    setWindowTitle(tr("Servers"));
    setObjectName(QStringLiteral("serversDialog"));
    resize(560, 440);
    auto* col = new QVBoxLayout(this);
    col->addWidget(bld::ui::help::headingWithHelp(tr("Your servers"), QStringLiteral("servers.list"), this));
    auto* intro = new QLabel(tr("The places your shared layouts live: your club's server, a friend's, and so on. "
                                "Your settings follow your account on the Main one."),
                             this);
    intro->setWordWrap(true);
    col->addWidget(intro);

    rows_ = new QListWidget(this);
    rows_->setObjectName(QStringLiteral("serverRows"));
    rows_->setSelectionMode(QAbstractItemView::SingleSelection);
    rows_->setSpacing(2);
    // The picked server gets an outline, not a solid fill its text can't be read on.
    rows_->setStyleSheet(QStringLiteral(
        "QListWidget#serverRows::item:selected { background: palette(alternate-base); color: palette(text);"
        " border: 2px solid palette(highlight); border-radius: 10px; }"));
    col->addWidget(rows_, 1);
    empty_ = new QLabel(tr("No servers yet. Add one with its web address, like layouts.example.org."), this);
    empty_->setObjectName(QStringLiteral("noServers"));
    empty_->setWordWrap(true);
    col->addWidget(empty_);

    auto* buttons = new QHBoxLayout();
    auto* addBtn = new QPushButton(tr("Add a Server..."), this);
    addBtn->setObjectName(QStringLiteral("addServer"));
    addBtn->setProperty("accent", true);
    renameBtn_ = new QPushButton(tr("Rename..."), this);
    renameBtn_->setObjectName(QStringLiteral("renameServer"));
    removeBtn_ = new QPushButton(tr("Remove"), this);
    removeBtn_->setObjectName(QStringLiteral("removeServer"));
    signBtn_ = new QPushButton(tr("Sign In"), this);
    signBtn_->setObjectName(QStringLiteral("signInOut"));
    mainBtn_ = new QPushButton(tr("Make Main"), this);
    mainBtn_->setObjectName(QStringLiteral("makeMain"));
    mainBtn_->setToolTip(tr("Your settings follow your account on the Main server"));
    buttons->addWidget(addBtn);
    buttons->addStretch(1);
    buttons->addWidget(signBtn_);
    buttons->addWidget(bld::ui::help::withHelp(mainBtn_, QStringLiteral("servers.main"), this));
    buttons->addWidget(renameBtn_);
    buttons->addWidget(removeBtn_);
    col->addLayout(buttons);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    col->addWidget(close);

    // Each opens a nested dialog: once the click's signal has returned.
    const auto later = [this](void (ServersDialog::*fn)()) {
        return [this, fn] { QTimer::singleShot(0, this, fn); };
    };
    connect(addBtn, &QPushButton::clicked, this, later(&ServersDialog::onAdd));
    connect(renameBtn_, &QPushButton::clicked, this, later(&ServersDialog::onRename));
    connect(removeBtn_, &QPushButton::clicked, this, later(&ServersDialog::onRemove));
    connect(signBtn_, &QPushButton::clicked, this, later(&ServersDialog::onSignInOut));
    connect(mainBtn_, &QPushButton::clicked, this, [this] { makeMain(selectedServer()); });
    connect(rows_, &QListWidget::currentRowChanged, this, &ServersDialog::updateButtons);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);

    signInHandler_ = [this](const QUrl& url) {
        ServerApi api;
        ConnectDialog dialog(api, tokens_, openUrl_, this, ConnectDialog::Purpose::SignIn);
        dialog.setAddress(url.toString());
        QTimer::singleShot(0, &dialog, &ConnectDialog::connectToServer);
        if (dialog.exec() != QDialog::Accepted || !dialog.result()) return QString();
        return dialog.result()->token;
    };
    // Back from the browser (signed in or out there): check every server again.
    new RefreshOnFocus(this, [this] { refresh(); });
    refresh();
}

ServersDialog::~ServersDialog() = default;

void ServersDialog::refresh() {
    list_ = ServerList::load();
    status_.clear();
    rebuild();
    for (const ServerEntry& e : list_.servers()) check(e);
}

void ServersDialog::rebuild() {
    const QUrl keep = selectedServer();
    rows_->clear();
    for (const ServerEntry& e : list_.servers()) {
        auto* item = new QListWidgetItem(rows_);
        item->setData(kUrlRole, e.url);
        auto* row = new QWidget(rows_);
        row->setObjectName(QStringLiteral("serverRow"));
        auto* v = new QVBoxLayout(row);
        v->setContentsMargins(8, 6, 8, 6);
        v->setSpacing(0);
        auto* top = new QHBoxLayout();
        auto* name = new QLabel(row);
        name->setObjectName(QStringLiteral("serverName"));
        QFont bold = name->font();
        bold.setBold(true);
        name->setFont(bold);
        auto* badge = new QLabel(tr("★ Main"), row);
        badge->setObjectName(QStringLiteral("mainBadge"));
        badge->setToolTip(tr("Your settings follow your account on this server"));
        top->addWidget(name);
        top->addWidget(badge);
        top->addStretch(1);
        v->addLayout(top);
        v->addWidget(mutedLabel(row, "serverAddress"));
        v->addWidget(mutedLabel(row, "serverStatus"));
        // One line in every row, kept even when there's nothing to say, so
        // all the rows are the same height; the whole list is in its tooltip.
        auto* missing = new QLabel(row);
        missing->setObjectName(QStringLiteral("serverMissing"));
        missing->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        QSizePolicy retain = missing->sizePolicy();
        retain.setRetainSizeWhenHidden(true);
        missing->setSizePolicy(retain);
        // A fixed line: the "⚠" can come from a taller fallback font
        // (Windows), which would otherwise grow just this row.
        missing->setFixedHeight(missing->fontMetrics().height() + 2);
        v->addWidget(missing);
        rows_->setItemWidget(item, row);
        updateRow(e.url);
    }
    empty_->setVisible(list_.isEmpty());
    rows_->setVisible(!list_.isEmpty());
    if (keep.isValid() && list_.find(keep)) selectServer(keep);
    else if (rows_->count() > 0) rows_->setCurrentRow(0);
    updateButtons();
}

void ServersDialog::check(const ServerEntry& e) {
    const QUrl url = e.url;
    const QString key = e.key();
    status_[key] = Status{};
    // What it says about itself (no token needed).
    auto* about = new ServerApi(this);
    about->setBase(url);
    connect(about, &ServerApi::versionReady, this, [this, about, url](const ServerInfo& info) {
        about->deleteLater();
        list_ = ServerList::load();
        list_.remember(url, info);
        list_.save();
        updateRow(url);
    });
    connect(about, &ServerApi::requestFailed, this, [this, about, url, key](const QString& what, const QString&, bool) {
        if (what != QLatin1String("version")) return;
        about->deleteLater();
        status_[key].unreachable = true;
        updateRow(url);
    });
    about->fetchVersion();
    // Who is signed in there: its own token, sent only to it.
    std::weak_ptr<int> alive = alive_;
    tokens_.load(url, [this, alive, url, key](const QString& token) {
        if (alive.expired()) return;
        if (token.isEmpty()) {
            status_[key].sign = Status::Sign::Out;
            updateRow(url);
            return;
        }
        auto* who = new ServerApi(this);
        who->setBase(url);
        who->setToken(token);
        connect(who, &ServerApi::currentUserReady, this, [this, who, url, key](const QString&, const QString& name) {
            who->deleteLater();
            status_[key].sign = Status::Sign::In;
            list_ = ServerList::load();
            list_.rememberUser(url, name);
            list_.save();
            updateRow(url);
        });
        connect(who, &ServerApi::requestFailed, this,
                [this, who, url, key](const QString&, const QString&, bool unauthorized) {
                    who->deleteLater();
                    // Not reachable: still signed in, as far as we know.
                    status_[key].sign = unauthorized ? Status::Sign::Expired : Status::Sign::In;
                    if (!unauthorized) status_[key].unreachable = true;
                    updateRow(url);
                });
        who->fetchCurrentUser();
    });
}

QString ServersDialog::statusText(const QUrl& url) const {
    const ServerEntry* e = list_.find(url);
    if (!e) return {};
    const Status st = status_.value(e->key());
    QStringList parts;
    switch (st.sign) {
    case Status::Sign::Checking: parts << tr("Checking…"); break;
    case Status::Sign::In:
        parts << (e->userName.isEmpty() ? tr("Signed in") : tr("Signed in as %1").arg(e->userName));
        break;
    case Status::Sign::Out: parts << tr("Not signed in"); break;
    case Status::Sign::Expired: parts << tr("Signed out by the server. Sign in again."); break;
    }
    if (!e->version.isEmpty()) parts << tr("version %1").arg(e->version);
    if (st.unreachable) parts << tr("can't reach it right now");
    return parts.join(QStringLiteral(" · "));
}

QString ServersDialog::missingText(const QUrl& url) const {
    const ServerEntry* e = list_.find(url);
    if (!e) return {};
    const QStringList missing = e->missing();
    if (missing.isEmpty()) return {};
    QStringList names;
    for (const QString& f : missing) names << featureLabel(f);
    return tr("⚠ Can't do yet: %1").arg(names.join(QStringLiteral(", ")));
}

bool ServersDialog::isSignedIn(const QUrl& url) const {
    const ServerEntry* e = list_.find(url);
    return e && status_.value(e->key()).sign == Status::Sign::In;
}

void ServersDialog::updateRow(const QUrl& url) {
    const ServerEntry* e = list_.find(url);
    if (!e) return;
    for (int i = 0; i < rows_->count(); ++i) {
        QListWidgetItem* item = rows_->item(i);
        if (TokenStore::keyFor(item->data(kUrlRole).toUrl()) != e->key()) continue;
        QWidget* row = rows_->itemWidget(item);
        if (!row) return;
        row->findChild<QLabel*>(QStringLiteral("serverName"))->setText(e->label());
        row->findChild<QLabel*>(QStringLiteral("mainBadge"))->setVisible(e->main);
        row->findChild<QLabel*>(QStringLiteral("serverAddress"))->setText(e->address());
        row->findChild<QLabel*>(QStringLiteral("serverStatus"))->setText(statusText(url));
        auto* missing = row->findChild<QLabel*>(QStringLiteral("serverMissing"));
        missing->setText(missingText(url));
        missing->setToolTip(missing->text());
        missing->setVisible(!missing->text().isEmpty());
        item->setSizeHint(row->sizeHint());
        break;
    }
    updateButtons();
}

void ServersDialog::updateButtons() {
    const QUrl url = selectedServer();
    const ServerEntry* e = url.isValid() ? list_.find(url) : nullptr;
    renameBtn_->setEnabled(e);
    removeBtn_->setEnabled(e);
    mainBtn_->setEnabled(e && !e->main);
    signBtn_->setEnabled(e);
    signBtn_->setText(e && isSignedIn(url) ? tr("Sign Out") : tr("Sign In"));
}

QUrl ServersDialog::selectedServer() const {
    const QListWidgetItem* item = rows_->currentItem();
    return item ? item->data(kUrlRole).toUrl() : QUrl();
}

void ServersDialog::selectServer(const QUrl& url) {
    for (int i = 0; i < rows_->count(); ++i)
        if (TokenStore::keyFor(rows_->item(i)->data(kUrlRole).toUrl()) == TokenStore::keyFor(url)) {
            rows_->setCurrentRow(i);
            return;
        }
}

bool ServersDialog::addServer(const QString& address, const QString& name, QString* error) {
    const auto url = ServerApi::normalizeBase(address, error);
    if (!url) return false;
    list_ = ServerList::load();
    const bool isNew = !list_.find(*url);
    ServerEntry& e = list_.add(*url, name);
    if (!isNew && !name.trimmed().isEmpty()) e.name = name.trimmed();
    list_.save();
    rebuild();
    selectServer(*url);
    if (const ServerEntry* added = list_.find(*url)) check(*added);
    return true;
}

void ServersDialog::renameServer(const QUrl& url, const QString& name) {
    list_ = ServerList::load();
    if (!list_.rename(url, name)) return;
    list_.save();
    updateRow(url);
}

void ServersDialog::removeServer(const QUrl& url) {
    list_ = ServerList::load();
    if (!list_.remove(url)) return;
    list_.save();
    // Its sign-in goes with it.
    tokens_.remove(url);
    status_.remove(TokenStore::keyFor(url));
    rebuild();
}

void ServersDialog::makeMain(const QUrl& url) {
    if (!url.isValid()) return;
    list_ = ServerList::load();
    list_.setMain(url);
    list_.save();
    for (const ServerEntry& e : list_.servers()) updateRow(e.url);
}

void ServersDialog::signOut(const QUrl& url) {
    const ServerEntry* e = list_.find(url);
    if (!e) return;
    tokens_.remove(url);
    status_[e->key()].sign = Status::Sign::Out;
    updateRow(url);
}

void ServersDialog::signIn(const QUrl& url) {
    QTimer::singleShot(0, this, [this, url] {
        const ServerEntry* e = list_.find(url);
        if (!e || !signInHandler_) return;
        const QString token = signInHandler_(url);
        if (token.isEmpty()) return;
        list_ = ServerList::load();
        if (const ServerEntry* now = list_.find(url)) check(*now);
    });
}

void ServersDialog::onAdd() {
    QDialog ask(this);
    ask.setWindowTitle(tr("Add a Server"));
    auto* form = new QFormLayout(&ask);
    auto* intro = new QLabel(tr("Type the server's web address, the one you open in your browser."), &ask);
    intro->setWordWrap(true);
    form->addRow(intro);
    auto* address = new QLineEdit(&ask);
    address->setObjectName(QStringLiteral("newServerAddress"));
    address->setPlaceholderText(QStringLiteral("layouts.example.org"));
    auto* name = new QLineEdit(&ask);
    name->setObjectName(QStringLiteral("newServerName"));
    name->setPlaceholderText(tr("e.g. Train club"));
    auto* problem = new QLabel(&ask);
    problem->setWordWrap(true);
    problem->hide();
    form->addRow(tr("Web address"), bld::ui::help::withHelp(address, QStringLiteral("connect.server"), &ask));
    form->addRow(tr("Name (optional)"), name);
    form->addRow(problem);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &ask);
    box->button(QDialogButtonBox::Ok)->setText(tr("Add"));
    form->addRow(box);
    connect(box, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
    connect(box, &QDialogButtonBox::accepted, &ask, [&] {
        QString error;
        if (addServer(address->text(), name->text(), &error)) {
            ask.accept();
            return;
        }
        problem->setText(error);
        problem->show();
    });
    ask.exec();
}

void ServersDialog::onRename() {
    const QUrl url = selectedServer();
    const ServerEntry* e = list_.find(url);
    if (!e) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Server"), tr("What do you call %1?").arg(e->address()),
                                               QLineEdit::Normal, e->name, &ok);
    if (ok) renameServer(url, name);
}

void ServersDialog::onRemove() {
    const QUrl url = selectedServer();
    const ServerEntry* e = list_.find(url);
    if (!e) return;
    bld::ui::DeleteWording w;
    w.verb = tr("Remove");
    w.removes = tr("%1 leaves your list of servers, and you’re signed out of it here.").arg(e->label());
    w.keeps = tr("Your layouts stay on the server.");
    w.undo = tr("You can add it again later.");
    if (bld::ui::ConfirmDialog::confirmDelete(this, e->label(), w)) removeServer(url);
}

void ServersDialog::onSignInOut() {
    const QUrl url = selectedServer();
    if (!url.isValid()) return;
    if (isSignedIn(url)) signOut(url);
    else signIn(url);
}

}  // namespace bld::sync
