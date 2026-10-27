// Sending to a server, one model for everything (the web's words, the same
// "Save to: Me / a club" and the club question):
// - layouts: File › Save to Server… (the open layout), a layout file from
//   the Server window or the welcome;
// - modules: Save to Module library… on a placed module, or a Module
//   library file from the Server window;
// - venues: Save to Server… in the Venue library and the Venue Designer;
// - parts: your own parts the server lacks, one or all;
// - Share to the public catalog… for what's on the server.
// The Server window lists what's on this computer only ("Not on the
// server yet") with Send…, kept up to date as files and the layout change.

#include "MainWindow.h"

#include "MapView.h"
#include "ModuleLibraryPanel.h"
#include "ModuleThumbnail.h"
#include "SendToServerDialogs.h"
#include "ServerLibrary.h"
#include "ServerWindow.h"
#include "VenueLibraryPanel.h"
#include "NoticeArea.h"
#include "PartsBrowser.h"
#include "SaveSetDialog.h"

#include "../core/Map.h"
#include "../core/Venue.h"
#include "../edit/ModuleLibraryLink.h"
#include "../import/LayoutFile.h"
#include "../parts/PartsLibrary.h"
#include "../saveload/BbmReader.h"
#include "../saveload/SetIO.h"
#include "../saveload/VenueIO.h"

#include "LiveLayout.h"
#include "ModuleDoc.h"
#include "PartsUpload.h"
#include "ServerList.h"
#include "UploadPartsDialog.h"
#include "WhileAlive.h"

#include <QBuffer>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonDocument>
#include <QMessageBox>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoStack>

namespace bld::ui {

namespace {

using Tab = ServerWindow::Tab;

QByteArray pngBytes(const QImage& img) {
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes;
}

// A venue as the server keeps it: the .bld-venue file's JSON without its schema tag.
QJsonObject venueJson(const core::Venue& venue) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("venue.bld-venue"));
    if (!saveload::writeVenueFile(path, venue)) return {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    o.remove(QStringLiteral("schema"));
    return o;
}

}  // namespace

QString MainWindow::defaultSaveOwner() const {
    if (!serverLibrary_) return {};
    const QString show = ServerWindow::rememberedShow(serverLibrary_->server());
    return show == QLatin1String("all") || show == QLatin1String("me") ? QString() : show;
}

bool MainWindow::readyToSend() {
    if (serverLibrary_ && serverLibrary_->state() == ServerLibrary::State::Ready) return true;
    // Not signed in (or no server): the Server window says so, with the button that helps.
    statusBar()->showMessage(tr("Sign in to a server first: the Server window shows how."), 6000);
    showServerWindow(serverWindow_ ? static_cast<int>(serverWindow_->currentTab()) : 0);
    return false;
}

