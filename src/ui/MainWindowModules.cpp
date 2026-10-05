// The server half of the Module library: your modules and
// your clubs' on the server, the catalog, Save to server with versions, and
// your warnings as a notice over the map. Kept up to date by the server's
// live hints (GET /api/events) and when you come back to the window.

#include "MainWindow.h"

#include "LayerPanel.h"
#include "LoadingCard.h"
#include "MapView.h"
#include "ModuleLibraryPanel.h"
#include "ModuleThumbnail.h"
#include "ModulesPanel.h"
#include "NoticeArea.h"
#include "SaveModuleDialog.h"
#include "ServerLibrary.h"
#include "tours/Tours.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/PartsLibrary.h"

#include "ConnectDialog.h"
#include "EventStream.h"
#include "LiveLayout.h"
#include "ModuleDoc.h"
#include "RefreshOnFocus.h"
#include "ServerList.h"

#include <QBuffer>
#include <QPainter>
#include <QToolButton>
#include <QDesktopServices>
#include <QInputDialog>
#include <QJsonObject>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QUndoStack>

namespace bld::ui {

namespace {

// The parts in `map` the library doesn't know.
bool hasUnknownParts(const core::Map& map, const parts::PartsLibrary& parts) {
    for (const auto& layer : map.layers()) {
        if (!layer || layer->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*layer).bricks)
            if (!parts.metadata(b.partNumber)) return true;
    }
    return false;
}

// The server's picture of a module: as big as the web makes it, smaller
// when the server's upload limit (before 2.x: 512 KB) can't take it.
constexpr qsizetype kThumbnailLimit = qsizetype{ 512 } * 1024;

QByteArray pngOf(const QImage& img) {
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes;
}

}  // namespace

void MainWindow::setupServerLibrary() {
    serverLibrary_ = new ServerLibrary(this);
    auto* serverTab = new ServerModulesTab(*serverLibrary_, moduleLibraryPanel_);
    auto* catalogTab = new CatalogTab(*serverLibrary_, moduleLibraryPanel_);
    moduleLibraryPanel_->addTab(serverTab, tr("Server"));
    moduleLibraryPanel_->addTab(catalogTab, tr("Catalog"));
    // The server's tabs replace the links to the website.
    moduleLibraryPanel_->setCatalogLinkVisible(false);
    // Each tab says which server it shows.
    auto retitle = [this] {
        QTabWidget* tabs = moduleLibraryPanel_->tabs();
        const QString label = serverLibrary_->label();
        tabs->setTabText(1, label.isEmpty() ? tr("Server") : label);
        tabs->setTabToolTip(1, label.isEmpty() ? tr("Modules you and your clubs saved on a server")
                                               : tr("Modules you and your clubs saved on %1").arg(label));
        tabs->setTabToolTip(2, tr("Modules, parts and collections people shared for everyone"));
    };
    connect(serverLibrary_, &ServerLibrary::stateChanged, this, retitle);
    connect(serverLibrary_, &ServerLibrary::changed, this, retitle);

    // The status bar says how the server is doing; a click opens Servers.
    serverStatus_ = new QToolButton(this);
    serverStatus_->setObjectName(QStringLiteral("serverStatus"));
    serverStatus_->setAutoRaise(true);
    serverStatus_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    serverStatus_->setCursor(Qt::PointingHandCursor);
    tours::tag(serverStatus_, QStringLiteral("servers.status"));
    statusBar()->addPermanentWidget(serverStatus_);
    connect(serverStatus_, &QToolButton::clicked, this, [this] {
        // Not inside the button's own click.
        QTimer::singleShot(0, this, &MainWindow::onManageServers);
    });
    connect(serverLibrary_, &ServerLibrary::stateChanged, this, &MainWindow::updateServerStatus);
    connect(serverLibrary_, &ServerLibrary::changed, this, &MainWindow::updateServerStatus);
    updateServerStatus();

    connect(serverLibrary_, &ServerLibrary::signInRequested, this, &MainWindow::signInToLibraryServer);
    connect(serverLibrary_, &ServerLibrary::addServerRequested, this, [this] {
        // Not inside the button's own click.
        QTimer::singleShot(0, this, [this] {
            onManageServers();
            updateLibraryServer();
        });
    });
    connect(serverLibrary_, &ServerLibrary::insertRequested, this, &MainWindow::insertServerModule);
    connect(serverLibrary_, &ServerLibrary::openRequested, this, [this](const QString& id) {
        QTimer::singleShot(0, this, [this, id] { openServerModule(id); });
    });
    connect(serverLibrary_, &ServerLibrary::catalogInsertRequested, this, &MainWindow::insertCatalogModule);
    connect(serverLibrary_, &ServerLibrary::partsAdded, this, [this] {
        syncServerParts(serverLibrary_->server(), serverLibrary_->token());
    });
    connect(serverLibrary_, &ServerLibrary::message, this, [this](const QString& text) {
        statusBar()->showMessage(text, 5000);
    });
    // Your warnings come with the library's server.
    connect(serverLibrary_, &ServerLibrary::stateChanged, this, [this] {
        if (serverLibrary_->state() == ServerLibrary::State::Ready) refreshNotices();
    });

    // Live: a hint from the server asks again (a burst of them, once).
    serverEvents_ = new sync::EventStream(this);
    connect(serverEvents_, &sync::EventStream::hint, this, &MainWindow::onServerHint);
    connect(serverEvents_, &sync::EventStream::reconnected, this, [this] {
        serverLibrary_->refresh();
        refreshNotices();
    });
    libraryRefresh_ = new QTimer(this);
    libraryRefresh_->setSingleShot(true);
    libraryRefresh_->setInterval(300);
    connect(libraryRefresh_, &QTimer::timeout, serverLibrary_, &ServerLibrary::refresh);
    // Back in the window: someone may have changed things on the web meanwhile.
    new sync::RefreshOnFocus(this, [this] {
        if (serverLibrary_->state() == ServerLibrary::State::Ready || serverLibrary_->state() == ServerLibrary::State::Offline) {
            serverLibrary_->refresh();
            refreshNotices();
        } else {
            // Signed in elsewhere (Servers…) meanwhile.
            updateLibraryServer();
        }
    });
    QTimer::singleShot(0, this, &MainWindow::updateLibraryServer);
}

