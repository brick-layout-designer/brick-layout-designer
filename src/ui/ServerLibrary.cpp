#include "ServerLibrary.h"

#include "../sync/ServerList.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

namespace bld::ui {

namespace {

constexpr int kRowPicture = 48;

// A small picture for a row: the server's, else a plain placeholder.
QLabel* pictureLabel(QWidget* parent) {
    auto* l = new QLabel(parent);
    l->setObjectName(QStringLiteral("rowPicture"));
    l->setFixedSize(kRowPicture, kRowPicture);
    l->setAlignment(Qt::AlignCenter);
    l->setProperty("libraryPicture", true);
    return l;
}

void showPicture(QLabel* label, const QImage& img) {
    if (img.isNull()) return;
    const qreal dpr = label->devicePixelRatioF();
    QPixmap px = QPixmap::fromImage(img.scaled(QSize(kRowPicture, kRowPicture) * dpr, Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation));
    px.setDevicePixelRatio(dpr);
    label->setPixmap(px);
}

QLabel* mutedLabel(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setProperty("muted", true);
    l->setWordWrap(true);
    return l;
}

QLabel* groupHeader(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setProperty("groupHeader", true);
    return l;
}

QFrame* rowFrame(QWidget* parent) {
    auto* f = new QFrame(parent);
    f->setProperty("libraryRow", true);
    return f;
}

}  // namespace

// ---------- ServerLibrary --------------------------------------------------

ServerLibrary::ServerLibrary(QObject* parent) : QObject(parent) {}

void ServerLibrary::setServer(const QUrl& url, const QString& label, const QString& token) {
    const bool same = url == server_ && token == api_.token();
    server_ = url;
    label_ = label;
    api_.setBase(url);
    api_.setToken(token);
    if (same && state_ != State::NoServer) return;
    ++generation_;
    modules_.clear();
    orgs_.clear();
    catalog_ = {};
    waiting_.clear();
    if (!url.isValid()) {
        setState(State::NoServer);
    } else if (token.isEmpty()) {
        setState(State::SignedOut);
    } else {
        refresh();
        return;
    }
    emit changed();
}

QString ServerLibrary::stateText() const {
    switch (state_) {
    case State::NoServer:
        return tr("Connect to a server to see the modules you and your clubs saved there, and the catalog.");
    case State::SignedOut:
        return tr("Sign in to %1 to see your modules, your clubs' modules and the catalog.").arg(label_);
    case State::Loading:
        return tr("Loading…");
    case State::Offline:
        return tr("Can't reach %1 right now. Your modules on this computer still work.").arg(label_);
    case State::Refused:
        return why_;
    case State::Ready:
        break;
    }
    return {};
}

void ServerLibrary::setState(State s, const QString& why) {
    if (s == state_ && why == why_) return;
    state_ = s;
    why_ = why;
    emit stateChanged();
}

QString ServerLibrary::refusalText(const sync::ServerRefusal& r) {
    const QString code = r.code;
    if (code == QLatin1String("forbidden"))
        return tr("You can't do that with this module. Ask its owner, or your club's admins, for editor rights.");
    if (code == QLatin1String("not_an_org_member")) return tr("You're not a member of that club.");
    if (code == QLatin1String("only_club_admins_can_add"))
        return tr("In this club, only admins and managers can add modules and parts.");
    if (code == QLatin1String("catalog_off")) return tr("The catalog isn't turned on on this server.");
    if (code == QLatin1String("not_found")) return tr("It isn't there any more. Someone may have deleted it.");
    if (code == QLatin1String("insufficient_scope") || code == QLatin1String("token_not_allowed"))
        return tr("This sign-in can't do that. Sign in again (File › Servers…) to give the app the rights it needs.");
    if (code.startsWith(QLatin1String("demo_account")))
        return tr("The demo account can't do that.");
    if (r.status == 401) return tr("Your sign-in has run out. Sign in again.");
    return sync::describe(r);
}

void ServerLibrary::fail(const sync::ServerRefusal& r) {
    if (r.status == 0) {
        setState(State::Offline);
    } else if (r.status == 401 || r.code == QLatin1String("invalid_token")) {
        // The token was revoked or ran out: back to "Sign in".
        api_.setToken({});
        setState(State::SignedOut);
    } else {
        setState(State::Refused, refusalText(r));
    }
}

void ServerLibrary::refresh() {
    if (!server_.isValid() || api_.token().isEmpty()) return;
    const int gen = ++generation_;
    if (state_ != State::Ready) setState(State::Loading);
    auto pending = std::make_shared<int>(3);
    auto failed = std::make_shared<bool>(false);
    auto settle = [this, gen, pending, failed] {
        if (gen != generation_ || --*pending > 0 || *failed) return;
        setState(State::Ready);
        emit changed();
    };
    auto onFail = [this, gen, failed](const sync::ServerRefusal& r) {
        if (gen != generation_ || *failed) return;
        *failed = true;
        fail(r);
        emit changed();
    };
    api_.listModules([this, gen, settle](const QList<sync::ServerModule>& list) {
        if (gen != generation_) return;
        modules_ = list;
        settle();
    }, onFail);
    api_.orgs([this, gen, settle](const QList<sync::OrgEntry>& list) {
        if (gen != generation_) return;
        orgs_ = list;
        settle();
    }, onFail);
    // A server without the catalog (or before it) simply has none.
    api_.catalogSettings([this, gen, settle](const sync::CatalogSettings& s) {
        if (gen != generation_) return;
        catalog_ = s;
        settle();
    }, [this, gen, settle](const sync::ServerRefusal& r) {
        if (gen != generation_) return;
        if (r.status == 0) {
            if (state_ != State::Offline) fail(r);
            return;
        }
        catalog_ = {};
        settle();
    });
}

const sync::ServerModule* ServerLibrary::module(const QString& id) const {
    for (const auto& m : modules_)
        if (m.id == id) return &m;
    return nullptr;
}

QList<sync::ServerModule> ServerLibrary::editableModules() const {
    QList<sync::ServerModule> out;
    for (const auto& m : modules_)
        if (m.canEdit()) out << m;
    return out;
}

void ServerLibrary::setEditingModule(const QString& id) {
    if (editing_ == id) return;
    editing_ = id;
    emit editingChanged();
}

void ServerLibrary::setInserting(const QString& id) {
    if (inserting_ == id) return;
    inserting_ = id;
    emit editingChanged();
}

QString ServerLibrary::pictureCacheDir() const {
    QString root = cacheRoot_;
    if (root.isEmpty()) {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (base.isEmpty()) return {};
        root = base + QStringLiteral("/server-pictures");
    }
    return root + QLatin1Char('/') + sync::ServerList::folderName(server_);
}

void ServerLibrary::picture(const QString& path, QObject* context, std::function<void(const QImage&)> done) {
    if (path.isEmpty() || !server_.isValid()) return;
    const QString dir = pictureCacheDir();
    const QString file = dir.isEmpty() ? QString()
                                       : dir + QLatin1Char('/') +
                                             QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex()) +
                                             QStringLiteral(".img");
    if (!file.isEmpty() && QFile::exists(file)) {
        const QImage img(file);
        if (!img.isNull()) {
            done(img);
            return;
        }
    }
    // Rows come and go while pictures load: only the ones still there get theirs.
    auto alive = std::make_shared<bool>(true);
    connect(context, &QObject::destroyed, this, [alive] { *alive = false; });
    auto deliver = [alive, done = std::move(done)](const QImage& img) {
        if (*alive) done(img);
    };
    auto& queue = waiting_[path];
    queue << deliver;
    if (queue.size() > 1) return;  // already on its way
    const int gen = generation_;
    api_.picture(path, [this, gen, path, file, dir](const QByteArray& bytes) {
        if (gen != generation_) return;
        QImage img;
        img.loadFromData(bytes);
        if (!img.isNull() && !file.isEmpty() && QDir().mkpath(dir)) {
            QFile f(file);
            if (f.open(QIODevice::WriteOnly)) f.write(bytes);
        }
        const auto callbacks = waiting_.take(path);
        for (const auto& cb : callbacks) cb(img);
    }, [this, gen, path](const sync::ServerRefusal&) {
        if (gen != generation_) return;
        // No picture yet: the placeholder stays.
        waiting_.remove(path);
    });
}

