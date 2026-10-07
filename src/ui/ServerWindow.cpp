#include "ServerWindow.h"

#include "ConfirmDialog.h"
#include "ReturnWording.h"
#include "ServerLibrary.h"
#include "help/HelpButton.h"
#include "tours/Tours.h"

#include "OwnerFilter.h"
#include "RefreshOnFocus.h"
#include "ServerList.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScroller>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace bld::ui {

namespace {

// The Show filter, kept for each server.
QString showSettingsKey(const QUrl& server) {
    return QStringLiteral("serverWindow/show/") + sync::ServerList::folderName(server);
}

// "by Sam · in ArkLUG", else whose it is ("Yours", the club).
QString whoseText(const sync::Credit& credit, const sync::ItemOwner& owner) {
    const QString line = sync::creditLine(credit);
    if (!line.isEmpty()) return line;
    return owner.key == sync::kShowMine ? ServerWindow::tr("Yours") : owner.label;
}

QString whenText(const QString& what, const QDateTime& at) {
    if (!at.isValid()) return {};
    return what.arg(QLocale().toString(at.toLocalTime(), QLocale::ShortFormat));
}

sync::ItemOwner ownerOf(const sync::OwnerTag& t) {
    return t.isClub() ? sync::itemOwner(t.slug, t.id, t.name) : sync::itemOwner({}, {}, {});
}

// A file name from a title: no characters a file system refuses.
QString fileNameOf(const QString& title) {
    static const QRegularExpression bad(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])"));
    QString n = title;
    n.replace(bad, QStringLiteral("_"));
    n = n.trimmed();
    return n.isEmpty() ? QStringLiteral("Layout") : n;
}

}  // namespace