namespace {

QIcon statusDot(const QColor& c) {
    QPixmap pm(10, 10);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(QRectF(1, 1, 8, 8));
    return QIcon(pm);
}

}  // namespace

void MainWindow::updateServerStatus() {
    if (!serverStatus_ || !serverLibrary_) return;
    const QString name = serverLibrary_->label();
    QString text;
    QString tip;
    QColor dot(150, 150, 150);
    switch (serverLibrary_->state()) {
    case ServerLibrary::State::NoServer:
        text = tr("No server");
        tip = tr("You're not connected to a server. Click to add your club's server and share layouts with it.");
        break;
    case ServerLibrary::State::SignedOut:
    case ServerLibrary::State::Refused:
        text = tr("Signed out of %1").arg(name);
        tip = tr("You're not signed in to %1. Click to sign in.").arg(name);
        dot = QColor(214, 140, 30);
        break;
    case ServerLibrary::State::Loading:
        text = tr("Connecting to %1…").arg(name);
        tip = tr("Reaching %1. Click to see your servers.").arg(name);
        break;
    case ServerLibrary::State::Ready:
        text = tr("Connected to %1").arg(name);
        tip = tr("Signed in to %1: your modules, the catalog and your settings come from there. Click to see your "
                 "servers.")
                  .arg(name);
        dot = QColor(46, 157, 91);
        break;
    case ServerLibrary::State::Offline:
        text = tr("%1 is offline").arg(name);
        tip = tr("%1 can't be reached right now; the app tries again by itself. Click to see your servers.").arg(name);
        dot = QColor(200, 70, 60);
        break;
    }
    serverStatus_->setText(text);
    serverStatus_->setToolTip(tip);
    serverStatus_->setAccessibleName(text);
    serverStatus_->setIcon(statusDot(dot));
}