// ---------- LibraryStatusLine ----------------------------------------------

LibraryStatusLine::LibraryStatusLine(ServerLibrary& library, QWidget* parent) : QWidget(parent), library_(library) {
    setObjectName(QStringLiteral("libraryStatus"));
    auto* row = new QVBoxLayout(this);
    row->setContentsMargins(8, 8, 8, 8);
    text_ = new QLabel(this);
    text_->setObjectName(QStringLiteral("libraryStatusText"));
    text_->setWordWrap(true);
    button_ = new QPushButton(this);
    button_->setObjectName(QStringLiteral("libraryStatusButton"));
    button_->setProperty("accent", true);
    row->addWidget(text_);
    row->addWidget(button_, 0, Qt::AlignLeft);
    connect(button_, &QPushButton::clicked, this, [this] {
        switch (library_.state()) {
        case ServerLibrary::State::NoServer: emit library_.addServerRequested(); break;
        case ServerLibrary::State::SignedOut: emit library_.signInRequested(); break;
        default: library_.refresh(); break;
        }
    });
    showState();
}

void LibraryStatusLine::showState() {
    using S = ServerLibrary::State;
    const S s = library_.state();
    setVisible(s != S::Ready);
    text_->setText(library_.stateText());
    switch (s) {
    case S::NoServer: button_->setText(tr("Add a server…")); break;
    case S::SignedOut: button_->setText(tr("Sign in")); break;
    case S::Offline:
    case S::Refused: button_->setText(tr("Try again")); break;
    default: break;
    }
    button_->setVisible(s != S::Loading && s != S::Ready);
}