ServerWindow::ServerWindow(ServerLibrary& library, QWidget* parent) : QWidget(parent, Qt::Window), library_(library) {
    setObjectName(QStringLiteral("ServerWindow"));
    setWindowTitle(tr("Open from Server"));
    resize(780, 640);
    tours::tag(this, QStringLiteral("server.window"));

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(14, 12, 14, 12);
    col->setSpacing(8);

    // Which server, and who is signed in there.
    auto* head = new QHBoxLayout();
    auto* title = new QLabel(tr("Server"), this);
    QFont tf = title->font();
    tf.setBold(true);
    if (tf.pointSizeF() > 0) tf.setPointSizeF(tf.pointSizeF() * 1.3);
    title->setFont(tf);
    servers_ = new QComboBox(this);
    servers_->setObjectName(QStringLiteral("serverPicker"));
    servers_->setAccessibleName(tr("Server"));
    servers_->setToolTip(tr("Which of your servers to look at"));
    servers_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    auto* manage = new QPushButton(tr("Manage Servers…"), this);
    manage->setObjectName(QStringLiteral("manageServers"));
    manage->setToolTip(tr("Add, rename or remove servers, sign in or out, and pick your Main one"));
    head->addWidget(title);
    head->addWidget(servers_, 1);
    head->addWidget(manage);
    head->addWidget(new help::HelpButton(QStringLiteral("server.window"), this, servers_));
    col->addLayout(head);
    account_ = libraryMutedLabel(QString(), this);
    account_->setObjectName(QStringLiteral("serverAccount"));
    col->addWidget(account_);

    status_ = new LibraryStatusLine(library_, this);
    status_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    col->addWidget(status_);

    content_ = new QWidget(this);
    auto* c = new QVBoxLayout(content_);
    c->setContentsMargins(0, 0, 0, 0);
    c->setSpacing(6);
    auto* filters = new QHBoxLayout();
    search_ = new QLineEdit(content_);
    search_->setObjectName(QStringLiteral("serverSearch"));
    search_->setPlaceholderText(tr("Find a layout, venue, module or part"));
    search_->setClearButtonEnabled(true);
    show_ = new QComboBox(content_);
    show_->setObjectName(QStringLiteral("ownerFilter"));
    show_->setToolTip(tr("Show everything, only yours, or one club's"));
    show_->setAccessibleName(tr("Show"));
    filters->addWidget(search_, 1);
    filters->addWidget(new QLabel(tr("Show:"), content_));
    filters->addWidget(help::withHelp(show_, QStringLiteral("owners.filter"), content_));
    c->addLayout(filters);

    tabs_ = new QTabWidget(content_);
    tabs_->setObjectName(QStringLiteral("serverTabs"));
    tabs_->setDocumentMode(true);
    // Sending: a layout file of yours, and all your parts the server lacks.
    auto* saveFile = new QPushButton(tr("Save a file to the server…"));
    saveFile->setObjectName(QStringLiteral("saveLayoutFile"));
    saveFile->setToolTip(tr("Put a layout file from this computer (.bld-layout, .bbm and more) on the server"));
    connect(saveFile, &QPushButton::clicked, this, [this] { QTimer::singleShot(0, this, [this] { emit saveLayoutFileRequested(); }); });
    auto* uploadParts = new QPushButton(tr("Upload to server…"));
    uploadParts->setObjectName(QStringLiteral("uploadParts"));
    uploadParts->setToolTip(tr("Send your own parts the server doesn't have yet, all at once"));
    connect(uploadParts, &QPushButton::clicked, this, [this] { QTimer::singleShot(0, this, [this] { emit uploadPartsRequested(); }); });
    layoutsPage_ = makePage(QStringLiteral("layoutsPage"), { saveFile });
    venuesPage_ = makePage(QStringLiteral("venuesPage"));
    modulesPage_ = makePage(QStringLiteral("modulesPage"));
    partsPage_ = makePage(QStringLiteral("partsPage"), { uploadParts });
    tabs_->addTab(layoutsPage_.scroll, tr("Layouts"));
    tabs_->addTab(venuesPage_.scroll, tr("Venues"));
    tabs_->addTab(modulesPage_.scroll, tr("Modules"));
    tabs_->addTab(partsPage_.scroll, tr("Parts"));
    catalog_ = new CatalogTab(library_, tabs_, true);
    catalog_->setKinds({ CatalogTab::Kind::Modules, CatalogTab::Kind::Parts, CatalogTab::Kind::Layouts, CatalogTab::Kind::Venues });
    collections_ = new CatalogTab(library_, tabs_, true);
    collections_->setObjectName(QStringLiteral("collectionsTab"));
    collections_->setKinds({ CatalogTab::Kind::Collections });
    tabs_->addTab(catalog_, tr("Catalog"));
    tabs_->addTab(collections_, tr("Collections"));
    tabs_->setTabToolTip(4, tr("Modules, parts, layouts and venues people shared for everyone"));
    tabs_->setTabToolTip(5, tr("Sets of catalog things to add in one go"));
    c->addWidget(tabs_, 1);
    col->addWidget(content_, 1);
    // Signed out, offline: the friendly line stays at the top, the rest empty.
    filler_ = new QWidget(this);
    filler_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    col->addWidget(filler_, 1);

    confirm = [this](const ConfirmOptions& o) { return ConfirmDialog::ask(this, o); };
    askSavePath = [this](const QString& title, bool native) {
        const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const QString nativeFilter = tr("Brick Layout Designer layout (*.bld-layout)");
        const QString bbmFilter = tr("BlueBrick map (*.bbm)");
        QString chosen = native ? nativeFilter : bbmFilter;
        QString path = QFileDialog::getSaveFileName(
            this, tr("Download a copy"),
            QDir(docs).filePath(fileNameOf(title) + (native ? QStringLiteral(".bld-layout") : QStringLiteral(".bbm"))),
            native ? nativeFilter + QStringLiteral(";;") + bbmFilter : bbmFilter, &chosen);
        if (path.isEmpty()) return path;
        const bool bbm = chosen == bbmFilter || !native;
        if (!path.endsWith(QStringLiteral(".bld-layout"), Qt::CaseInsensitive) &&
            !path.endsWith(QStringLiteral(".bbm"), Qt::CaseInsensitive))
            path += bbm ? QStringLiteral(".bbm") : QStringLiteral(".bld-layout");
        return path;
    };

    hintTimer_ = new QTimer(this);
    hintTimer_->setSingleShot(true);
    hintTimer_->setInterval(300);
    connect(hintTimer_, &QTimer::timeout, this, &ServerWindow::loadLists);

    connect(servers_, &QComboBox::activated, this, [this] {
        const QUrl url = servers_->currentData().toUrl();
        if (url.isValid() && url != library_.server()) emit serverPicked(url);
    });
    // A nested dialog's event loop must not run inside the click's signal.
    connect(manage, &QPushButton::clicked, this, [this] {
        QTimer::singleShot(0, this, [this] { emit manageServersRequested(); });
    });
    connect(show_, &QComboBox::activated, this, [this] {
        if (library_.server().isValid()) QSettings().setValue(showSettingsKey(library_.server()), show_->currentData().toString());
        rebuild();
    });
    connect(search_, &QLineEdit::textChanged, this, [this] { rebuild(); });
    connect(&library_, &ServerLibrary::stateChanged, this, [this] {
        showState();
        if (library_.server() != shownServer_ || library_.state() != ServerLibrary::State::Ready) {
            // Another server, or signed out: what was listed is not this one's.
            ++generation_;
            shownServer_ = library_.server();
            layouts_.clear();
            venues_.clear();
            parts_.clear();
            shared_.clear();
            layoutsIn_ = venuesIn_ = partsIn_ = false;
            layoutsWhy_.clear();
            venuesWhy_.clear();
            partsWhy_.clear();
            refreshServers();
        }
        if (library_.state() == ServerLibrary::State::Ready) reload();
        else rebuild();
    });
    connect(&library_, &ServerLibrary::changed, this, [this] {
        showState();
        rebuildFilter();
        rebuildModules();
    });
    connect(&library_, &ServerLibrary::editingChanged, this, [this] { rebuildModules(); });
    // Back in the window: someone may have changed things on the web meanwhile.
    new sync::RefreshOnFocus(this, [this] {
        if (library_.state() == ServerLibrary::State::Ready || library_.state() == ServerLibrary::State::Offline) {
            library_.refresh();
            reload();
            emit localRefreshWanted();
        } else {
            // Signed in elsewhere (Servers…) meanwhile: the server again, with its token.
            emit serverPicked(library_.server());
        }
    });
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, &QWidget::close);

    shownServer_ = library_.server();
    refreshServers();
    showState();
    rebuild();
}