void MainWindow::updateLibraryServer() {
    if (!serverLibrary_) return;
    const sync::ServerList servers = sync::ServerList::load();
    const sync::ServerEntry* e = live_ && live_->active() ? servers.find(liveServer_) : nullptr;
    if (!e) e = servers.lastUsed();
    if (!e) {
        serverLibrary_->setServer({}, {}, {});
        serverEvents_->stop();
        return;
    }
    const QUrl url = e->url;
    const QString label = e->label();
    // Live on it: that token is at hand.
    if (live_ && live_->active() && sync::TokenStore::keyFor(url) == sync::TokenStore::keyFor(liveServer_) &&
        !liveToken_.isEmpty()) {
        serverLibrary_->setServer(url, label, liveToken_);
        serverEvents_->start(url, liveToken_);
        return;
    }
    tokens_->load(url, [this, alive = std::weak_ptr<int>(alive_), url, label](const QString& token) {
        if (alive.expired()) return;
        serverLibrary_->setServer(url, label, token);
        if (token.isEmpty()) serverEvents_->stop();
        else serverEvents_->start(url, token);
    });
}

void MainWindow::signInToLibraryServer() {
    const QUrl url = serverLibrary_->server();
    if (!url.isValid()) return;
    // Not inside the button's own click.
    QTimer::singleShot(0, this, [this, url] {
        sync::ServerApi api;
        sync::ConnectDialog dialog(api, *tokens_, [](const QUrl& u) { QDesktopServices::openUrl(u); }, this,
                                   sync::ConnectDialog::Purpose::SignIn);
        dialog.setAddress(url.toString());
        QTimer::singleShot(0, &dialog, &sync::ConnectDialog::connectToServer);
        if (dialog.exec() != QDialog::Accepted || !dialog.result()) return;
        const QString token = dialog.result()->token;
        serverLibrary_->setServer(url, serverLibrary_->label(), token);
        serverEvents_->start(url, token);
    });
}

void MainWindow::onServerHint(const QJsonObject& hint) {
    const QString kind = hint.value(QLatin1String("kind")).toString();
    if (kind == QLatin1String("warning")) {
        refreshNotices();
    } else if (kind == QLatin1String("module") || kind == QLatin1String("catalog") || kind == QLatin1String("club") ||
               kind == QLatin1String("me") || kind == QLatin1String("settings")) {
        libraryRefresh_->start();
        if (kind == QLatin1String("club") || kind == QLatin1String("me")) refreshNotices();
    }
}

void MainWindow::insertServerModule(const QString& moduleId) {
    if (!serverLibrary_ || !serverLibrary_->inserting().isEmpty()) return;
    const sync::ServerModule* m = serverLibrary_->module(moduleId);
    const QString title = m ? m->title : tr("Module");
    ensureDocument();
    serverLibrary_->setInserting(moduleId);
    if (LoadingCard* card = mapView_->loadingCard()) {
        card->setPlace(LoadingCard::Place::Centre);
        card->showBusy(tr("Getting “%1” from the server…").arg(title));
    }
    const QUrl server = serverLibrary_->server();
    const QString token = serverLibrary_->token();
    const QUrl source = serverLibrary_->api().moduleWebUrl(moduleId);
    const auto finish = [this] {
        serverLibrary_->setInserting({});
        if (LoadingCard* card = mapView_->loadingCard(); card && !card->failureShown()) card->finish();
    };
    serverLibrary_->api().moduleSnapshot(moduleId, [this, title, server, token, source, finish](const QByteArray& bytes) {
        QString error;
        std::shared_ptr<core::Map> module(sync::mapFromModuleSnapshot(bytes, &error).release());
        if (!module || sync::partCount(*module) == 0) {
            finish();
            QMessageBox::warning(this, tr("Add module"),
                                 module ? tr("“%1” has no parts yet. Open it to add some.").arg(title)
                                        : tr("“%1” couldn't be read (%2).").arg(title, error));
            return;
        }
        auto place = [this, module, title, source, finish] {
            finish();
            if (!mapView_->currentMap()) return;
            if (!mapView_->placeModule(*module, title, source.toString(), mapView_->viewCentre())) return;
            modulesPanel_->setMap(mapView_->currentMap());
            layerPanel_->setMap(mapView_->currentMap(), mapView_->builder());
            statusBar()->showMessage(tr("Added “%1” to the layout").arg(title), 5000);
        };
        // Its parts come from the server first, as the web shows them.
        const sync::ServerList servers = sync::ServerList::load();
        const sync::ServerEntry* e = servers.find(server);
        const bool serverHasParts = !e || !e->features || e->features->contains(QStringLiteral("partsManifest"));
        if (serverHasParts && hasUnknownParts(*module, parts_)) syncServerParts(server, token, place);
        else place();
    }, [this, title, finish](const sync::ServerRefusal& r) {
        finish();
        QMessageBox::warning(this, tr("Add module"),
                             tr("Couldn't get “%1”: %2").arg(title, ServerLibrary::refusalText(r)));
    });
}