// ---------- ServerModulesTab -----------------------------------------------

ServerModulesTab::ServerModulesTab(ServerLibrary& library, QWidget* parent) : QWidget(parent), library_(library) {
    setObjectName(QStringLiteral("serverModulesTab"));
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(2, 4, 2, 2);
    col->setSpacing(4);
    status_ = new LibraryStatusLine(library_, this);
    col->addWidget(status_);
    filter_ = new QLineEdit(this);
    filter_->setObjectName(QStringLiteral("serverModulesFilter"));
    filter_->setPlaceholderText(tr("Filter modules…"));
    filter_->setClearButtonEnabled(true);
    col->addWidget(filter_);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    listHost_ = new QWidget(scroll);
    list_ = new QVBoxLayout(listHost_);
    list_->setContentsMargins(0, 0, 0, 0);
    list_->setSpacing(4);
    list_->addStretch(1);
    scroll->setWidget(listHost_);
    // A finger flicks the list up and down.
    QScroller::grabGesture(scroll->viewport(), QScroller::TouchGesture);
    col->addWidget(scroll, 1);

    askName = [this](const sync::ServerModule& m) {
        bool ok = false;
        const QString t = QInputDialog::getText(this, tr("Rename module"), tr("Module name"), QLineEdit::Normal, m.title, &ok);
        return ok ? t.trimmed() : QString();
    };
    confirmDelete = [this](const sync::ServerModule& m) {
        return QMessageBox::question(this, tr("Delete module"), tr("Delete module \"%1\"?").arg(m.title)) == QMessageBox::Yes;
    };

    connect(filter_, &QLineEdit::textChanged, this, &ServerModulesTab::rebuild);
    connect(&library_, &ServerLibrary::changed, this, &ServerModulesTab::rebuild);
    connect(&library_, &ServerLibrary::editingChanged, this, &ServerModulesTab::rebuild);
    connect(&library_, &ServerLibrary::stateChanged, this, &ServerModulesTab::rebuild);
    rebuild();
}

QWidget* ServerModulesTab::row(const QString& id) const { return rows_.value(id); }

