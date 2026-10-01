// File › Connect to Server… and the live-layout state of the main window
// (sync phase P4). Built only with -DBLD_SYNC=ON.

#include "MainWindow.h"

#include "LayerPanel.h"
#include "LiveLayout.h"
#include "MapView.h"
#include "MapViewInternal.h"

#include "../core/Map.h"
#include "../saveload/BbmWriter.h"
#include "../saveload/SidecarIO.h"
#include "ModulesPanel.h"
#include "VenueLibraryPanel.h"

#include "CompareDialog.h"
#include "ConnectDialog.h"
#include "LayoutMerge.h"
#include "PartsUpload.h"
#include "PartsSync.h"
#include "ServerApi.h"
#include "TokenStore.h"
#include "PrefsSync.h"
#include "theme/AppPrefs.h"
#include "UploadPartsDialog.h"

#include <QAction>
#include <QBuffer>
#include <QDir>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QUndoStack>

#include <algorithm>

namespace bld::ui {

void MainWindow::setupLiveMenu(QMenu* file) {
    live_ = new LiveLayout(*mapView_, this);
    liveStatus_ = new QLabel(this);
    liveStatus_->setVisible(false);
    statusBar()->addPermanentWidget(liveStatus_);

    file->addSeparator();
    auto* connectAct = file->addAction(tr("&Connect to Server..."));
    connect(connectAct, &QAction::triggered, this, &MainWindow::onConnectToServer);
    disconnectAct_ = file->addAction(tr("&Disconnect"));
    disconnectAct_->setEnabled(false);
    connect(disconnectAct_, &QAction::triggered, this, &MainWindow::onDisconnect);
    auto* publishAct = file->addAction(tr("&Publish to Server..."));
    publishAct->setToolTip(
        tr("Put this layout on a server, yours or an organisation's, and keep editing it live"));
    connect(publishAct, &QAction::triggered, this, &MainWindow::onPublishToServer);
    uploadPartsAct_ = file->addAction(tr("&Upload My Parts to Server..."));
    uploadPartsAct_->setToolTip(tr("Offer your own parts that the live layout's server doesn't have yet"));
    uploadPartsAct_->setEnabled(false);
    connect(uploadPartsAct_, &QAction::triggered, this, [this] { offerPartsUpload(false); });
    reviewOfflineAct_ = file->addAction(tr("Review &Offline Changes..."));
    reviewOfflineAct_->setToolTip(tr("Compare what you changed offline with the server's layout"));
    reviewOfflineAct_->setEnabled(false);
    connect(reviewOfflineAct_, &QAction::triggered, this, &MainWindow::onReviewOfflineEdits);
    auto* venuesAct = file->addAction(tr("Download &Venues from Server..."));
    connect(venuesAct, &QAction::triggered, this, &MainWindow::onDownloadVenues);

    // While live, Undo / Redo revert this desktop's own edits on the server.
    liveUndoAct_ = new QAction(tr("&Undo"), this);
    liveUndoAct_->setShortcut(QKeySequence::Undo);
    liveUndoAct_->setVisible(false);
    connect(liveUndoAct_, &QAction::triggered, this, [this] { live_->undo(); });
    liveRedoAct_ = new QAction(tr("&Redo"), this);
    liveRedoAct_->setShortcut(QKeySequence::Redo);
    liveRedoAct_->setVisible(false);
    connect(liveRedoAct_, &QAction::triggered, this, [this] { live_->redo(); });

    connect(live_, &LiveLayout::mapReloaded, this, &MainWindow::onLiveReloaded);
    connect(live_, &LiveLayout::statusTextChanged, this, [this] { updateLiveUi(); });
    connect(live_, &LiveLayout::undoStateChanged, this, [this] { updateLiveUi(); });
    connect(live_, &LiveLayout::offlineEditsReady, this, &MainWindow::onReviewOfflineEdits);
    // Queued: after the edit's own signal handling, not inside it.
    connect(live_, &LiveLayout::localEdited, this, &MainWindow::offerPlacedParts, Qt::QueuedConnection);
    connect(live_, &LiveLayout::ended, this, [this](const QString& reason) {
        if (prefsSync_) prefsSync_->stop();
        updateLiveUi();
        QMessageBox::information(
            this, tr("Live layout closed"),
            tr("The server ended the live session%1. The layout stays open here as an unsaved copy.")
                .arg(reason.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(reason)));
    });
}

void MainWindow::onConnectToServer() {
    if (!maybeSave()) return;
    sync::ServerApi api;
    sync::KeychainTokenStore tokens;
    sync::ConnectDialog dialog(api, tokens, [](const QUrl& u) { QDesktopServices::openUrl(u); }, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto chosen = dialog.result();
    if (!chosen) return;
    openLive(*chosen);
}

void MainWindow::onPublishToServer() {
    auto* m = mapView_->currentMap();
    if (!m) return;
    if (live_->active()) {
        QMessageBox::information(this, tr("Publish to Server"),
                                 tr("This layout is already live on a server."));
        return;
    }
    QBuffer bbm;
    bbm.open(QIODevice::WriteOnly);
    if (!saveload::writeBbm(*m, bbm).ok) {
        QMessageBox::warning(this, tr("Publish to Server"), tr("Could not write the layout to publish it."));
        return;
    }
    const QByteArray sidecar =
        m->sidecar.isEmpty()
            ? QByteArray()
            : QJsonDocument(saveload::sidecarToJson(m->sidecar)).toJson(QJsonDocument::Compact);
    const QString title = currentFilePath_.isEmpty() ? (m->event.isEmpty() ? tr("Untitled Layout") : m->event)
                                                     : QFileInfo(currentFilePath_).completeBaseName();
    sync::ServerApi api;
    sync::KeychainTokenStore tokens;
    sync::ConnectDialog dialog(
        api, tokens, [](const QUrl& u) { QDesktopServices::openUrl(u); }, this,
        sync::ConnectDialog::Purpose::Publish);
    dialog.setPublishContent(bbm.data(), sidecar, title);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto published = dialog.result();
    if (!published) return;
    // The published layout is now the live one; the local file stays as it was.
    openLive(*published);
    statusBar()->showMessage(tr("Published \"%1\" to %2").arg(published->title, published->server.host()),
                             5000);
    // Parts of yours the server lacks would show as missing there: offer them.
    offerPartsUpload(true);
}

void MainWindow::offerPartsUpload(bool quiet) {
    if (!live_->active() || liveToken_.isEmpty()) return;
    const QString root = importedPartsRoot();
    const auto local = root.isEmpty() ? QList<sync::LocalPart>() : sync::PartsUpload::scanFolder(root);
    if (local.isEmpty()) {
        if (!quiet) statusBar()->showMessage(tr("You have no parts of your own to upload."), 4000);
        return;
    }
    auto* upload = new sync::PartsUpload(liveServer_, liveToken_, this);
    const auto checkFailed =
        connect(upload, &sync::PartsUpload::failed, this, [this, upload, quiet](const QString& message, bool) {
            if (!quiet)
                statusBar()->showMessage(tr("Could not check the server's parts: %1").arg(message), 6000);
            upload->deleteLater();
        });
    connect(upload, &sync::PartsUpload::missingReady, this,
            [this, upload, quiet, checkFailed](const QList<sync::LocalPart>& missing) {
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
                QTimer::singleShot(0, this, [this, upload = QPointer<sync::PartsUpload>(upload), missing] {
                    if (!upload) return;
                    sync::ServerApi api;
                    api.setBase(liveServer_);
                    api.setToken(liveToken_);
                    sync::UploadPartsDialog dialog(api, *upload, missing, this);
                    if (dialog.exec() == QDialog::Accepted) {
                        statusBar()->showMessage(
                            tr("Uploaded %n part(s) to the server", nullptr, dialog.uploadedCount()), 5000);
                        loadLivePartsCatalog();
                    }
                    upload->deleteLater();
                });
            });
    upload->findMissing(local);
}

void MainWindow::openLive(const sync::ConnectResult& r) {
    liveServer_ = r.server;
    liveToken_ = r.token;
    sync::ServerApi api;
    if (live_->active()) live_->close();
    api.setBase(r.server);
    currentFilePath_.clear();
    clearActiveView();
    // The layout and any offline edits are kept per server and layout.
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                             + QStringLiteral("/live/")
                             + QString(r.server.host()).replace(QLatin1Char(':'), QLatin1Char('_'))
                             + QLatin1Char('/') + r.layoutId;
    live_->open(api.layoutSocketUrl(r.layoutId), r.token, r.readOnly, r.title, cacheDir);
    // Name and colour our cursor as the web does, once we know who we are.
    auto* who = new sync::ServerApi(this);
    who->setBase(r.server);
    who->setToken(r.token);
    connect(who, &sync::ServerApi::currentUserReady, this,
            [this, who, layout = r.layoutId](const QString& id, const QString& name) {
                live_->setUser(id, name, layout);
                who->deleteLater();
            });
    connect(who, &sync::ServerApi::requestFailed, who, &QObject::deleteLater);
    who->fetchCurrentUser();
    syncServerParts(r.server, r.token);
    liveAskedParts_.clear();
    liveLayoutSeen_ = false;
    liveLocalParts_.clear();
    // Your own parts: the imported ones and your library folders, not the
    // bundled library or the folders of server parts.
    const QString vendored = defaultVendoredPartsRoot();
    const QString serverParts = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                                + QStringLiteral("/server-parts"));
    QStringList roots{ importedPartsRoot() };
    for (const QString& p : loadUserLibraryPaths())
        if (!roots.contains(p) && p != vendored && !QDir::cleanPath(p).startsWith(serverParts)) roots << p;
    for (const QString& root : std::as_const(roots))
        if (!root.isEmpty() && QDir(root).exists()) liveLocalParts_ << sync::PartsUpload::scanFolder(root);
    loadLivePartsCatalog();
    // Settings follow the account on this server while connected.
    if (!prefsSync_) {
        prefsSync_ = new PrefsSync(theme::PrefsStore::instance(), this);
        connect(prefsSync_, &PrefsSync::failed, this, [this](const QString&, bool unauthorized) {
            if (unauthorized)
                statusBar()->showMessage(
                    tr("Your settings didn't sync with the server. Sign in again to sync them."), 6000);
        });
    }
    prefsSync_->start(r.server, r.token);
    updateLiveUi();
}

void MainWindow::loadLivePartsCatalog() {
    liveCatalogReady_ = false;
    liveServerParts_.clear();
    if (!live_->active() || liveToken_.isEmpty() || liveLocalParts_.isEmpty()) return;
    auto* catalog = new sync::PartsUpload(liveServer_, liveToken_, this);
    const QUrl server = liveServer_;
    connect(catalog, &sync::PartsUpload::catalogReady, this, [this, catalog, server](const QSet<QString>& known) {
        catalog->deleteLater();
        if (server != liveServer_) return; // another layout opened meanwhile
        liveServerParts_ = known;
        liveCatalogReady_ = true;
    });
    // Without the catalog nothing is offered; File > Upload My Parts still works.
    connect(catalog, &sync::PartsUpload::failed, catalog, &QObject::deleteLater);
    catalog->fetchCatalog();
}

void MainWindow::offerPlacedParts() {
    if (!live_->active() || live_->readOnly() || !liveCatalogReady_ || liveOfferOpen_) return;
    const core::Map* map = mapView_->currentMap();
    if (!map) return;
    const auto parts =
        sync::partsToOffer(*map, liveServerParts_, liveLocalParts_, liveAskedParts_, defaultVendoredPartsRoot());
    if (parts.isEmpty()) return;
    // Asked once per session, whatever the answer.
    QStringList names;
    for (const auto& p : parts) {
        liveAskedParts_.insert(p.key.toUpper());
        names << p.key;
    }
    auto* box = new QMessageBox(QMessageBox::Question, tr("Upload to Server"),
                                parts.size() == 1
                                    ? tr("%1 isn't on the server yet, so others see a missing part. Upload it?")
                                          .arg(names.first())
                                    : tr("%1 aren't on the server yet, so others see missing parts. Upload them?")
                                          .arg(names.join(QStringLiteral(", "))),
                                QMessageBox::NoButton, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    auto* uploadBtn = box->addButton(tr("Upload..."), QMessageBox::AcceptRole);
    box->addButton(tr("Not Now"), QMessageBox::RejectRole);
    liveOfferOpen_ = true;
    connect(box, &QMessageBox::finished, this, [this, box, uploadBtn, parts] {
        liveOfferOpen_ = false;
        if (box->clickedButton() != uploadBtn || !live_->active()) return;
        // The box deletes itself on close: the upload dialog's own event loop
        // must not run inside its finished signal, or the box is destroyed
        // while it is still closing. Open it once the signal has returned.
        QTimer::singleShot(0, this, [this, parts] {
            if (!live_->active()) return;
            auto* upload = new sync::PartsUpload(liveServer_, liveToken_, this);
            sync::ServerApi api;
            api.setBase(liveServer_);
            api.setToken(liveToken_);
            sync::UploadPartsDialog dialog(api, *upload, parts, this);
            if (dialog.exec() == QDialog::Accepted) {
                statusBar()->showMessage(
                    tr("Uploaded %n part(s) to the server", nullptr, dialog.uploadedCount()), 5000);
                loadLivePartsCatalog();
            }
            upload->deleteLater();
        });
    });
    box->open();
}

void MainWindow::onDownloadVenues() {
    sync::ServerApi api;
    sync::KeychainTokenStore tokens;
    sync::ConnectDialog dialog(
        api, tokens, [](const QUrl& u) { QDesktopServices::openUrl(u); }, this,
        sync::ConnectDialog::Purpose::DownloadVenues);
    if (dialog.exec() != QDialog::Accepted) return;
    QStringList saved, failed;
    for (const auto& v : dialog.venues()) {
        if (venueLibraryPanel_->addVenueFile(v.name, v.file).isEmpty()) failed << v.name;
        else saved << v.name;
    }
    venueLibraryPanel_->show();
    venueLibraryPanel_->raise();
    if (!failed.isEmpty()) {
        QMessageBox::warning(this, tr("Download venues"),
                             tr("Could not save %1 in the venue library folder %2.")
                                 .arg(failed.join(QStringLiteral(", ")), venueLibraryPanel_->libraryPath()));
    }
    if (!saved.isEmpty())
        statusBar()->showMessage(
            tr("Added %n venue(s) to the Venue Library", nullptr, static_cast<int>(saved.size())), 5000);
}

void MainWindow::syncServerParts(const QUrl& server, const QString& token) {
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + QStringLiteral("/server-parts/")
                         + QString(server.host()).replace(QLatin1Char(':'), QLatin1Char('_'));
    auto* job = new sync::PartsSync(server, token, root, this);
    connect(job, &sync::PartsSync::progress, this, [this](int done, int total) {
        statusBar()->showMessage(tr("Downloading server parts: %1 of %2").arg(done).arg(total), 2000);
    });
    connect(job, &sync::PartsSync::failed, this, [this, job](const QString& message, bool) {
        statusBar()->showMessage(tr("Could not read the server's parts: %1").arg(message), 6000);
        job->deleteLater();
    });
    connect(job, &sync::PartsSync::finished, this, [this, job, root](const sync::PartsSyncResult& r) {
        job->deleteLater();
        // The server's folder joins the library paths once, then the library reloads when anything came in.
        QStringList paths = loadUserLibraryPaths();
        const bool added = !paths.contains(root);
        if (added) {
            paths << root;
            saveUserLibraryPaths(paths);
        }
        if (added || r.downloaded > 0 || r.removed > 0) onReloadLibrary();
        statusBar()->showMessage(
            r.failed.isEmpty() ? tr("Server parts up to date (%n file(s) downloaded)", nullptr, r.downloaded)
                               : tr("Server parts: %1 downloaded, %2 failed (%3)")
                                     .arg(r.downloaded)
                                     .arg(r.failed.size())
                                     .arg(r.failed.first()),
            6000);
    });
    job->start();
}

void MainWindow::onDisconnect() {
    live_->close();
    if (prefsSync_) prefsSync_->stop();
    updateLiveUi();
    statusBar()->showMessage(tr("Disconnected. The layout stays open here as an unsaved copy."), 5000);
}

void MainWindow::onLiveReloaded() {
    // Parts already in the layout as opened aren't "placed": File > Upload
    // My Parts covers those.
    if (!liveLayoutSeen_ && live_->active() && mapView_->currentMap()) {
        liveAskedParts_ |= sync::partNumbersIn(*mapView_->currentMap());
        liveLayoutSeen_ = true;
    }
    layerPanel_->setMap(mapView_->currentMap(), mapView_->builder());
    modulesPanel_->setMap(mapView_->currentMap());
    updateTitle();
}

void MainWindow::updateLiveUi() {
    const bool on = live_->active();
    liveStatus_->setVisible(on);
    liveStatus_->setText(on ? tr("Live: %1").arg(live_->statusText()) : QString());
    disconnectAct_->setEnabled(on);
    uploadPartsAct_->setEnabled(on);
    undoAct_->setVisible(!on);
    redoAct_->setVisible(!on);
    liveUndoAct_->setVisible(on);
    liveRedoAct_->setVisible(on);
    liveUndoAct_->setEnabled(on && live_->canUndo());
    liveRedoAct_->setEnabled(on && live_->canRedo());
    reviewOfflineAct_->setEnabled(on && live_->session().offlineEdits()
                                  && live_->session().status() == sync::SyncClient::Status::Synced);
    updateTitle();
}

void MainWindow::onReviewOfflineEdits() {
    if (!live_->active()) return;
    auto& session = live_->session();
    const auto& off = session.offlineEdits();
    if (!off) return;
    const auto current = session.currentMap();
    if (!current) return;
    const auto server = sync::merge::snapshotOf(*current);
    const auto base = off->base, mine = off->mine;
    const auto changes = sync::merge::compareLayouts(base, mine, server);
    const bool anyMine = std::any_of(changes.cbegin(), changes.cend(), [](const sync::merge::ItemChange& c) {
        return c.mine != sync::merge::Side::Unchanged;
    });
    if (!anyMine) {
        session.resolveOffline(nullptr);
        statusBar()->showMessage(tr("Your offline edits left the layout as it was; nothing to apply."), 5000);
        updateLiveUi();
        return;
    }

    sync::CompareDialog dlg(changes, this);
    connect(&dlg, &sync::CompareDialog::highlight, this, [this](const sync::merge::ItemChange& c) {
        // brick:<layer>:<id>
        if (c.kind == QLatin1String("brick")) showBrick(c.key.section(QLatin1Char(':'), 2));
    });
    dlg.exec();
    // The session may have ended or resolved meanwhile.
    if (!live_->active() || !session.offlineEdits()) return;

    QString error;
    switch (dlg.action()) {
    case sync::CompareDialog::Action::Later:
        break;
    case sync::CompareDialog::Action::Apply: {
        const auto merged = sync::merge::mergeLayouts(mine, server, changes, dlg.choices(), &error);
        if (!merged) {
            QMessageBox::warning(this, tr("Review Offline Changes"),
                                 tr("Could not merge your changes: %1").arg(error));
            break;
        }
        session.resolveOffline(merged.get());
        statusBar()->showMessage(tr("Your offline changes were applied to the live layout."), 5000);
        break;
    }
    case sync::CompareDialog::Action::Discard:
        session.resolveOffline(nullptr);
        statusBar()->showMessage(tr("Your offline changes were discarded."), 5000);
        break;
    case sync::CompareDialog::Action::ReplaceServer: {
        const auto map = sync::merge::mapOf(mine, &error);
        if (!map) {
            QMessageBox::warning(this, tr("Review Offline Changes"),
                                 tr("Could not read your version: %1").arg(error));
            break;
        }
        session.resolveOffline(map.get());
        statusBar()->showMessage(tr("Your version replaced the server's."), 5000);
        break;
    }
    case sync::CompareDialog::Action::SaveAsNew:
        saveOfflineAsNew(mine);
        break;
    }
    updateLiveUi();
}

void MainWindow::saveOfflineAsNew(const sync::merge::Snapshot& mine) {
    QString error;
    const auto map = sync::merge::mapOf(mine, &error);
    QBuffer bbm;
    bbm.open(QIODevice::WriteOnly);
    if (!map || !saveload::writeBbm(*map, bbm).ok) {
        QMessageBox::warning(this, tr("Save as a New Layout"),
                             tr("Could not write your version to publish it. Your offline changes are kept."));
        return;
    }
    const QByteArray sidecar =
        map->sidecar.isEmpty()
            ? QByteArray()
            : QJsonDocument(saveload::sidecarToJson(map->sidecar)).toJson(QJsonDocument::Compact);
    const QString title = tr("%1 (offline copy)").arg(live_->title());
    auto* api = new sync::ServerApi(this);
    api->setBase(liveServer_);
    api->setToken(liveToken_);
    const QUrl server = liveServer_;
    connect(api, &sync::ServerApi::published, this, [this, api, server](const QString&, const QString& t) {
        api->deleteLater();
        if (live_->active() && live_->session().offlineEdits()) live_->session().resolveOffline(nullptr);
        updateLiveUi();
        QMessageBox::information(this, tr("Save as a New Layout"),
                                 tr("Your version was saved as \"%1\" in your layouts on %2. The live layout "
                                    "stays as the server has it.")
                                     .arg(t, server.host()));
    });
    connect(api, &sync::ServerApi::requestFailed, this, [this, api](const QString&, const QString& message, bool) {
        api->deleteLater();
        QMessageBox::warning(this, tr("Save as a New Layout"),
                             tr("Could not publish your version: %1\nYour offline changes are kept; "
                                "File > Review Offline Changes brings them back.")
                                 .arg(message));
    });
    // Empty organisation: the user's own layouts.
    api->publishLayout(title, bbm.data(), sidecar, QString());
    statusBar()->showMessage(tr("Publishing your version as \"%1\"...").arg(title), 5000);
}

void MainWindow::showBrick(const QString& guid) {
    auto* scene = mapView_->scene();
    QGraphicsItem* found = nullptr;
    for (QGraphicsItem* it : scene->items())
        if (detail::isBrickItem(it) && it->data(detail::kBrickDataGuid).toString() == guid) {
            found = it;
            break;
        }
    if (!found) return;
    scene->clearSelection();
    found->setSelected(true);
    mapView_->centerOn(found);
}

} // namespace bld::ui