void MainWindow::insertCatalogModule(const sync::CatalogItem& item) {
    if (!serverLibrary_) return;
    statusBar()->showMessage(tr("Adding “%1” to your modules…").arg(item.title), 5000);
    serverLibrary_->api().addCatalogItem(item.id, QString(), [this, item](const QString&, const QString& id) {
        // The copy is yours now: list it, then put it in the layout.
        serverLibrary_->refresh();
        auto once = std::make_shared<QMetaObject::Connection>();
        *once = connect(serverLibrary_, &ServerLibrary::changed, this, [this, id, once] {
            disconnect(*once);
            insertServerModule(id);
        });
    }, [this, item](const sync::ServerRefusal& r) {
        QMessageBox::warning(this, tr("Catalog"), tr("Couldn't add “%1”: %2").arg(item.title, ServerLibrary::refusalText(r)));
    });
}

void MainWindow::openServerModule(const QString& moduleId) {
    if (!serverLibrary_) return;
    const sync::ServerModule* m = serverLibrary_->module(moduleId);
    if (!m) return;
    const QString title = m->title;
    const QUrl server = serverLibrary_->server();
    const QString token = serverLibrary_->token();
    if (!maybeSave()) return;
    serverLibrary_->api().moduleSnapshot(moduleId, [this, moduleId, title, server, token](const QByteArray& bytes) {
        QString error;
        std::shared_ptr<core::Map> map(sync::mapFromModuleSnapshot(bytes, &error).release());
        if (!map) {
            QMessageBox::warning(this, tr("Open module"), tr("“%1” couldn't be read (%2).").arg(title, error));
            return;
        }
        forgetFileSource();
        // The module replaces the live layout here: leave the server session.
        if (live_ && live_->active()) {
            live_->close();
            updateLiveUi();
        }
        auto open = [this, moduleId, title, server, token, map] {
            clearActiveView();
            mapView_->loadMap(std::make_unique<core::Map>(std::move(*map)));
            layerPanel_->setMap(mapView_->currentMap(), mapView_->builder());
            modulesPanel_->setMap(mapView_->currentMap());
            currentFilePath_.clear();
            mapView_->undoStack()->clear();
            mapView_->undoStack()->setClean();
            cleanUndoIndex_ = 0;
            editingModule_ = { server, token, moduleId, title };
            serverLibrary_->setEditingModule(moduleId);
            updateTitle();
            notices_->showNotice(
                QStringLiteral("editing-module"), tr("You're changing the module “%1”").arg(title),
                tr("Add, move or remove its parts, then Save (Ctrl+S) to make a new version on %1.")
                    .arg(serverLibrary_->label()),
                { NoticeAction{ tr("Save new version"), [this] { saveEditedModule(); }, true, false },
                  NoticeAction{ tr("OK"), {} } });
            statusBar()->showMessage(tr("Opened the module “%1”").arg(title), 5000);
        };
        const sync::ServerList servers = sync::ServerList::load();
        const sync::ServerEntry* e = servers.find(server);
        const bool serverHasParts = !e || !e->features || e->features->contains(QStringLiteral("partsManifest"));
        if (serverHasParts && hasUnknownParts(*map, parts_)) syncServerParts(server, token, open);
        else open();
    }, [this, title](const sync::ServerRefusal& r) {
        QMessageBox::warning(this, tr("Open module"), tr("Couldn't get “%1”: %2").arg(title, ServerLibrary::refusalText(r)));
    });
}

void MainWindow::clearEditingModule() {
    if (editingModule_.id.isEmpty()) return;
    editingModule_ = {};
    if (serverLibrary_) serverLibrary_->setEditingModule({});
    notices_->hideNotice(QStringLiteral("editing-module"));
}