void ServerModulesTab::rebuild() {
    status_->showState();
    // Rows may be rebuilt from one of their own buttons: let them go later.
    while (list_->count() > 1) {
        QLayoutItem* it = list_->takeAt(0);
        if (QWidget* w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }
    rows_.clear();
    const bool ready = library_.state() == ServerLibrary::State::Ready;
    filter_->setVisible(ready);
    if (!ready) return;

    const QString f = filter_->text().trimmed();
    QList<sync::ServerModule> shown;
    for (const auto& m : library_.modules())
        if (f.isEmpty() || m.title.contains(f, Qt::CaseInsensitive)) shown << m;
    int at = 0;
    if (shown.isEmpty()) {
        auto* empty = mutedLabel(f.isEmpty() ? tr("No saved modules yet. Pick some parts, then Modules › Save Selection as Module…")
                                             : tr("No modules match."),
                                 listHost_);
        empty->setObjectName(QStringLiteral("serverModulesEmpty"));
        list_->insertWidget(at++, empty);
        return;
    }
    // Yours first, then each club (in the order the server lists them), then shared with you.
    QStringList groups;
    QHash<QString, QString> names;
    for (const auto& m : shown) {
        const QString key = m.owner.isClub() ? m.owner.id : QStringLiteral("me");
        if (!groups.contains(key)) groups << key;
        if (m.owner.isClub()) names.insert(key, m.owner.name.isEmpty() ? tr("A club") : m.owner.name);
    }
    groups.removeAll(QStringLiteral("me"));
    groups.prepend(QStringLiteral("me"));
    for (const QString& g : std::as_const(groups)) {
        QList<sync::ServerModule> inGroup;
        for (const auto& m : shown)
            if ((m.owner.isClub() ? m.owner.id : QStringLiteral("me")) == g) inGroup << m;
        if (inGroup.isEmpty()) continue;
        list_->insertWidget(at++, groupHeader(g == QLatin1String("me") ? tr("Yours") : names.value(g), listHost_));
        for (const auto& m : std::as_const(inGroup)) {
            QWidget* r = makeRow(m);
            rows_.insert(m.id, r);
            list_->insertWidget(at++, r);
        }
    }
}

QWidget* ServerModulesTab::makeRow(const sync::ServerModule& m) {
    // Narrow docks: the picture, then the words with the button under them.
    QFrame* frame = rowFrame(listHost_);
    frame->setObjectName(QStringLiteral("moduleRow:") + m.id);
    auto* h = new QHBoxLayout(frame);
    h->setContentsMargins(6, 6, 6, 6);
    h->setSpacing(8);
    QLabel* pic = pictureLabel(frame);
    h->addWidget(pic, 0, Qt::AlignTop);
    // List rows ask for the small picture (servers without one send the full one).
    if (const QString path = m.thumbnailPath(); !path.isEmpty())
        library_.picture(path + QStringLiteral("&size=small"), pic, [pic](const QImage& img) { showPicture(pic, img); });

    auto* text = new QVBoxLayout();
    text->setSpacing(2);
    auto* top = new QHBoxLayout();
    auto* title = new QLabel(m.title, frame);
    title->setObjectName(QStringLiteral("rowTitle"));
    title->setProperty("rowTitle", true);
    title->setWordWrap(true);
    top->addWidget(title, 1);
    text->addLayout(top);
    QString sub = tr("v%1").arg(m.shownVersion());
    if (m.updatedAt.isValid()) sub += QStringLiteral(" · ") + QLocale().toString(m.updatedAt.date(), QLocale::ShortFormat);
    auto* subtitle = mutedLabel(sub, frame);
    subtitle->setObjectName(QStringLiteral("rowSubtitle"));
    text->addWidget(subtitle);
    h->addLayout(text, 1);

    if (m.id == library_.editingModule()) {
        auto* pill = new QLabel(tr("Editing now"), frame);
        pill->setObjectName(QStringLiteral("editingNow"));
        pill->setProperty("pill", true);
        text->addWidget(pill, 0, Qt::AlignLeft);
        return frame;
    }
    const bool inModule = !library_.editingModule().isEmpty();
    auto* add = new QPushButton(library_.inserting() == m.id ? tr("Adding…")
                                : inModule                   ? tr("Add to this module")
                                                             : tr("Add to layout"),
                                frame);
    add->setObjectName(QStringLiteral("insertModule"));
    add->setEnabled(library_.inserting().isEmpty());
    const QString id = m.id;
    connect(add, &QPushButton::clicked, this, [this, id] { emit library_.insertRequested(id); });
    text->addWidget(add, 0, Qt::AlignLeft);

    auto* more = new QToolButton(frame);
    more->setObjectName(QStringLiteral("moduleMore"));
    more->setText(QStringLiteral("⋯"));
    more->setToolTip(tr("More for %1").arg(m.title));
    more->setAccessibleName(tr("More for %1").arg(m.title));
    more->setPopupMode(QToolButton::InstantPopup);
    more->setProperty("rowMore", true);
    auto* menu = new QMenu(more);
    QAction* open = menu->addAction(tr("Open to change it"));
    open->setObjectName(QStringLiteral("openModule"));
    open->setEnabled(m.canEdit());
    if (!m.canEdit()) open->setToolTip(tr("You can look at this module, not change it."));
    connect(open, &QAction::triggered, this, [this, id] { emit library_.openRequested(id); });
    QAction* ren = menu->addAction(tr("Rename…"));
    ren->setObjectName(QStringLiteral("renameModule"));
    connect(ren, &QAction::triggered, this, [this, id] { rename(id); });
    QAction* del = menu->addAction(tr("Delete…"));
    del->setObjectName(QStringLiteral("deleteModule"));
    connect(del, &QAction::triggered, this, [this, id] { remove(id); });
    menu->setToolTipsVisible(true);
    more->setMenu(menu);
    top->addWidget(more, 0, Qt::AlignTop);
    return frame;
}

void ServerModulesTab::rename(const QString& id) {
    const sync::ServerModule* m = library_.module(id);
    if (!m || !askName) return;
    const QString title = askName(*m);
    if (title.isEmpty() || title == m->title) return;
    library_.api().renameModule(id, title, [this, title] {
        emit library_.message(tr("Renamed to \"%1\"").arg(title));
        library_.refresh();
    }, [this](const sync::ServerRefusal& r) {
        QMessageBox::warning(this, tr("Rename module"), ServerLibrary::refusalText(r));
    });
}

void ServerModulesTab::remove(const QString& id) {
    const sync::ServerModule* m = library_.module(id);
    if (!m || !confirmDelete || !confirmDelete(*m)) return;
    const QString title = m->title;
    library_.api().deleteModule(id, [this, title] {
        emit library_.message(tr("Deleted \"%1\"").arg(title));
        library_.refresh();
    }, [this](const sync::ServerRefusal& r) {
        QMessageBox::warning(this, tr("Delete module"), ServerLibrary::refusalText(r));
    });
}

// ---------- CatalogTab -----------------------------------------------------

QString itemsText(int n) { return n == 1 ? QObject::tr("1 item") : QObject::tr("%1 items").arg(n); }
QString usesText(int n) { return n == 1 ? QObject::tr("1 use") : QObject::tr("%1 uses").arg(n); }

QString collectionAddText(const sync::CollectionAddResult& r) {
    QStringList parts;
    parts << (r.added == 0 ? QObject::tr("Nothing new to add") : QObject::tr("Added %1").arg(itemsText(r.added)));
    if (r.skipped) parts << QObject::tr("already had %1").arg(r.skipped);
    if (r.failed) parts << QObject::tr("%1 couldn't be added").arg(r.failed);
    return parts.join(QStringLiteral(" · "));
}

CatalogTab::CatalogTab(ServerLibrary& library, QWidget* parent) : QWidget(parent), library_(library) {
    setObjectName(QStringLiteral("catalogTab"));
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(2, 4, 2, 2);
    col->setSpacing(4);
    status_ = new LibraryStatusLine(library_, this);
    col->addWidget(status_);

    auto* seg = new QFrame(this);
    seg->setObjectName(QStringLiteral("Segmented"));
    kinds_ = seg;
    auto* segRow = new QHBoxLayout(seg);
    segRow->setContentsMargins(2, 2, 2, 2);
    segRow->setSpacing(2);
    const QList<std::pair<Kind, QString>> kinds{ { Kind::Modules, tr("Modules") },
                                                  { Kind::Parts, tr("Parts") },
                                                  { Kind::Collections, tr("Collections") } };
    for (const auto& [k, label] : kinds) {
        auto* b = new QPushButton(label, seg);
        b->setCheckable(true);
        b->setObjectName(QStringLiteral("catalogKind%1").arg(static_cast<int>(k)));
        const Kind kind = k;
        connect(b, &QPushButton::clicked, this, [this, kind] { setKind(kind); });
        segRow->addWidget(b);
        kindButtons_ << b;
    }
    col->addWidget(seg);

    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("catalogSearch"));
    search_->setPlaceholderText(tr("Search the catalog"));
    search_->setClearButtonEnabled(true);
    col->addWidget(search_);
    back_ = new QPushButton(tr("‹ All collections"), this);
    back_->setObjectName(QStringLiteral("catalogBack"));
    back_->setFlat(true);
    back_->setVisible(false);
    col->addWidget(back_, 0, Qt::AlignLeft);
    note_ = mutedLabel(QString(), this);
    note_->setObjectName(QStringLiteral("catalogNote"));
    col->addWidget(note_);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* host = new QWidget(scroll);
    list_ = new QVBoxLayout(host);
    list_->setContentsMargins(0, 0, 0, 0);
    list_->setSpacing(4);
    list_->addStretch(1);
    scroll->setWidget(host);
    QScroller::grabGesture(scroll->viewport(), QScroller::TouchGesture);
    col->addWidget(scroll, 1);

    chooseOwner = [this](const QString& title) -> std::optional<QString> {
        QStringList names{ tr("Me") };
        for (const auto& o : library_.orgs()) names << o.name;
        bool ok = false;
        const QString picked = QInputDialog::getItem(this, title, tr("Add to"), names, 0, false, &ok);
        if (!ok) return std::nullopt;
        const int i = static_cast<int>(names.indexOf(picked));
        return i <= 0 ? QString() : library_.orgs().at(i - 1).slug;
    };

    connect(search_, &QLineEdit::textChanged, this, [this] { reload(); });
    connect(back_, &QPushButton::clicked, this, [this] {
        openCollection_.clear();
        reload();
    });
    connect(&library_, &ServerLibrary::stateChanged, this, [this] { reload(); });
    connect(&library_, &ServerLibrary::changed, this, [this] {
        // The catalog settings may have changed: the kinds that are on.
        status_->showState();
        reload();
    });
    setKind(Kind::Modules);
}

