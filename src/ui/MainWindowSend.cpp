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

#include "../core/Map.h"
#include "../core/Venue.h"
#include "../edit/ModuleLibraryLink.h"
#include "../import/LayoutFile.h"
#include "../saveload/BbmReader.h"
#include "../saveload/VenueIO.h"

#include "LiveLayout.h"
#include "ModuleDoc.h"
#include "PartsUpload.h"
#include "ServerList.h"
#include "UploadPartsDialog.h"

#include <QBuffer>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QMessageBox>
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

void MainWindow::checkMissingParts() {
    if (checkingParts_ || !serverLibrary_ || serverLibrary_->state() != ServerLibrary::State::Ready) return;
    // Only a server that takes parts (servers that don't say: all of them).
    const sync::ServerList list = sync::ServerList::load();
    if (const sync::ServerEntry* e = list.find(serverLibrary_->server()); e && e->features && !e->features->contains(QStringLiteral("uploadParts")))
        return;
    const QString root = importedPartsRoot();
    const auto local = root.isEmpty() ? QList<sync::LocalPart>() : sync::PartsUpload::scanFolder(root);
    if (local.isEmpty()) {
        missingParts_.clear();
        refreshServerLocal();
        return;
    }
    checkingParts_ = true;
    auto* check = new sync::PartsUpload(serverLibrary_->server(), serverLibrary_->token(), this);
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
                                                               "Brick Layout Designer layout (*.bld-layout);;BlueBrick map (*.bbm)"));
    if (path.isEmpty() || !maybeSave() || !openFile(path)) return;
    onPublishToServer();
}

void MainWindow::uploadPartsTo(const QUrl& server, const QString& token, bool quiet, const QStringList& only) {
    const QString root = importedPartsRoot();
    auto local = root.isEmpty() ? QList<sync::LocalPart>() : sync::PartsUpload::scanFolder(root);
    if (!only.isEmpty()) local.erase(std::remove_if(local.begin(), local.end(), [&](const auto& p) { return !only.contains(p.key); }), local.end());
    if (local.isEmpty()) {
        if (!quiet) statusBar()->showMessage(tr("You have no parts of your own to upload."), 4000);
        return;
    }
    auto* upload = new sync::PartsUpload(server, token, this);
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
                    if (dialog.exec() == QDialog::Accepted) {
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
        SaveToServerDialog dialog(tr("venue"), venue.name, serverLibrary_->label(), serverLibrary_->orgs(), defaultSaveOwner(), this);
        if (dialog.exec() == QDialog::Accepted) where = std::make_pair(dialog.name(), dialog.orgSlug());
    }
    if (!where) return;
    const QJsonObject data = venueJson(venue);
    if (data.isEmpty()) {
        QMessageBox::warning(this, tr("Save to Server"), tr("The venue couldn't be written out to send it."));
        return;
    }
    const QString name = where->first;
    serverLibrary_->api().createVenue(name, data, where->second, [this, name](const QString&) {
        statusBar()->showMessage(tr("Saved “%1” on %2").arg(name, serverLibrary_->label()), 6000);
        if (serverWindow_) serverWindow_->hint(QJsonObject{ { QStringLiteral("kind"), QStringLiteral("venue") } });
    }, [this](const sync::ServerRefusal& r) {
        QMessageBox::warning(this, tr("Save to Server"), tr("Couldn't save the venue: %1").arg(ServerLibrary::refusalText(r)));
    });
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
        SaveToServerDialog dialog(tr("module"), name, serverLibrary_->label(), serverLibrary_->orgs(), defaultSaveOwner(), this);
        if (dialog.exec() == QDialog::Accepted) where = std::make_pair(dialog.name(), dialog.orgSlug());
    }
    if (!where) return;
    std::shared_ptr<core::Map> module(read.map.release());
    uploadModule(serverLibrary_->api(), module, QString(), where->first, where->second, QString(), {});
}

void MainWindow::shareToCatalog(const sync::CatalogShare& share, bool update, const QString& clubReview) {
    if (!readyToSend()) return;
    ShareToCatalogDialog dialog(serverLibrary_->api(), share, update, serverLibrary_->catalog().review, clubReview, this);
    if (share.kind == QLatin1String("layout")) {
        // A layout's card shows its picture, drawn here from the server's copy.
        const sync::ServerList list = sync::ServerList::load();
        const sync::ServerEntry* e = list.find(serverLibrary_->server());
        const bool native = e && e->features && e->features->contains(QStringLiteral("layoutDownload"));
        dialog.makeThumbnail = [this, id = share.sourceId, native](std::function<void(const QByteArray&)> done) {
            serverLibrary_->api().layoutFile(id, native, [this, native, done](const QByteArray& bytes) {
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
            }, [done](const sync::ServerRefusal&) { done({}); });
        };
    }
    connect(&dialog, &ShareToCatalogDialog::shared, this, [this] {
        if (serverWindow_) serverWindow_->hint(QJsonObject{ { QStringLiteral("kind"), QStringLiteral("catalog") } });
    });
    dialog.exec();
}

}  // namespace bld::ui