void ServerWindow::loadLists() {
    if (!isVisible()) {
        stale_ = true;
        return;
    }
    if (library_.state() != ServerLibrary::State::Ready) return;
    const int gen = generation_;
    const auto failed = [this, gen](const QString& kind) {
        return [this, gen, kind](const sync::ServerRefusal& r) {
            if (gen != generation_) return;
            if (r.status == 401) {
                library_.refresh();
                return;
            }
            const QString why = ServerLibrary::refusalText(r);
            if (kind == QLatin1String("layouts")) layoutsWhy_ = why;
            else if (kind == QLatin1String("venues")) venuesWhy_ = why;
            else partsWhy_ = why;
            rebuild();
        };
    };
    if (std::exchange(hintLayouts_, false)) {
        ++loads_;
        library_.api().layouts([this, gen](const QList<sync::LayoutEntry>& list) {
            if (gen != generation_) return;
            layouts_ = list;
            layoutsIn_ = true;
            layoutsWhy_.clear();
            rebuildFilter();
            rebuildLayouts();
        }, failed(QStringLiteral("layouts")));
    }
    if (std::exchange(hintVenues_, false)) {
        // A server without a venue library is not asked (it would only say "not found").
        const sync::ServerList list = sync::ServerList::load();
        const sync::ServerEntry* e = list.find(library_.server());
        if (e && e->features && !e->features->contains(QStringLiteral("venues"))) {
            venues_.clear();
            venuesIn_ = true;
            venuesWhy_ = tr("This server doesn't have a venue library yet.");
            rebuildVenues();
        } else {
            ++loads_;
            library_.api().venues([this, gen](const QList<sync::VenueEntry>& list) {
                if (gen != generation_) return;
                venues_ = list;
                venuesIn_ = true;
                venuesWhy_.clear();
                rebuildFilter();
                rebuildVenues();
            }, failed(QStringLiteral("venues")));
        }
    }
    if (std::exchange(hintCatalog_, false) && hasFeature(QStringLiteral("catalogShare"))) {
        const sync::CatalogSettings& cat = library_.catalog();
        if (cat.modules || cat.parts || cat.layouts || cat.venues) {
            ++loads_;
            library_.api().myCatalogItems([this, gen](const QList<sync::MyCatalogItem>& list) {
                if (gen != generation_) return;
                shared_ = list;
                rebuild();
            }, {});
        }
    }
    if (std::exchange(hintParts_, false)) {
        ++loads_;
        library_.api().customParts([this, gen](const QList<sync::ServerPart>& list) {
            if (gen != generation_) return;
            parts_ = list;
            partsIn_ = true;
            partsWhy_.clear();
            rebuildFilter();
            rebuildParts();
        }, failed(QStringLiteral("parts")));
    }
}

ServerWindow::Page ServerWindow::makePage(const QString& name, const QList<QPushButton*>& tools) {
    Page p;
    p.scroll = new QScrollArea(this);
    p.scroll->setObjectName(name);
    p.scroll->setWidgetResizable(true);
    p.scroll->setFrameShape(QFrame::NoFrame);
    p.host = new QWidget(p.scroll);
    p.list = new QVBoxLayout(p.host);
    p.list->setContentsMargins(0, 6, 0, 0);
    p.list->setSpacing(4);
    if (!tools.isEmpty()) {
        auto* row = new QHBoxLayout();
        for (QPushButton* b : tools) {
            b->setParent(p.host);
            row->addWidget(b);
        }
        row->addWidget(new help::HelpButton(QStringLiteral("server.send"), p.host, tools.front()));
        row->addStretch(1);
        p.list->addLayout(row);
    }
    p.empty = libraryMutedLabel(QString(), p.host);
    p.empty->setObjectName(QStringLiteral("emptyLine"));
    p.empty->setTextInteractionFlags(Qt::TextSelectableByMouse);
    p.list->addWidget(p.empty);
    p.list->addStretch(1);
    p.scroll->setWidget(p.host);
    // A finger flicks the list up and down.
    QScroller::grabGesture(p.scroll->viewport(), QScroller::TouchGesture);
    return p;
}

void ServerWindow::clearPage(Page& pg) {
    // Rows may be rebuilt from one of their own buttons: let them go later.
    for (QWidget* w : std::as_const(pg.rows)) {
        pg.list->removeWidget(w);
        w->hide();
        w->deleteLater();
    }
    pg.rows.clear();
}

QWidget* ServerWindow::makeRow(Page& pg, const RowSpec& spec) {
    QFrame* frame = libraryRowFrame(pg.host);
    frame->setObjectName(spec.objectName);
    auto* h = new QHBoxLayout(frame);
    h->setContentsMargins(8, 8, 8, 8);
    h->setSpacing(10);
    QLabel* pic = libraryRowPicture(frame);
    pic->setText(spec.placeholder);
    h->addWidget(pic, 0, Qt::AlignTop);
    if (!spec.picture.isEmpty())
        library_.picture(spec.picture, pic, [pic](const QImage& img) { showLibraryPicture(pic, img); });

    auto* text = new QVBoxLayout();
    text->setSpacing(2);
    auto* title = new QLabel(spec.title, frame);
    title->setObjectName(QStringLiteral("rowTitle"));
    title->setProperty("rowTitle", true);
    title->setWordWrap(true);
    text->addWidget(title);
    for (const QString& line : spec.lines) {
        if (line.isEmpty()) continue;
        auto* l = libraryMutedLabel(line, frame);
        l->setObjectName(QStringLiteral("rowLine"));
        text->addWidget(l);
    }
    h->addLayout(text, 1);

    if (!spec.pill.isEmpty()) {
        auto* pill = new QLabel(spec.pill, frame);
        pill->setObjectName(QStringLiteral("rowPill"));
        pill->setProperty("pill", true);
        h->addWidget(pill, 0, Qt::AlignVCenter);
    } else if (!spec.primary.text.isEmpty()) {
        auto* b = new QPushButton(spec.primary.text, frame);
        b->setObjectName(spec.primary.name);
        b->setProperty("accent", true);
        b->setEnabled(spec.primary.enabled);
        b->setToolTip(spec.primary.tip);
        b->setAccessibleName(spec.primary.text + QStringLiteral(" ") + spec.title);
        connect(b, &QPushButton::clicked, this, [run = spec.primary.run] {
            if (run) run();
        });
        h->addWidget(b, 0, Qt::AlignVCenter);
    }
    if (spec.pill.isEmpty() && !spec.more.isEmpty()) {
        auto* more = new QToolButton(frame);
        more->setObjectName(QStringLiteral("rowMore"));
        more->setText(QStringLiteral("⋯"));
        more->setToolTip(tr("More for %1").arg(spec.title));
        more->setAccessibleName(tr("More for %1").arg(spec.title));
        more->setPopupMode(QToolButton::InstantPopup);
        more->setProperty("rowMore", true);
        auto* menu = new QMenu(more);
        menu->setToolTipsVisible(true);
        for (const Action& a : spec.more) {
            QAction* act = menu->addAction(a.text);
            act->setObjectName(a.name);
            act->setEnabled(a.enabled);
            act->setToolTip(a.tip);
            // After the menu has closed: the action may open a dialog.
            connect(act, &QAction::triggered, this, [this, run = a.run] {
                QTimer::singleShot(0, this, [run] {
                    if (run) run();
                });
            });
        }
        more->setMenu(menu);
        h->addWidget(more, 0, Qt::AlignVCenter);
    }
    pg.rows.insert(spec.objectName, frame);
    // Before the empty line and the stretch.
    pg.list->insertWidget(pg.list->count() - 2, frame);
    return frame;
}