QWidget* CatalogTab::row(const QString& id) const { return rows_.value(id); }

void CatalogTab::setKind(Kind kind) {
    kind_ = kind;
    openCollection_.clear();
    added_.clear();
    for (int i = 0; i < kindButtons_.size(); ++i) kindButtons_[i]->setChecked(i == static_cast<int>(kind));
    reload();
}

void CatalogTab::clearRows() {
    while (list_->count() > 1) {
        QLayoutItem* it = list_->takeAt(0);
        if (QWidget* w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }
    rows_.clear();
}

void CatalogTab::reload() {
    status_->showState();
    clearRows();
    const int gen = ++generation_;
    const bool ready = library_.state() == ServerLibrary::State::Ready;
    const auto& on = library_.catalog();
    kindButtons_[0]->setVisible(on.modules);
    kindButtons_[1]->setVisible(on.parts);
    kindButtons_[2]->setVisible(on.modules || on.parts);
    kinds_->setVisible(ready);
    search_->setVisible(ready && kind_ != Kind::Collections);
    back_->setVisible(ready && !openCollection_.isEmpty());
    note_->clear();
    if (!ready) return;
    if (!on.modules && !on.parts) {
        note_->setText(tr("The catalog isn't turned on on this server. Its admins can turn it on."));
        return;
    }
    if ((kind_ == Kind::Modules && !on.modules) || (kind_ == Kind::Parts && !on.parts)) {
        setKind(on.modules ? Kind::Modules : Kind::Parts);
        return;
    }
    if (added_.isEmpty()) note_->setText(tr("Loading…"));
    auto failed = [this, gen](const sync::ServerRefusal& r) {
        if (gen != generation_) return;
        note_->setText(ServerLibrary::refusalText(r));
    };
    if (!openCollection_.isEmpty()) {
        library_.api().collectionItems(openCollection_, [this, gen](const QList<sync::CatalogItem>& items) {
            if (gen == generation_) showItems(items);
        }, failed);
    } else if (kind_ == Kind::Collections) {
        library_.api().collections([this, gen](const QList<sync::CatalogCollection>& list) {
            if (gen == generation_) showCollections(list);
        }, failed);
    } else {
        library_.api().catalogItems(kind_ == Kind::Modules ? QStringLiteral("module") : QStringLiteral("part"),
                                    search_->text(), [this, gen](const QList<sync::CatalogItem>& items) {
            if (gen == generation_) showItems(items);
        }, failed);
    }
}

void CatalogTab::showItems(const QList<sync::CatalogItem>& items) {
    clearRows();
    note_->setText(items.isEmpty() ? (search_->text().trimmed().isEmpty() ? tr("Nothing in the catalog yet.")
                                                                         : tr("Nothing matches."))
                   : kind_ == Kind::Modules && openCollection_.isEmpty()
                       ? tr("Adding copies it into your modules first.")
                       : QString());
    int at = 0;
    for (const auto& it : items) {
        QWidget* r = itemRow(it);
        rows_.insert(it.id, r);
        list_->insertWidget(at++, r);
    }
}

void CatalogTab::showCollections(const QList<sync::CatalogCollection>& list) {
    clearRows();
    // What Add all did stays in sight while the list comes again.
    note_->setText(!added_.isEmpty() ? added_ : list.isEmpty() ? tr("No collections yet.") : QString());
    int at = 0;
    for (const auto& c : list) {
        QWidget* r = collectionRow(c);
        rows_.insert(c.id, r);
        list_->insertWidget(at++, r);
    }
}

QWidget* CatalogTab::itemRow(const sync::CatalogItem& it) {
    QFrame* frame = rowFrame(this);
    frame->setObjectName(QStringLiteral("catalogRow:") + it.id);
    auto* h = new QHBoxLayout(frame);
    h->setContentsMargins(6, 6, 6, 6);
    h->setSpacing(8);
    QLabel* pic = pictureLabel(frame);
    h->addWidget(pic, 0, Qt::AlignTop);
    library_.picture(it.previewPath, pic, [pic](const QImage& img) { showPicture(pic, img); });
    auto* text = new QVBoxLayout();
    text->setSpacing(2);
    auto* title = new QLabel(it.title, frame);
    title->setProperty("rowTitle", true);
    title->setWordWrap(true);
    text->addWidget(title);
    text->addWidget(mutedLabel(tr("by %1 · %2").arg(it.by, usesText(it.uses)), frame));
    if (!it.description.isEmpty()) {
        title->setToolTip(it.description);
        pic->setToolTip(it.description);
    }
    const bool isModule = it.kind == QLatin1String("module");
    if (isModule) {
        auto* insert = new QPushButton(tr("Add and insert"), frame);
        insert->setObjectName(QStringLiteral("catalogInsert"));
        insert->setProperty("accent", true);
        insert->setAccessibleName(tr("Add and insert %1").arg(it.title));
        connect(insert, &QPushButton::clicked, this, [this, item = it] { emit library_.catalogInsertRequested(item); });
        text->addWidget(insert, 0, Qt::AlignLeft);
    }
    auto* add = new QPushButton(isModule ? tr("Add to my modules") : tr("Add to my parts"), frame);
    add->setObjectName(QStringLiteral("catalogAdd"));
    connect(add, &QPushButton::clicked, this, [this, item = it, add] { addItem(item, add); });
    text->addWidget(add, 0, Qt::AlignLeft);
    h->addLayout(text, 1);
    return frame;
}

QWidget* CatalogTab::collectionRow(const sync::CatalogCollection& c) {
    QFrame* frame = rowFrame(this);
    frame->setObjectName(QStringLiteral("collectionRow:") + c.id);
    auto* h = new QHBoxLayout(frame);
    h->setContentsMargins(6, 6, 6, 6);
    h->setSpacing(8);
    QLabel* pic = pictureLabel(frame);
    h->addWidget(pic, 0, Qt::AlignTop);
    if (!c.coverPath.isEmpty())
        library_.picture(c.coverPath, pic, [pic](const QImage& img) { showPicture(pic, img); });
    auto* text = new QVBoxLayout();
    text->setSpacing(2);
    if (c.featured) {
        auto* pill = new QLabel(tr("Featured"), frame);
        pill->setProperty("pill", true);
        text->addWidget(pill, 0, Qt::AlignLeft);
    }
    auto* title = new QLabel(c.title, frame);
    title->setProperty("rowTitle", true);
    title->setWordWrap(true);
    text->addWidget(title);
    text->addWidget(mutedLabel(tr("by %1 · %2").arg(c.by, itemsText(c.itemCount)), frame));
    if (!c.description.isEmpty()) text->addWidget(mutedLabel(c.description, frame));
    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(4);
    auto* addAllBtn = new QPushButton(tr("Add all"), frame);
    addAllBtn->setObjectName(QStringLiteral("collectionAddAll"));
    addAllBtn->setProperty("accent", true);
    addAllBtn->setToolTip(tr("A copy of each of its %1 goes to your modules and parts. Ones already there are skipped.")
                              .arg(itemsText(c.itemCount)));
    const sync::CatalogCollection coll = c;
    connect(addAllBtn, &QPushButton::clicked, this, [this, coll] { addAll(coll); });
    auto* open = new QPushButton(tr("Open"), frame);
    open->setObjectName(QStringLiteral("collectionOpen"));
    connect(open, &QPushButton::clicked, this, [this, coll] { openCollection(coll); });
    buttons->addWidget(addAllBtn);
    buttons->addWidget(open);
    buttons->addStretch(1);
    text->addLayout(buttons);
    h->addLayout(text, 1);
    return frame;
}

void CatalogTab::openCollection(const sync::CatalogCollection& c) {
    openCollection_ = c.id;
    reload();
}

std::optional<QString> CatalogTab::owner(const QString& title) {
    if (library_.orgs().isEmpty() || !chooseOwner) return QString();
    return chooseOwner(title);
}

void CatalogTab::addItem(const sync::CatalogItem& it, QPushButton* button) {
    const bool isModule = it.kind == QLatin1String("module");
    const auto dest = owner(isModule ? tr("Add to my modules") : tr("Add to my parts"));
    if (!dest) return;
    button->setEnabled(false);
    auto alive = std::make_shared<bool>(true);
    connect(button, &QObject::destroyed, this, [alive] { *alive = false; });
    QPushButton* b = button;
    library_.api().addCatalogItem(it.id, *dest, [this, b, alive, isModule, title = it.title](const QString&, const QString&) {
        if (*alive) b->setText(tr("Added"));
        emit library_.message(isModule ? tr("Added \"%1\" to your modules").arg(title)
                                       : tr("Added \"%1\" to your parts").arg(title));
        if (isModule) library_.refresh();
        else emit library_.partsAdded();
    }, [this, b, alive](const sync::ServerRefusal& r) {
        if (*alive) b->setEnabled(true);
        QMessageBox::warning(this, tr("Catalog"), ServerLibrary::refusalText(r));
    });
}

void CatalogTab::addAll(const sync::CatalogCollection& c) {
    const auto dest = owner(tr("Add all"));
    if (!dest) return;
    const bool parts = c.parts > 0;
    library_.api().addCollection(c.id, *dest, [this, parts](const sync::CollectionAddResult& r) {
        added_ = collectionAddText(r);
        note_->setText(added_);
        emit library_.message(collectionAddText(r));
        library_.refresh();
        if (parts && r.added > 0) emit library_.partsAdded();
    }, [this](const sync::ServerRefusal& r) {
        QMessageBox::warning(this, tr("Add all"), ServerLibrary::refusalText(r));
    });
}

}  // namespace bld::ui