void MainWindow::refreshServerLocal() {
    if (!serverWindow_) return;
    if (!localWatch_) {
        // Live: a file added to (or removed from) the Module or Venue library shows at once.
        localWatch_ = new QFileSystemWatcher(this);
        localRefresh_ = new QTimer(this);
        localRefresh_->setSingleShot(true);
        localRefresh_->setInterval(400);
        connect(localRefresh_, &QTimer::timeout, this, &MainWindow::refreshServerLocal);
        connect(localWatch_, &QFileSystemWatcher::directoryChanged, localRefresh_, qOverload<>(&QTimer::start));
        // The open layout: published, modules saved, a new one opened.
        connect(mapView_->undoStack(), &QUndoStack::indexChanged, localRefresh_, qOverload<>(&QTimer::start));
        connect(mapView_->undoStack(), &QUndoStack::cleanChanged, localRefresh_, qOverload<>(&QTimer::start));
    }
    for (const QString& dir : { moduleLibraryPanel_->libraryPath(), venueLibraryPanel_->libraryPath() })
        if (!dir.isEmpty() && QDir(dir).exists() && !localWatch_->directories().contains(dir)) localWatch_->addPath(dir);

    // Layouts: the open one (not live, not a copy of one there), then recent files.
    QList<ServerWindow::LocalItem> layouts;
    const core::Map* map = mapView_->currentMap();
    const bool live = live_ && live_->active();
    const bool copyOfServer = fileSource_ && QUrl(fileSource_->server) == serverLibrary_->server();
    if (map && !live && !copyOfServer)
        layouts << ServerWindow::LocalItem{ QStringLiteral("open"), layoutTitle(),
                                            currentFilePath_.isEmpty() ? tr("Open now, not saved as a file yet")
                                                                       : tr("Open now · %1").arg(QFileInfo(currentFilePath_).fileName()) };
    for (const QString& path : QSettings().value(QStringLiteral("recent/list")).toStringList()) {
        if (path == currentFilePath_ || !QFileInfo::exists(path)) continue;
        layouts << ServerWindow::LocalItem{ path, QFileInfo(path).completeBaseName(),
                                            tr("Recent file · %1").arg(QDir::toNativeSeparators(QFileInfo(path).absolutePath())) };
    }
    serverWindow_->setLocalItems(Tab::Layouts, layouts);

    // Modules: placed in the open layout and not in the library yet, then the Module library folder.
    QList<ServerWindow::LocalItem> modules;
    if (map)
        for (const core::Module& m : map->sidecar.modules)
            if (!edit::libraryLink(m))
                modules << ServerWindow::LocalItem{ QStringLiteral("placed:") + m.id, m.name.isEmpty() ? tr("Module") : m.name,
                                                    tr("In this layout") };
    if (const QString dir = moduleLibraryPanel_->libraryPath(); !dir.isEmpty())
        for (const QFileInfo& f : QDir(dir).entryInfoList({ QStringLiteral("*.bbm") }, QDir::Files, QDir::Name))
            modules << ServerWindow::LocalItem{ QStringLiteral("file:") + f.absoluteFilePath(), f.completeBaseName(),
                                                tr("Module library folder") };
    serverWindow_->setLocalItems(Tab::Modules, modules);

    // Venues: the Venue library folder.
    QList<ServerWindow::LocalItem> venues;
    if (const QString dir = venueLibraryPanel_->libraryPath(); !dir.isEmpty())
        for (const QFileInfo& f : QDir(dir).entryInfoList({ QStringLiteral("*.bld-venue") }, QDir::Files, QDir::Name))
            venues << ServerWindow::LocalItem{ f.absoluteFilePath(), f.completeBaseName(), tr("Venue library") };
    serverWindow_->setLocalItems(Tab::Venues, venues);

    // Parts: your own the server lacks, as last checked.
    QList<ServerWindow::LocalItem> parts;
    for (const sync::LocalPart& p : std::as_const(missingParts_))
        parts << ServerWindow::LocalItem{ p.key, p.displayName, tr("Your part %1").arg(p.key) };
    serverWindow_->setLocalItems(Tab::Parts, parts);
}

bool MainWindow::serverTakesSets(const QUrl& server) {
    const sync::ServerList list = sync::ServerList::load();
    const sync::ServerEntry* e = list.find(server);
    return e && e->features && e->features->contains(QStringLiteral("uploadSets"));
}

std::optional<MainWindow::SetServer> MainWindow::setUploadServer() const {
    if (serverLibrary_ && serverLibrary_->state() == ServerLibrary::State::Ready
        && serverTakesSets(serverLibrary_->server()))
        return SetServer{ serverLibrary_->server(), serverLibrary_->token(), serverLibrary_->label() };
    // Live on a server that says it takes sets (one that doesn't say doesn't).
    if (live_ && live_->active() && !liveToken_.isEmpty() && liveInfo_.features
        && liveInfo_.features->contains(QStringLiteral("uploadSets"))) {
        const sync::ServerList list = sync::ServerList::load();
        const sync::ServerEntry* e = list.find(liveServer_);
        return SetServer{ liveServer_, liveToken_, e ? e->label() : liveServer_.host() };
    }
    return std::nullopt;
}