void ServerWindow::showTab(Tab tab) {
    tabs_->setCurrentIndex(static_cast<int>(tab));
    show();
    raise();
    activateWindow();
}

ServerWindow::Tab ServerWindow::currentTab() const { return static_cast<Tab>(tabs_->currentIndex()); }

QWidget* ServerWindow::row(Tab tab, const QString& id) const {
    switch (tab) {
    case Tab::Layouts: return layoutsPage_.rows.value(QStringLiteral("layoutRow:") + id);
    case Tab::Venues: return venuesPage_.rows.value(QStringLiteral("venueRow:") + id);
    case Tab::Modules: return modulesPage_.rows.value(QStringLiteral("moduleRow:") + id);
    case Tab::Parts: return partsPage_.rows.value(QStringLiteral("partRow:") + id);
    case Tab::Catalog: return catalog_->row(id);
    case Tab::Collections: return collections_->row(id);
    }
    return nullptr;
}

QLabel* ServerWindow::emptyLine(Tab tab) const {
    switch (tab) {
    case Tab::Layouts: return layoutsPage_.empty;
    case Tab::Venues: return venuesPage_.empty;
    case Tab::Modules: return modulesPage_.empty;
    case Tab::Parts: return partsPage_.empty;
    default: return nullptr;
    }
}

QString ServerWindow::rememberedShow(const QUrl& server) {
    return QSettings().value(showSettingsKey(server), sync::kShowAll).toString();
}

void ServerWindow::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    emit localRefreshWanted();
    refreshServers();
    if (stale_) reload();
}

void ServerWindow::refreshServers() {
    const QSignalBlocker block(servers_);
    servers_->clear();
    const sync::ServerList list = sync::ServerList::load();
    for (const sync::ServerEntry& e : list.servers())
        servers_->addItem(e.main ? tr("%1 (Main)").arg(e.label()) : e.label(), e.url);
    servers_->setEnabled(!list.isEmpty());
    if (list.isEmpty()) servers_->addItem(tr("No servers yet"));
    const sync::ServerEntry* current = list.find(library_.server());
    if (current) servers_->setCurrentIndex(servers_->findData(current->url));
    showState();
}

void ServerWindow::showState() {
    using S = ServerLibrary::State;
    status_->showState();
    const bool ready = library_.state() == S::Ready;
    content_->setVisible(ready);
    filler_->setVisible(!ready);
    const sync::ServerList list = sync::ServerList::load();
    const sync::ServerEntry* e = list.find(library_.server());
    if (ready) {
        const QString where = e ? e->address() : library_.server().host();
        account_->setText(e && !e->userName.isEmpty() ? tr("Signed in as %1 · %2").arg(e->userName, where)
                                                      : tr("Signed in · %1").arg(where));
    }
    account_->setVisible(ready);
}

void ServerWindow::reload() {
    if (!isVisible()) {
        stale_ = true;
        return;
    }
    stale_ = false;
    if (library_.state() != ServerLibrary::State::Ready) return;
    hintLayouts_ = hintVenues_ = hintParts_ = hintCatalog_ = true;
    hintTimer_->stop();
    // Now, not after the hints' wait.
    loadLists();
}

void ServerWindow::hint(const QJsonObject& h) {
    const QString kind = h.value(QLatin1String("kind")).toString();
    if (kind == QLatin1String("layout")) hintLayouts_ = true;
    else if (kind == QLatin1String("venue")) hintVenues_ = true;
    else if (kind == QLatin1String("custom-part")) hintParts_ = true;
    else if (kind == QLatin1String("catalog")) hintCatalog_ = true;
    else if (kind == QLatin1String("club") || kind == QLatin1String("me"))
        // Joined or left a club, or a club's things moved: all of it.
        hintLayouts_ = hintVenues_ = hintParts_ = true;
    else return;
    // A burst of hints, once.
    hintTimer_->start();
}

void ServerWindow::partsChanged() { rebuildParts(); }

QString ServerWindow::showKey() const {
    const QString key = show_->currentData().toString();
    return key.isEmpty() ? sync::kShowAll : key;
}

bool ServerWindow::shown(const QString& ownerKey, const QStringList& texts) const {
    if (!sync::ownerMatches(showKey(), ownerKey)) return false;
    const QString f = search_->text().trimmed();
    if (f.isEmpty()) return true;
    return std::any_of(texts.begin(), texts.end(), [&](const QString& t) { return t.contains(f, Qt::CaseInsensitive); });
}

QString ServerWindow::whereText(const QString& has, const QString& have) const {
    // "Train Club has no layouts yet", "You have no …", "There are no …".
    const QString key = showKey();
    if (key == sync::kShowMine) return have;
    if (key == sync::kShowAll) return {};
    return has.arg(show_->currentText());
}