bool MainWindow::saveEditedModule() {
    auto* map = mapView_->currentMap();
    if (!map || editingModule_.id.isEmpty()) return false;
    bool ok = false;
    const QString note = QInputDialog::getText(this, tr("Save new version"),
                                               tr("What changed? (optional)"), QLineEdit::Normal, {}, &ok);
    if (!ok) return false;
    // Only its parts sheets are the module.
    core::Map module;
    module.author = map->author;
    module.lug = map->lug;
    module.event = map->event;
    for (const auto& layer : map->layers()) {
        if (!layer || layer->kind() != core::LayerKind::Brick) continue;
        auto copy = std::make_unique<core::LayerBrick>(static_cast<const core::LayerBrick&>(*layer));
        module.layers().push_back(std::move(copy));
    }
    module.nbItems = sync::partCount(module);
    const EditingModule target = editingModule_;
    const int undoIndex = mapView_->undoStack()->index();
    statusBar()->showMessage(tr("Saving “%1”…").arg(target.title));
    // The server the module came from, whichever one the library shows now.
    auto* api = new sync::LibraryApi(this);
    api->setBase(target.server);
    api->setToken(target.token);
    uploadModule(*api, std::make_shared<core::Map>(std::move(module)), target.id, target.title, QString(), note,
                 [this, api, target, undoIndex](bool saved) {
                     api->deleteLater();
                     if (!saved) return;
                     if (editingModule_.id == target.id && mapView_->undoStack()->index() == undoIndex) {
                         mapView_->undoStack()->setClean();
                         updateTitle();
                     }
                 });
    return true;
}