void MainWindow::uploadSet(const SetServer& to, const sync::LocalPart& set) {
    auto* upload = new sync::PartsUpload(to.server, to.token, this);
    upload->setServerTakesSets(true);
    // (A refusal comes as failures here too, after failed().)
    connect(upload, &sync::PartsUpload::uploaded, this,
            [this, upload, to, set, name = set.displayName](int count, const QStringList& failures) {
                upload->deleteLater();
                // Its name is taken there: offer a free one instead of only saying no.
                if (count == 0 && upload->refusalCode(set.key) == QLatin1String("part_number_taken")) {
                    offerAnotherSetName(to, set);
                    return;
                }
                if (count == 0) {
                    statusBar()->showMessage(tr("“%1” is saved on this computer, but it couldn’t go to %2: %3")
                                                 .arg(name, to.label, failures.join(QStringLiteral("; "))),
                                             10000);
                    return;
                }
                statusBar()->showMessage(tr("“%1” is in your parts on %2 too.").arg(name, to.label), 6000);
                if (live_->active() && to.server == liveServer_) loadLivePartsCatalog();
                checkMissingParts();
                // Parts of your own in it that the server lacks would show as
                // missing there, and wouldn't come along when it's shared:
                // offer them (nothing is offered when the server has them all).
                if (const QStringList own = ownPartsInSet(set.key); !own.isEmpty())
                    uploadPartsTo(to.server, to.token, true, own);
            });
    upload->upload({ set }, QString());
}

QStringList MainWindow::ownPartsInSet(const QString& setKey) const {
    // Your own parts (as Upload My Parts lists them), by part number.
    QHash<QString, QString> own;
    for (const sync::LocalPart& p : scanOwnParts()) own.insert(p.key.toUpper(), p.key);
    QStringList out;
    QSet<QString> seen;
    QStringList pending{ setKey };
    while (!pending.isEmpty()) {
        const QString key = pending.takeFirst();
        if (seen.contains(key.toUpper())) continue;
        seen.insert(key.toUpper());
        // The library knows a set by its file name with ".set", the upload without.
        auto meta = parts_.metadata(key);
        if (!meta && !key.endsWith(QStringLiteral(".set"), Qt::CaseInsensitive)) meta = parts_.metadata(key + QStringLiteral(".set"));
        if (!meta) continue;
        for (const parts::PartSubPart& sub : meta->subparts) {
            QString number = sub.subKey;
            if (number.endsWith(QStringLiteral(".set"), Qt::CaseInsensitive)) number.chop(4);
            if (const auto it = own.constFind(number.toUpper()); it != own.cend() && !out.contains(*it)) out << *it;
            pending << sub.subKey; // a set inside the set: its parts too
        }
    }
    return out;
}

void MainWindow::offerAnotherSetName(const SetServer& to, const sync::LocalPart& set) {
    auto* api = new sync::LibraryApi(this);
    api->setBase(to.server);
    api->setToken(to.token);
    // What's taken: the numbers of your own parts there (a club's or a shared
    // one with the same number doesn't stop yours), and the sets beside this one here.
    const auto ask = [this, to, set](const QSet<QString>& onServer) {
        const QDir dir = QFileInfo(set.xmlPath).absoluteDir();
        const auto taken = [&](const QString& stem) {
            return onServer.contains(stem.toLower()) || QFileInfo::exists(dir.filePath(stem + QStringLiteral(".set.xml")));
        };
        // Nor a number of one of its own parts (it would stand in for that part).
        const QStringList subparts = saveload::setSubpartKeys(set.xmlPath);
        const auto takenOrOwnPart = [&](const QString& stem) {
            return taken(stem) || !saveload::partNamedLikeSet(stem, subparts).isEmpty();
        };
        SetNameTakenDialog dialog(set.key, saveload::freeSetName(set.displayName, takenOrOwnPart), to.label,
                                  this);
        dialog.refuseNamesOf(subparts);
        if (!(runSetNameTakenDialog ? runSetNameTakenDialog(dialog) : dialog.exec() == QDialog::Accepted)) {
            statusBar()->showMessage(tr("“%1” is saved on this computer only.").arg(set.displayName), 8000);
            return;
        }
        const QString name = dialog.name();
        const QString stem = saveload::setFileStem(name);
        const QString path = dir.filePath(stem + QStringLiteral(".set.xml"));
        const bool sameFile = QFileInfo(path).absoluteFilePath() == QFileInfo(set.xmlPath).absoluteFilePath();
        if (!sameFile && QFileInfo::exists(path)) {
            // A set of yours here has that name already: ask again.
            QMessageBox::information(this, tr("Name already used"),
                                     tr("You already have a set called “%1” on this computer. Pick another name.").arg(stem));
            offerAnotherSetName(to, set);
            return;
        }
        if (!sameFile && !QFile::rename(set.xmlPath, path)) {
            QMessageBox::warning(this, tr("Save set"), tr("“%1” couldn’t be renamed on this computer.").arg(set.displayName));
            return;
        }
        QString error;
        if (!saveload::renameSetXml(path, name, &error))
            QMessageBox::warning(this, tr("Save set"), tr("The set couldn’t be renamed: %1").arg(error));
        rescanLibrary(allLibraryPaths());
        partsBrowser_->rebuild();
        sync::LocalPart renamed{ stem, name, path, QString() };
        renamed.isSet = true;
        uploadSet(to, renamed);
    };
    api->myCustomParts(
        [api, ask](const QList<sync::ServerPart>& list) {
            api->deleteLater();
            QSet<QString> numbers;
            for (const sync::ServerPart& p : list) numbers.insert(p.partNumber.toLower());
            ask(numbers);
        },
        [api, ask](const sync::ServerRefusal&) {
            api->deleteLater();
            ask({});
        });
}