void ServerWindow::rebuildFilter() {
    // All, Mine, then your clubs and any other club something listed belongs to.
    QList<std::pair<QString, QString>> clubs;
    const auto addClub = [&](const sync::ItemOwner& o) {
        if (o.key == sync::kShowMine) return;
        for (const auto& c : clubs)
            if (c.first == o.key) return;
        clubs.append({ o.key, o.label });
    };
    for (const auto& o : library_.orgs()) addClub({ o.slug, o.name });
    for (const auto& l : layouts_) addClub(sync::itemOwner(l.ownerOrgSlug, {}, l.ownerOrgName));
    for (const auto& v : venues_) addClub(sync::itemOwner(v.ownerOrgSlug, v.ownerOrgId, v.ownerOrgName));
    for (const auto& m : library_.modules()) addClub(ownerOf(m.owner));
    for (const auto& p : parts_) addClub(ownerOf(p.owner));
    const QSignalBlocker block(show_);
    show_->clear();
    show_->addItem(tr("All"), sync::kShowAll);
    show_->addItem(tr("Mine"), sync::kShowMine);
    for (const auto& [key, name] : clubs) show_->addItem(name, key);
    // The last choice for this server, while that club is still listed; else All.
    const int at = show_->findData(rememberedShow(library_.server()));
    show_->setCurrentIndex(at >= 0 ? at : 0);
}

void ServerWindow::rebuild() {
    rebuildFilter();
    rebuildLayouts();
    rebuildVenues();
    rebuildModules();
    rebuildParts();
}

QList<ServerWindow::Action> ServerWindow::returnActions(const QString& kindPath, const QString& word, const QString& id,
                                                        const QString& title, const sync::Credit& credit) {
    QList<Action> out;
    if (credit.canTakeBack)
        out << Action{ tr("Take Back to Mine…"), QStringLiteral("takeBack"),
                       [this, kindPath, word, id, title, credit] { giveOrTakeBack(kindPath, word, id, title, credit, false); },
                       true, tr("It becomes yours again; the club keeps its own copy, credited to you.") };
    if (credit.canGiveBack)
        out << Action{ tr("Give Back to %1…").arg(credit.authorName), QStringLiteral("giveBack"),
                       [this, kindPath, word, id, title, credit] { giveOrTakeBack(kindPath, word, id, title, credit, true); },
                       true, tr("It goes back to the person who made it; the club keeps its own copy.") };
    return out;
}

void ServerWindow::rebuildLayouts() {
    clearPage(layoutsPage_);
    QList<sync::LayoutEntry> list = layouts_;
    // Newest first, as the web's Home page lists them.
    std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.updatedAt > b.updatedAt; });
    int n = 0;
    for (const sync::LayoutEntry& l : std::as_const(list)) {
        const sync::ItemOwner owner = sync::itemOwner(l.ownerOrgSlug, {}, l.ownerOrgName);
        const QString who = whoseText(l.credit, owner);
        if (!shown(owner.key, { l.title, who })) continue;
        ++n;
        const bool viewer = l.role == QLatin1String("viewer");
        // Older servers don't send the role: their list held only your own layouts.
        const bool isOwner = l.role.isEmpty() || l.role == QLatin1String("owner");
        RowSpec r;
        r.objectName = QStringLiteral("layoutRow:") + l.id;
        r.title = l.title.isEmpty() ? tr("Untitled Layout") : l.title;
        r.placeholder = QStringLiteral("▦");
        r.lines << who
                << QStringList{ viewer ? tr("View only") : QString(), whenText(tr("updated %1"), l.updatedAt) }
                       .filter(QRegularExpression(QStringLiteral(".+")))
                       .join(QStringLiteral(" · "));
        r.primary = { tr("Open live"), QStringLiteral("openLayout"), [this, l] { emit openLayoutRequested(l); }, true,
                      viewer ? tr("Opens it to look at: you can't change this one.")
                             : tr("Opens it here, kept in step with everyone working on it.") };
        r.more << Action{ tr("Download a copy…"), QStringLiteral("downloadLayout"), [this, l] { downloadLayout(l); }, true,
                          canDownloadNative() ? tr("Save it as a file on this computer (.bld-layout, or .bbm for BlueBrick)")
                                              : tr("Save it as a BlueBrick map (.bbm) on this computer") };
        r.more << returnActions(QStringLiteral("layouts"), tr("layout"), l.id, r.title, l.credit);
        addShareAction(r, QStringLiteral("layout"), l.id, r.title, isOwner);
        if (isOwner) r.more << Action{ tr("Delete…"), QStringLiteral("deleteLayout"), [this, l] { deleteLayout(l); }, true, {} };
        makeRow(layoutsPage_, r);
    }
    {
        QStringList onServer;
        for (const auto& l : layouts_) onServer << l.title;
        addLocalRows(Tab::Layouts, onServer);
    }
    QString empty;
    if (!layoutsWhy_.isEmpty()) empty = layoutsWhy_;
    else if (!layoutsIn_) empty = tr("Loading…");
    else if (n == 0 && !search_->text().trimmed().isEmpty()) empty = tr("No layouts match.");
    else if (n == 0) {
        const QString where = whereText(tr("%1 has no layouts yet."), tr("You have no layouts yet."));
        empty = (where.isEmpty() ? tr("There are no layouts here yet.") : where) + QLatin1Char(' ') +
                tr("Send one with Save a file to the server…, or start one on the web.");
    }
    layoutsPage_.empty->setText(empty);
    layoutsPage_.empty->setVisible(!empty.isEmpty());
}