void MainWindow::saveModuleToServer(core::Map& module, int partCount) {
    SaveModuleDialog dialog(*serverLibrary_, QString(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    const SaveModuleDialog::Choice c = dialog.choice();
    if (c.onThisComputer) {
        saveModuleLocally(module, partCount, c.title);
        return;
    }
    // Centred on the origin, as the web saves modules, so both apps place it alike.
    QRectF box;
    for (const auto& layer : module.layers())
        if (layer && layer->kind() == core::LayerKind::Brick)
            for (const auto& b : static_cast<const core::LayerBrick&>(*layer).bricks)
                box = box.isNull() ? b.displayArea : box.united(b.displayArea);
    const QPointF shift = -box.center();
    for (auto& layer : module.layers())
        if (layer && layer->kind() == core::LayerKind::Brick)
            for (auto& b : static_cast<core::LayerBrick&>(*layer).bricks) b.displayArea.translate(shift);
    uploadModule(serverLibrary_->api(), std::make_shared<core::Map>(std::move(module)), c.updateId, c.title, c.orgSlug,
                 c.note, {});
}

void MainWindow::uploadModule(sync::LibraryApi& api, const std::shared_ptr<core::Map>& module, const QString& updateId,
                              const QString& title, const QString& orgSlug, const QString& note,
                              const std::function<void(bool)>& done) {
    // Its picture now, while the parts are at hand: as big as the web's,
    // smaller when the server's limit can't take that.
    QByteArray png;
    for (int side : { kServerModuleThumbnailSide, kServerModuleThumbnailSide / 2, kServerModuleThumbnailSide / 4 }) {
        png = pngOf(renderModuleThumbnail(*module, parts_, side));
        if (png.size() <= kThumbnailLimit) break;
    }
    const sync::ServerList servers = sync::ServerList::load();
    const sync::ServerEntry* entry = servers.find(api.base());
    const QString server = entry ? entry->label() : api.base().host();
    auto failed = [this, done](const sync::ServerRefusal& r) {
        QMessageBox::warning(this, tr("Save module"), tr("Couldn't save the module: %1").arg(ServerLibrary::refusalText(r)));
        if (done) done(false);
    };
    // Write the module into what the server holds (a new one starts with
    // the server's empty layout), then save it as a version.
    auto write = [this, &api, module, png, note, server, done, failed](
                     const QString& id, const QString& name, bool created) {
        api.moduleSnapshot(id, [this, &api, module, id, name, png, note, server, done, failed, created](const QByteArray& current) {
            QString error;
            const QByteArray snapshot = sync::moduleSnapshotFor(*module, current, &error);
            if (snapshot.isEmpty()) {
                QMessageBox::warning(this, tr("Save module"), tr("Couldn't save the module (%1).").arg(error));
                if (created) api.deleteModule(id, {}, {});
                if (done) done(false);
                return;
            }
            api.saveModuleSnapshot(id, snapshot, note, [this, &api, id, name, png, server, done](int version) {
                auto finished = [this, name, version, server, done] {
                    statusBar()->showMessage(version > 1 ? tr("Saved version %1 of “%2” on %3").arg(version).arg(name, server)
                                                         : tr("Saved “%1” on %2").arg(name, server),
                                             6000);
                    serverLibrary_->refresh();
                    if (done) done(true);
                };
                if (png.isEmpty()) {
                    finished();
                    return;
                }
                // No picture: the lists show a placeholder until it's opened on the web.
                api.setModuleThumbnail(id, png, finished, [finished](const sync::ServerRefusal&) { finished(); });
            }, [&api, id, created, failed](const sync::ServerRefusal& r) {
                // Don't leave an empty module behind when its contents didn't arrive.
                if (created) api.deleteModule(id, {}, {});
                failed(r);
            });
        }, failed);
    };
    if (!updateId.isEmpty()) {
        const sync::ServerModule* m = serverLibrary_->module(updateId);
        write(updateId, m ? m->title : title, false);
        return;
    }
    api.createModule(title, orgSlug, [write](const QString& id, const QString& name) { write(id, name, true); }, failed);
}

void MainWindow::refreshNotices() {
    if (!serverLibrary_ || serverLibrary_->token().isEmpty()) {
        notices_->hideNotice(QStringLiteral("server-warning"));
        return;
    }
    const QUrl server = serverLibrary_->server();
    serverLibrary_->api().notices([this, server](const QList<sync::Notice>& list) {
        if (server != serverLibrary_->server()) return;
        showNextNotice(list);
    }, [](const sync::ServerRefusal&) {
        // A server before warnings, or offline: nothing to show.
    });
}

void MainWindow::showNextNotice(const QList<sync::Notice>& list) {
    QList<sync::Notice> open;
    for (const auto& n : list)
        if (!n.acknowledged) open << n;
    if (open.isEmpty()) {
        shownNotice_.clear();
        notices_->hideNotice(QStringLiteral("server-warning"));
        return;
    }
    const sync::Notice w = open.first();
    shownNotice_ = w.id;
    const QString severity = w.severity == QLatin1String("final")  ? tr("Final warning")
                             : w.severity == QLatin1String("note") ? tr("Note")
                                                                   : tr("Warning");
    const QString from = w.scope == QLatin1String("club") && !w.clubName.isEmpty() ? tr("From %1").arg(w.clubName)
                                                                                   : tr("From the site team");
    QString title = severity + QStringLiteral(" · ") + from;
    if (open.size() > 1) title += QStringLiteral(" ") + tr("(1 of %1)").arg(open.size());
    QString text = w.reason;
    if (!w.toClub.isEmpty()) text = tr("To your club %1").arg(w.toClub) + QStringLiteral("\n\n") + text;
    const QUrl base = serverLibrary_->server();
    const QString id = w.id;
    QList<NoticeAction> actions;
    actions << NoticeAction{ tr("I understand"), [this, id] {
                                serverLibrary_->api().acknowledge(id, [this] { refreshNotices(); },
                                                                  [this](const sync::ServerRefusal& r) {
                                                                      statusBar()->showMessage(ServerLibrary::refusalText(r), 6000);
                                                                  });
                            },
                             true, true };
    if (!w.link.isEmpty()) {
        QUrl link = base;
        link.setPath(w.link.section(QLatin1Char('?'), 0, 0));
        if (w.link.contains(QLatin1Char('?'))) link.setQuery(w.link.section(QLatin1Char('?'), 1));
        actions << NoticeAction{ tr("See what it's about"), [link] { QDesktopServices::openUrl(link); }, false, false };
    }
    QUrl all = base;
    all.setPath(QStringLiteral("/notices"));
    actions << NoticeAction{ tr("All notices"), [all] { QDesktopServices::openUrl(all); }, false, false };
    notices_->showNotice(QStringLiteral("server-warning"), title, text, actions, {}, w.severity != QLatin1String("note"));
}

}  // namespace bld::ui