bool MainWindow::serverDeletesParts(const QUrl& server) const {
    const QString flag = QStringLiteral("deletePartsFromDesktop");
    if (live_ && live_->active() && server == liveServer_ && liveInfo_.features
        && liveInfo_.features->contains(flag))
        return true;
    const sync::ServerList list = sync::ServerList::load();
    const sync::ServerEntry* e = list.find(server);
    return e && e->features && e->features->contains(flag);
}

void MainWindow::deletePartOnServer(const SetServer& on, const QString& id, const QString& name) {
    auto* api = new sync::LibraryApi(this);
    api->setBase(on.server);
    api->setToken(on.token);
    api->deleteCustomPart(
        id,
        [this, api, on, name] {
            api->deleteLater();
            statusBar()->showMessage(tr("“%1” is deleted from %2 too.").arg(name, on.label), 6000);
            if (live_->active() && on.server == liveServer_) loadLivePartsCatalog();
        },
        [this, api, on, name](const sync::ServerRefusal& r) {
            api->deleteLater();
            QMessageBox::warning(this, tr("Delete"),
                                 tr("“%1” is deleted from this computer, but not from %2: %3")
                                     .arg(name, on.label, ServerLibrary::refusalText(r)));
        });
}

void MainWindow::findServerCopy(const QString& key, bool set,
                                const std::function<void(const ServerSetCopy&)>& found) {
    // The server you're signed in to, and the live layout's if it's another.
    QList<SetServer> servers;
    if (serverLibrary_ && serverLibrary_->state() == ServerLibrary::State::Ready)
        servers.push_back({ serverLibrary_->server(), serverLibrary_->token(), serverLibrary_->label() });
    if (live_ && live_->active() && !liveToken_.isEmpty() && (servers.isEmpty() || servers.front().server != liveServer_))
        servers.push_back({ liveServer_, liveToken_, liveServerName() });
    for (const SetServer& on : std::as_const(servers)) {
        auto* api = new sync::LibraryApi(this);
        api->setBase(on.server);
        api->setToken(on.token);
        // Your own there only: a club's set or part with the same number, or
        // one someone shared with you, isn't the copy you sent.
        api->myCustomParts(
            [this, api, on, key, set, found](const QList<sync::ServerPart>& list) {
                api->deleteLater();
                for (const sync::ServerPart& p : list) {
                    // Your set there (no picture of its own) or part (with one), by its number, as uploaded.
                    if (p.hasSprite == set || p.partNumber.compare(key, Qt::CaseInsensitive) != 0) continue;
                    if (p.role != QLatin1String("owner") && p.role != QLatin1String("editor")) continue;
                    ServerSetCopy copy;
                    copy.serverLabel = on.label;
                    if (set)
                        copy.rename = [this, on, id = p.id, number = p.partNumber](const QString& name) {
                            renameSetOnServer(on, id, number, name);
                        };
                    if (p.role == QLatin1String("owner")) {
                        // Servers with deletePartsFromDesktop take the desktop's token for it.
                        const bool deletes = serverDeletesParts(on.server);
                        if (deletes)
                            copy.deleteThere = [this, on, id = p.id, name = p.name()] {
                                deletePartOnServer(on, id, name);
                            };
                        // Deleted here, still there: say so, with a way to delete it there
                        // (from here when the server lets the desktop, else on the website).
                        copy.stillThere = [this, on, deletes, deleteThere = copy.deleteThere,
                                           name = p.name()] {
                            QUrl parts = on.server;
                            parts.setPath(QStringLiteral("/"));
                            parts.setFragment(QStringLiteral("parts"));
                            const QString text = deletes ? tr("“%1” is deleted from this computer, but it’s "
                                                              "still in your parts on %2.")
                                                               .arg(name, on.label)
                                                         : tr("“%1” is deleted from this computer, but it’s "
                                                              "still in your parts on %2. "
                                                              "Delete it there on the website.")
                                                               .arg(name, on.label);
                            const NoticeAction there =
                                deletes
                                    ? NoticeAction{ tr("Delete it from %1").arg(on.label), deleteThere, true }
                                    : NoticeAction{ tr("Open %1").arg(on.label),
                                                    [parts] { QDesktopServices::openUrl(parts); }, true };
                            notices_->showNotice(QStringLiteral("set-still-on-server:")
                                                     + on.server.toString(),
                                                 tr("Still on %1").arg(on.label), text,
                                                 { there, NoticeAction{ tr("Keep it there"), {} } });
                        };
                    }
                    found(copy);
                    return;
                }
            },
            [api](const sync::ServerRefusal&) { api->deleteLater(); });
    }
}