void ServerWindow::rebuildVenues() {
    clearPage(venuesPage_);
    QList<sync::VenueEntry> list = venues_;
    std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.name.localeAwareCompare(b.name) < 0; });
    int n = 0;
    for (const sync::VenueEntry& v : std::as_const(list)) {
        const sync::ItemOwner owner = sync::itemOwner(v.ownerOrgSlug, v.ownerOrgId, v.ownerOrgName);
        const QString who = whoseText(v.credit, owner);
        if (!shown(owner.key, { v.name, who })) continue;
        ++n;
        RowSpec r;
        r.objectName = QStringLiteral("venueRow:") + v.id;
        r.title = v.name.isEmpty() ? tr("Untitled venue") : v.name;
        r.placeholder = QStringLiteral("⌂");
        r.lines << who
                << QStringList{ studsSizeText(v.widthStuds, v.heightStuds), whenText(tr("saved %1"), v.createdAt) }
                       .filter(QRegularExpression(QStringLiteral(".+")))
                       .join(QStringLiteral(" · "));
        r.primary = { tr("Use this venue"), QStringLiteral("useVenue"), [this, v] { emit useVenueRequested(v.id, v.name); },
                      true, tr("Start a new layout in this venue (it's kept in your Venue library too)") };
        r.more << Action{ tr("Add to my venue library"), QStringLiteral("addVenue"),
                          [this, v] { emit addVenueRequested(v.id, v.name); }, true,
                          tr("Keep a copy in the Venue library on this computer") };
        r.more << returnActions(QStringLiteral("venues"), tr("venue"), v.id, r.title, v.credit);
        addShareAction(r, QStringLiteral("venue"), v.id, r.title, v.canManage);
        if (v.canManage) r.more << Action{ tr("Delete…"), QStringLiteral("deleteVenue"), [this, v] { deleteVenue(v); }, true, {} };
        makeRow(venuesPage_, r);
    }
    {
        QStringList onServer;
        for (const auto& v : venues_) onServer << v.name;
        addLocalRows(Tab::Venues, onServer);
    }
    QString empty;
    if (!venuesWhy_.isEmpty()) empty = venuesWhy_;
    else if (!venuesIn_) empty = tr("Loading…");
    else if (n == 0 && !search_->text().trimmed().isEmpty()) empty = tr("No venues match.");
    else if (n == 0) {
        const QString where = whereText(tr("%1 has no saved venues yet."), tr("You have no saved venues yet."));
        empty = (where.isEmpty() ? tr("No saved venues here yet.") : where) + QLatin1Char(' ') +
                tr("Send one from the Venue library with Save to Server…, or make one on the web.");
    }
    venuesPage_.empty->setText(empty);
    venuesPage_.empty->setVisible(!empty.isEmpty());
}

void ServerWindow::rebuildModules() {
    clearPage(modulesPage_);
    const bool ready = library_.state() == ServerLibrary::State::Ready;
    int n = 0;
    for (const sync::ServerModule& m : library_.modules()) {
        const sync::ItemOwner owner = ownerOf(m.owner);
        const QString who = whoseText(m.credit, owner);
        if (!shown(owner.key, { m.title, who })) continue;
        ++n;
        RowSpec r;
        r.objectName = QStringLiteral("moduleRow:") + m.id;
        r.title = m.title;
        r.placeholder = QStringLiteral("▣");
        // List rows ask for the small picture (servers without one send the full one).
        if (const QString path = m.thumbnailPath(); !path.isEmpty()) r.picture = path + QStringLiteral("&size=small");
        r.lines << who
                << QStringList{ tr("version %1").arg(m.shownVersion()), whenText(tr("updated %1"), m.updatedAt) }
                       .filter(QRegularExpression(QStringLiteral(".+")))
                       .join(QStringLiteral(" · "));
        const QString id = m.id;
        if (id == library_.editingModule()) {
            r.pill = tr("Editing now");
        } else {
            const bool busy = !library_.inserting().isEmpty();
            r.primary = { library_.inserting() == id ? tr("Adding…") : tr("Insert into this layout"),
                          QStringLiteral("insertModule"), [this, id] { emit library_.insertRequested(id); }, !busy,
                          tr("Add its parts to the layout that's open, as one module") };
            r.more << Action{ tr("Open to change it"), QStringLiteral("openModule"),
                              [this, id] { emit library_.openRequested(id); }, m.canEdit(),
                              m.canEdit() ? QString() : tr("You can look at this module, not change it.") };
            r.more << Action{ tr("Save a copy locally"), QStringLiteral("saveModuleCopy"),
                              [this, id] { emit saveModuleCopyRequested(id); }, true,
                              tr("Keep a copy in the Module library folder on this computer") };
            r.more << returnActions(QStringLiteral("modules"), tr("module"), id, m.title, m.credit);
            addShareAction(r, QStringLiteral("module"), id, m.title, m.canDelete());
            if (m.canDelete()) r.more << Action{ tr("Delete…"), QStringLiteral("deleteModule"), [this, m] { deleteModule(m); }, true, {} };
        }
        makeRow(modulesPage_, r);
    }
    {
        QStringList onServer;
        for (const auto& m : library_.modules()) onServer << m.title;
        addLocalRows(Tab::Modules, onServer);
    }
    QString empty;
    if (!ready) empty = {};
    else if (n == 0 && !search_->text().trimmed().isEmpty()) empty = tr("No modules match.");
    else if (n == 0) {
        const QString where = whereText(tr("%1 has no saved modules yet."), tr("You have no saved modules yet."));
        empty = (where.isEmpty() ? tr("No saved modules here yet.") : where) + QLatin1Char(' ') +
                tr("Make a module in a layout, then choose Save to Module library… from its menu.");
    }
    modulesPage_.empty->setText(empty);
    modulesPage_.empty->setVisible(!empty.isEmpty());
}