void MainWindow::renameSetOnServer(const SetServer& on, const QString& id, const QString& partNumber,
                                   const QString& name) {
    auto* api = new sync::LibraryApi(this);
    api->setBase(on.server);
    api->setToken(on.token);
    const auto failed = [this, api, on, name](const sync::ServerRefusal& r) {
        api->deleteLater();
        QMessageBox::warning(this, tr("Rename set"),
                             tr("“%1” is renamed on this computer, but not on %2: %3")
                                 .arg(name, on.label, ServerLibrary::refusalText(r)));
    };
    // Its XML as it is there, with the new name: the rest stays as it is.
    api->picture(QStringLiteral("/api/custom-parts/%1/xml").arg(sync::idPath(id)),
        [this, api, on, id, partNumber, name, failed](const QByteArray& xml) {
            const std::optional<QString> renamed = saveload::renamedSetXml(QString::fromUtf8(xml), name);
            if (!renamed) {
                failed(sync::ServerRefusal{});
                return;
            }
            const QJsonObject body{ { QStringLiteral("partNumber"), partNumber },
                                    { QStringLiteral("displayName"), name },
                                    { QStringLiteral("xmlBase64"), QString::fromLatin1(renamed->toUtf8().toBase64()) } };
            api->replaceCustomPart(id, body, [this, api, on, name] {
                api->deleteLater();
                statusBar()->showMessage(tr("“%1” is renamed on %2 too.").arg(name, on.label), 6000);
                if (live_->active() && on.server == liveServer_) loadLivePartsCatalog();
            }, failed);
        },
        failed);
}

void MainWindow::checkMissingParts() {
    if (checkingParts_ || !serverLibrary_ || serverLibrary_->state() != ServerLibrary::State::Ready) return;
    // Only a server that takes parts (servers that don't say: all of them).
    const sync::ServerList list = sync::ServerList::load();
    if (const sync::ServerEntry* e = list.find(serverLibrary_->server()); e && e->features && !e->features->contains(QStringLiteral("uploadParts")))
        return;
    const auto local = scanOwnParts();
    if (local.isEmpty()) {
        missingParts_.clear();
        refreshServerLocal();
        return;
    }
    checkingParts_ = true;
    auto* check = new sync::PartsUpload(serverLibrary_->server(), serverLibrary_->token(), this);
    check->setServerTakesSets(serverTakesSets(serverLibrary_->server()));
    connect(check, &sync::PartsUpload::missingReady, this, [this, check](const QList<sync::LocalPart>& missing) {
        check->deleteLater();
        checkingParts_ = false;
        missingParts_ = missing;
        refreshServerLocal();
    });
    connect(check, &sync::PartsUpload::failed, this, [this, check](const QString&, bool) {
        check->deleteLater();
        checkingParts_ = false;
    });
    check->findMissing(local);
}