void ServerWindow::rebuildParts() {
    clearPage(partsPage_);
    QList<sync::ServerPart> list = parts_;
    std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.name().localeAwareCompare(b.name()) < 0; });
    int n = 0;
    for (const sync::ServerPart& p : std::as_const(list)) {
        const sync::ItemOwner owner = ownerOf(p.owner);
        const QString who = whoseText(p.credit, owner);
        if (!shown(owner.key, { p.name(), p.partNumber, who })) continue;
        ++n;
        RowSpec r;
        r.objectName = QStringLiteral("partRow:") + p.id;
        r.title = p.name();
        r.placeholder = QStringLiteral("◧");
        r.picture = p.spritePath();
        r.lines << who
                << QStringList{ p.partNumber, whenText(tr("updated %1"), p.updatedAt) }
                       .filter(QRegularExpression(QStringLiteral(".+")))
                       .join(QStringLiteral(" · "));
        const QString number = p.partNumber;
        if (hasPart && hasPart(number)) {
            r.primary = { tr("Show in parts list"), QStringLiteral("showPart"), [this, number] { emit showPartRequested(number); },
                          true, tr("It's in your parts: find it in the Parts panel") };
        } else {
            r.primary = { tr("Add to my parts"), QStringLiteral("addPart"), [this, number] { emit addPartRequested(number); },
                          true, tr("Download it into your parts, to use in any layout") };
        }
        r.more << returnActions(QStringLiteral("custom-parts"), tr("part"), p.id, r.title, p.credit);
        addShareAction(r, QStringLiteral("part"), p.id, r.title, p.role == QLatin1String("owner"));
        makeRow(partsPage_, r);
    }
    {
        // A local part counts as there when the server has its number.
        QStringList onServer;
        for (const auto& p : parts_) onServer << p.partNumber << p.name();
        addLocalRows(Tab::Parts, onServer);
    }
    QString empty;
    if (!partsWhy_.isEmpty()) empty = partsWhy_;
    else if (!partsIn_) empty = tr("Loading…");
    else if (n == 0 && !search_->text().trimmed().isEmpty()) empty = tr("No parts match.");
    else if (n == 0) {
        const QString where = whereText(tr("%1 has no custom parts yet."), tr("You have no custom parts yet."));
        empty = (where.isEmpty() ? tr("No custom parts here yet.") : where) + QLatin1Char(' ') +
                tr("Send yours with Upload to server…, or upload one on the web.");
    }
    partsPage_.empty->setText(empty);
    partsPage_.empty->setVisible(!empty.isEmpty());
}

ServerWindow::Page& ServerWindow::page(Tab tab) {
    switch (tab) {
    case Tab::Venues: return venuesPage_;
    case Tab::Modules: return modulesPage_;
    case Tab::Parts: return partsPage_;
    default: return layoutsPage_;
    }
}

void ServerWindow::setLocalItems(Tab tab, const QList<LocalItem>& items) {
    page(tab).local = items;
    switch (tab) {
    case Tab::Layouts: rebuildLayouts(); break;
    case Tab::Venues: rebuildVenues(); break;
    case Tab::Modules: rebuildModules(); break;
    case Tab::Parts: rebuildParts(); break;
    default: break;
    }
}

QStringList ServerWindow::localShown(Tab tab) const {
    QStringList out;
    const Page& p = const_cast<ServerWindow*>(this)->page(tab);
    for (auto it = p.rows.cbegin(); it != p.rows.cend(); ++it)
        if (it.key().startsWith(QLatin1String("localRow:")))
            out << it.value()->findChild<QLabel*>(QStringLiteral("rowTitle"))->text();
    out.sort();
    return out;
}

void ServerWindow::addLocalRows(Tab tab, const QStringList& serverTitles) {
    Page& p = page(tab);
    if (library_.state() != ServerLibrary::State::Ready) return;
    bool header = false;
    for (const LocalItem& item : std::as_const(p.local)) {
        // Already there under its name: not offered again.
        if (std::any_of(serverTitles.begin(), serverTitles.end(),
                        [&](const QString& t) { return t.compare(item.title, Qt::CaseInsensitive) == 0; }))
            continue;
        if (!shown(sync::kShowMine, { item.title, item.detail })) continue;
        if (!header) {
            auto* h = libraryGroupHeader(tr("On this computer, not on the server yet"), p.host);
            h->setObjectName(QStringLiteral("localHeader"));
            p.rows.insert(QStringLiteral("localHeader"), h);
            p.list->insertWidget(p.list->count() - 2, h);
            header = true;
        }
        RowSpec r;
        r.objectName = QStringLiteral("localRow:") + item.id;
        r.title = item.title;
        r.placeholder = QStringLiteral("↑");
        r.lines << item.detail << tr("Not on the server yet");
        const QString id = item.id;
        r.primary = { tr("Send…"), QStringLiteral("sendLocal"), [this, tab, id] { emit sendRequested(tab, id); }, true,
                      tr("Put a copy on %1").arg(library_.label()) };
        makeRow(p, r);
    }
}

bool ServerWindow::hasFeature(const QString& feature) const {
    const sync::ServerList list = sync::ServerList::load();
    const sync::ServerEntry* e = list.find(library_.server());
    return e && e->features && e->features->contains(feature);
}

QString ServerWindow::catalogLine(const QString& kind, const QString& sourceId) const {
    for (const sync::MyCatalogItem& i : shared_) {
        if (i.kind != kind || i.sourceId != sourceId) continue;
        const QString s = i.status;
        QString label = s == QLatin1String("public") ? (i.pendingVersion ? tr("Public · update in review") : tr("Public"))
                        : s == QLatin1String("in_review")   ? tr("In review")
                        : s == QLatin1String("declined")    ? tr("Declined")
                        : s == QLatin1String("unpublished") ? tr("Unpublished")
                        : s == QLatin1String("withdrawn")   ? tr("Withdrawn")
                                                            : s;
        if (!i.reason.isEmpty() && s != QLatin1String("public")) label += QStringLiteral(": ") + i.reason;
        return tr("In the catalog: %1").arg(label);
    }
    return {};
}