void MainWindow::sendLocalToServer(int tab, const QString& id) {
    if (!readyToSend()) return;
    switch (static_cast<Tab>(tab)) {
    case Tab::Layouts:
        if (id == QLatin1String("open")) {
            onPublishToServer();
        } else {
            if (!maybeSave() || !openFile(id)) return;
            onPublishToServer();
        }
        break;
    case Tab::Modules:
        if (id.startsWith(QLatin1String("placed:"))) saveModuleToLibrary(id.mid(7));
        else if (id.startsWith(QLatin1String("file:"))) saveModuleFileToServer(id.mid(5));
        break;
    case Tab::Venues: {
        QString error;
        const auto venue = saveload::readVenueFile(id, &error);
        if (!venue) {
            QMessageBox::warning(this, tr("Save to Server"), tr("Couldn't read %1: %2").arg(QDir::toNativeSeparators(id), error));
            return;
        }
        core::Venue v = *venue;
        // The file's name is the one people know it by.
        if (v.name.trimmed().isEmpty()) v.name = QFileInfo(id).completeBaseName();
        saveVenueToServer(v);
        break;
    }
    case Tab::Parts:
        uploadPartsTo(serverLibrary_->server(), serverLibrary_->token(), false, { id });
        break;
    default:
        break;
    }
}

void MainWindow::saveLayoutFileToServer() {
    if (!readyToSend()) return;
    const QString path = askLayoutFileToSend
                             ? askLayoutFileToSend()
                             : QFileDialog::getOpenFileName(this, tr("Save a file to the server"), {},
                                                            tr("All supported maps (*.bld-layout *.bbm *.ldr *.mpd *.tdl *.ncp);;"
                                                               "Collaborative Brick Layout Designer layout (*.bld-layout);;BlueBrick map (*.bbm)"));
    if (path.isEmpty() || !maybeSave() || !openFile(path)) return;
    onPublishToServer();
}

void MainWindow::uploadPartsTo(const QUrl& server, const QString& token, bool quiet, const QStringList& only) {
    auto local = scanOwnParts();
    if (!only.isEmpty()) local.erase(std::remove_if(local.begin(), local.end(), [&](const auto& p) { return !only.contains(p.key); }), local.end());
    if (local.isEmpty()) {
        if (!quiet) statusBar()->showMessage(tr("You have no parts of your own to upload."), 4000);
        return;
    }
    auto* upload = new sync::PartsUpload(server, token, this);
    upload->setServerTakesSets(serverTakesSets(server));
    const auto checkFailed =
        connect(upload, &sync::PartsUpload::failed, this, [this, upload, quiet](const QString& message, bool) {
            if (!quiet) statusBar()->showMessage(tr("Could not check the server's parts: %1").arg(message), 6000);
            upload->deleteLater();
        });
    connect(upload, &sync::PartsUpload::missingReady, this,
            [this, upload, quiet, checkFailed, server, token](const QList<sync::LocalPart>& missing) {
                // The check is over: a refused upload later must not delete the
                // PartsUpload under the dialog that is still using it.
                disconnect(checkFailed);
                if (missing.isEmpty()) {
                    if (!quiet) statusBar()->showMessage(tr("The server already has all your parts."), 4000);
                    upload->deleteLater();
                    return;
                }
                // missingReady comes from inside a network reply's finished
                // signal, and that reply is already deleteLater'd: open the
                // dialog (and its event loop) once the signal has returned.
                QTimer::singleShot(0, this, [this, upload, missing, server, token] {
                    sync::ServerApi api;
                    api.setBase(server);
                    api.setToken(token);
                    sync::UploadPartsDialog dialog(api, *upload, missing, this);
                    if (runUploadPartsDialog ? runUploadPartsDialog(dialog) : dialog.exec() == QDialog::Accepted) {
                        statusBar()->showMessage(tr("Uploaded %n part(s) to the server", nullptr, dialog.uploadedCount()), 5000);
                        if (live_->active() && server == liveServer_) loadLivePartsCatalog();
                        checkMissingParts();
                    }
                    upload->deleteLater();
                });
            });
    upload->findMissing(local);
}

void MainWindow::saveVenueToServer(const core::Venue& venue) {
    if (!readyToSend()) return;
    std::optional<std::pair<QString, QString>> where;
    if (askSaveToServer) {
        where = askSaveToServer(QStringLiteral("venue"), venue.name);
    } else {
        SaveToServerDialog dialog(tr("venue"), sync::AddKind::Venue, venue.name, serverLibrary_->label(), serverLibrary_->orgs(), defaultSaveOwner(), this);
        dialog.showLogosFrom(serverLibrary_->server());
        if (dialog.exec() == QDialog::Accepted) where = std::make_pair(dialog.name(), dialog.orgSlug());
    }
    if (!where) return;
    const QJsonObject venueData = venueJson(venue);
    if (venueData.isEmpty()) {
        QMessageBox::warning(this, tr("Save to Server"), tr("The venue couldn't be written out to send it."));
        return;
    }
    const QString name = where->first;
    serverLibrary_->api().createVenue(name, venueData, where->second, sync::whileAlive(this, [this, name](const QString&) {
        statusBar()->showMessage(tr("Saved “%1” on %2").arg(name, serverLibrary_->label()), 6000);
        if (serverWindow_) serverWindow_->hint(QJsonObject{ { QStringLiteral("kind"), QStringLiteral("venue") } });
    }), sync::whileAlive(this, [this](const sync::ServerRefusal& r) {
        QMessageBox::warning(this, tr("Save to Server"), tr("Couldn't save the venue: %1").arg(ServerLibrary::refusalText(r)));
    }));
}

void MainWindow::saveModuleFileToServer(const QString& bbmPath) {
    auto read = saveload::readBbm(bbmPath);
    if (!read.ok()) {
        QMessageBox::warning(this, tr("Save to Server"), tr("Couldn't read %1: %2").arg(QDir::toNativeSeparators(bbmPath), read.error));
        return;
    }
    std::optional<std::pair<QString, QString>> where;
    const QString name = QFileInfo(bbmPath).completeBaseName();
    if (askSaveToServer) {
        where = askSaveToServer(QStringLiteral("module"), name);
    } else {
        SaveToServerDialog dialog(tr("module"), sync::AddKind::Module, name, serverLibrary_->label(), serverLibrary_->orgs(), defaultSaveOwner(), this);
        dialog.showLogosFrom(serverLibrary_->server());
        if (dialog.exec() == QDialog::Accepted) where = std::make_pair(dialog.name(), dialog.orgSlug());
    }
    if (!where) return;
    std::shared_ptr<core::Map> module(read.map.release());
    uploadModule(serverLibrary_->api(), module, QString(), where->first, where->second, QString(), {});
}

void MainWindow::shareToCatalog(const sync::CatalogShare& share, bool isUpdate, const QString& clubReview) {
    if (!readyToSend()) return;
    ShareToCatalogDialog dialog(serverLibrary_->api(), share, isUpdate, serverLibrary_->catalog().review, clubReview, this);
    if (share.kind == QLatin1String("layout")) {
        // A layout's card shows its picture, drawn here from the server's copy.
        const sync::ServerList list = sync::ServerList::load();
        const sync::ServerEntry* e = list.find(serverLibrary_->server());
        const bool native = e && e->features && e->features->contains(QStringLiteral("layoutDownload"));
        dialog.makeThumbnail = [this, id = share.sourceId, native](const std::function<void(const QByteArray&)>& done) {
            serverLibrary_->api().layoutFile(id, native, sync::whileAlive(this, [this, native, done](const QByteArray& bytes) {
                std::unique_ptr<core::Map> map;
                if (native) {
                    QTemporaryDir assets;
                    map = import::readLayoutFileBytes(bytes, assets.path()).map;
                } else {
                    QBuffer buf;
                    buf.setData(bytes);
                    buf.open(QIODevice::ReadOnly);
                    map = saveload::readBbm(buf).map;
                }
                done(map ? pngBytes(renderModuleThumbnail(*map, parts_, 1024)) : QByteArray());
            }), [done](const sync::ServerRefusal&) { done({}); });
        };
    }
    connect(&dialog, &ShareToCatalogDialog::shared, this, [this] {
        if (serverWindow_) serverWindow_->hint(QJsonObject{ { QStringLiteral("kind"), QStringLiteral("catalog") } });
    });
    dialog.exec();
}

}  // namespace bld::ui