void ServerWindow::addShareAction(RowSpec& r, const QString& kind, const QString& sourceId, const QString& title, bool mayShare) {
    const QString line = catalogLine(kind, sourceId);
    if (!line.isEmpty()) r.lines << line;
    if (!mayShare || !hasFeature(QStringLiteral("catalogShare")) || !library_.catalog().on(kind)) return;
    const bool isUpdate = !line.isEmpty();
    sync::CatalogShare share;
    share.kind = kind;
    share.sourceId = sourceId;
    share.title = title;
    r.more << Action{ isUpdate ? tr("Publish this update…") : tr("Share to the public catalog…"), QStringLiteral("shareToCatalog"),
                      [this, share, isUpdate] { emit shareRequested(share, isUpdate, QString()); }, true,
                      tr("A copy goes to the public catalog, for anyone to add to their layouts") };
}

bool ServerWindow::canDownloadNative() const {
    const sync::ServerList list = sync::ServerList::load();
    const sync::ServerEntry* e = list.find(library_.server());
    return e && e->features && e->features->contains(QStringLiteral("layoutDownload"));
}

bool ServerWindow::ask(const ConfirmOptions& o) { return confirm && confirm(o); }

void ServerWindow::downloadLayout(const sync::LayoutEntry& l) {
    if (!askSavePath) return;
    const QString title = l.title.isEmpty() ? tr("Untitled Layout") : l.title;
    const QString path = askSavePath(title, canDownloadNative());
    if (path.isEmpty()) return;
    const bool native = path.endsWith(QStringLiteral(".bld-layout"), Qt::CaseInsensitive);
    emit message(tr("Downloading “%1”…").arg(title));
    library_.api().layoutFile(l.id, native, [this, path, title](const QByteArray& bytes) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) {
            QMessageBox::warning(this, tr("Download a copy"), tr("Couldn't save %1: %2").arg(QDir::toNativeSeparators(path), f.errorString()));
            return;
        }
        emit message(tr("Saved a copy of “%1” as %2").arg(title, QFileInfo(path).fileName()));
    }, [this](const sync::ServerRefusal& r) {
        QMessageBox::warning(this, tr("Download a copy"), ServerLibrary::refusalText(r));
    });
}

void ServerWindow::deleteLayout(const sync::LayoutEntry& l) {
    const QString title = l.title.isEmpty() ? tr("Untitled Layout") : l.title;
    if (!ask(ConfirmDialog::deleteOptions(title, ConfirmDialog::layoutWording()))) return;
    library_.api().deleteLayout(l.id, [this, title, id = l.id] {
        emit message(tr("Deleted “%1”").arg(title));
        layouts_.erase(std::remove_if(layouts_.begin(), layouts_.end(), [&](const auto& e) { return e.id == id; }), layouts_.end());
        rebuildLayouts();
        hint(QJsonObject{ { QStringLiteral("kind"), QStringLiteral("layout") } });
    }, [this](const sync::ServerRefusal& r) {
        QMessageBox::warning(this, tr("Delete layout"), ServerLibrary::refusalText(r));
    });
}

void ServerWindow::deleteVenue(const sync::VenueEntry& v) {
    if (!ask(ConfirmDialog::deleteOptions(v.name, ConfirmDialog::venueWording()))) return;
    library_.api().deleteVenue(v.id, [this, name = v.name, id = v.id] {
        emit message(tr("Deleted “%1”").arg(name));
        venues_.erase(std::remove_if(venues_.begin(), venues_.end(), [&](const auto& e) { return e.id == id; }), venues_.end());
        rebuildVenues();
        hint(QJsonObject{ { QStringLiteral("kind"), QStringLiteral("venue") } });
    }, [this](const sync::ServerRefusal& r) {
        QMessageBox::warning(this, tr("Delete venue"), ServerLibrary::refusalText(r));
    });
}

void ServerWindow::deleteModule(const sync::ServerModule& m) {
    if (!ask(ConfirmDialog::deleteOptions(m.title, ConfirmDialog::moduleWording()))) return;
    library_.api().deleteModule(m.id, [this, title = m.title] {
        emit message(tr("Deleted “%1”").arg(title));
        library_.refresh();
    }, [this](const sync::ServerRefusal& r) {
        QMessageBox::warning(this, tr("Delete module"), ServerLibrary::refusalText(r));
    });
}

void ServerWindow::giveOrTakeBack(const QString& kindPath, const QString& word, const QString& id, const QString& title,
                                  const sync::Credit& credit, bool give) {
    if (!ask(returnOptions(word, title, credit, give))) return;
    library_.api().returnToAuthor(
        kindPath, id, give,
        [this, kindPath, title, author = credit.authorName, give](const QString&) {
            emit message(give ? tr("Gave “%1” back to %2; the club kept its own copy.").arg(title, author)
                              : tr("“%1” is yours again; the club kept its own copy.").arg(title));
            if (kindPath == QLatin1String("modules")) library_.refresh();
            else
                hint(QJsonObject{ { QStringLiteral("kind"), kindPath == QLatin1String("layouts")  ? QStringLiteral("layout")
                                                            : kindPath == QLatin1String("venues") ? QStringLiteral("venue")
                                                                                                  : QStringLiteral("custom-part") } });
        },
        [this, give](const sync::ServerRefusal& r) {
            QMessageBox::warning(this, give ? tr("Give back") : tr("Take back"), ServerLibrary::refusalText(r));
        });
}

}  // namespace bld::ui
