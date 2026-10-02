// File › Connect to Server… and the live-layout state of the main window
// (sync phase P4). Built only with -DBLD_SYNC=ON.

#include "MainWindow.h"

#include "LayerPanel.h"
#include "LiveLayout.h"
#include "LoadingCard.h"
#include "NoticeArea.h"
#include "core/Version.h"
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
#include "ServerList.h"
#include "ServersDialog.h"
#include "TokenStore.h"
#include "PrefsSync.h"
#include "theme/AppPrefs.h"
#include "UploadPartsDialog.h"

#include <QAction>
#include <QBuffer>
#include <QDir>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QToolButton>
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
    auto* serversAct = file->addAction(tr("Ser&vers..."));
    serversAct->setObjectName(QStringLiteral("manageServers"));
    serversAct->setToolTip(tr("Your servers: add one, sign in, or pick your Main one"));
    connect(serversAct, &QAction::triggered, this, &MainWindow::onManageServers);
    auto* connectAct = file->addAction(tr("&Connect to Server..."));
    connect(connectAct, &QAction::triggered, this, &MainWindow::onConnectToServer);
    disconnectAct_ = file->addAction(tr("&Disconnect"));
    disconnectAct_->setEnabled(false);
    connect(disconnectAct_, &QAction::triggered, this, &MainWindow::onDisconnect);
    auto* publishAct = file->addAction(tr("&Publish to Server..."));
    publishAct->setToolTip(
        tr("Put this layout on a server, yours or a club's, and keep editing it live"));
    connect(publishAct, &QAction::triggered, this, &MainWindow::onPublishToServer);
    uploadPartsAct_ = file->addAction(tr("&Upload My Parts to Server..."));
    uploadPartsAct_->setToolTip(tr("Offer your own parts that the live layout's server doesn't have yet"));
    uploadPartsAct_->setEnabled(false);
    connect(uploadPartsAct_, &QAction::triggered, this, [this] { offerPartsUpload(false); });
    downloadPartsAct_ = file->addAction(tr("&Download Server Parts Again"));
    downloadPartsAct_->setToolTip(tr("Fetch the live layout's server parts that are missing or changed"));
    downloadPartsAct_->setEnabled(false);
    connect(downloadPartsAct_, &QAction::triggered, this, [this] { syncServerParts(liveServer_, liveToken_); });
    partsSyncFailed_ = new QToolButton(this);
    partsSyncFailed_->setObjectName(QStringLiteral("partsSyncFailed"));
    partsSyncFailed_->setAutoRaise(true);
    partsSyncFailed_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    partsSyncFailed_->setVisible(false);
    connect(partsSyncFailed_, &QToolButton::clicked, this, [this] {
        if (live_->active()) syncServerParts(liveServer_, liveToken_);
    });
    statusBar()->addPermanentWidget(partsSyncFailed_);
    partsDownloadCard_ = new LoadingCard(mapView_, LoadingCard::Place::Bottom);
    partsDownloadCard_->setObjectName(QStringLiteral("partsDownloadCard"));
    connect(partsDownloadCard_, &QObject::destroyed, this, [this] { partsDownloadCard_ = nullptr; });
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
    sync::ConnectDialog dialog(api, *tokens_, [](const QUrl& u) { QDesktopServices::openUrl(u); }, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto chosen = dialog.result();
    if (!chosen) return;
    openLive(*chosen);
}

void MainWindow::onManageServers() {
    sync::ServersDialog dialog(*tokens_, [](const QUrl& u) { QDesktopServices::openUrl(u); }, this);
    dialog.exec();
    // A renamed live server shows its new name.
    updateLiveUi();
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
    sync::ConnectDialog dialog(
        api, *tokens_, [](const QUrl& u) { QDesktopServices::openUrl(u); }, this,
        sync::ConnectDialog::Purpose::Publish);
    dialog.setPublishContent(bbm.data(), sidecar, title);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto published = dialog.result();
    if (!published) return;
    // The published layout is now the live one; the local file stays as it was.
    openLive(*published);
    statusBar()->showMessage(tr("Published \"%1\" to %2").arg(published->title, liveServerName()),
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
    liveInfo_ = r.info;
    sync::ServerApi api;
    if (live_->active()) live_->close();
    api.setBase(r.server);
    currentFilePath_.clear();
    clearActiveView();
    // The layout and any offline edits are kept per server and layout.
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                             + QStringLiteral("/live/") + sync::ServerList::folderName(r.server) + QLatin1Char('/')
                             + r.layoutId;
    // One of your servers, with this layout first among its recent ones.
    {
        sync::ServerList servers = sync::ServerList::load();
        servers.touch(r.server);
        servers.remember(r.server, r.info);
        servers.addRecent(r.server, { r.layoutId, r.title, r.readOnly, {} });
        servers.save();
    }
    mapView_->showOpening(tr("Getting the layout from the server."));
    live_->open(api.layoutSocketUrl(r.layoutId), r.token, r.readOnly, r.title, cacheDir);
    // Name and colour our cursor as the web does, once we know who we are.
    auto* who = new sync::ServerApi(this);
    who->setBase(r.server);
    who->setToken(r.token);
    connect(who, &sync::ServerApi::currentUserReady, this,
            [this, who, layout = r.layoutId, server = r.server](const QString& id, const QString& name) {
                live_->setUser(id, name, layout);
                sync::ServerList servers = sync::ServerList::load();
                servers.rememberUser(server, name);
                servers.save();
                who->deleteLater();
            });
    connect(who, &sync::ServerApi::requestFailed, who, &QObject::deleteLater);
    who->fetchCurrentUser();
    // Only what the server has (servers that don't say: everything, as before).
    if (r.info.has(QStringLiteral("partsManifest"))) syncServerParts(r.server, r.token);
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
    startPrefsSync(r);
    showServerNotices(r.info);
    updateLiveUi();
}

void MainWindow::startPrefsSync(const sync::ConnectResult& r) {
    const sync::ServerList servers = sync::ServerList::load();
    const QUrl account = servers.settingsAccount();
    if (account.isEmpty() || sync::TokenStore::keyFor(account) == sync::TokenStore::keyFor(r.server)) {
        // Live on the Main server (or the only one): its token, as before.
        if (r.info.has(QStringLiteral("preferences"))) prefsSync_->start(r.server, r.token);
        else prefsSync_->stop();
        return;
    }
    // Live elsewhere: settings still follow Main, with Main's own token
    // (never this server's), when signed in there and it keeps settings.
    prefsSync_->stop();
    const sync::ServerEntry* main = servers.find(account);
    if (main && main->features && !main->features->contains(QStringLiteral("preferences"))) return;
    tokens_->load(account, [this, account](const QString& token) {
        // Still live, and nothing started meanwhile.
        if (token.isEmpty() || !live_->active() || prefsSync_->active()) return;
        prefsSync_->start(account, token);
    });
}

QString MainWindow::liveServerName() const {
    const sync::ServerList servers = sync::ServerList::load();
    const sync::ServerEntry* e = servers.find(liveServer_);
    return e ? e->label() : liveServer_.host();
}

void MainWindow::showServerNotices(const sync::ServerInfo& info) {
    const QString host = liveServerName();
    const QString mine = QCoreApplication::applicationVersion();
    if (info.standing(mine) == sync::Standing::UpdateSuggested) {
        const QUrl url(info.downloadUrl.isEmpty() ? core::desktopDownloadUrl() : info.downloadUrl);
        notices_->showNotice(
            QStringLiteral("server-update"), tr("Please update Brick Layout Designer"),
            tr("%1 works best with version %2 or newer, and you have %3. Everything still works for now.")
                .arg(host, info.desktopRecommended, mine),
            { NoticeAction{ tr("Download"), [url] { QDesktopServices::openUrl(url); }, true },
              NoticeAction{ tr("Later"), {} } });
    } else {
        notices_->hideNotice(QStringLiteral("server-update"));
    }
    const QStringList missing = info.missing();
    if (missing.isEmpty()) {
        notices_->hideNotice(QStringLiteral("server-features"));
        return;
    }
    QStringList names;
    for (const QString& f : missing) names << QStringLiteral("• ") + sync::featureLabel(f);
    notices_->showNotice(
        QStringLiteral("server-features"),
        info.features ? tr("This server can't do everything yet") : tr("This server may not do everything yet"),
        (info.features ? tr("%1 is running an older version, so these aren't available there:").arg(host)
                       : tr("%1 hasn't been updated in a while, so these may not work there:").arg(host)) +
            QLatin1Char('\n') + names.join(QLatin1Char('\n')) + QLatin1Char('\n') +
            tr("Everything else works as usual. Whoever runs the server can update it."),
        { NoticeAction{ tr("OK"), {} } });
}

void MainWindow::loadLivePartsCatalog() {
    liveCatalogReady_ = false;
    liveServerParts_.clear();
    if (!live_->active() || liveToken_.isEmpty() || liveLocalParts_.isEmpty()) return;
    if (!liveInfo_.has(QStringLiteral("uploadParts"))) return;  // nothing to offer on such a server
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
    sync::ConnectDialog dialog(
        api, *tokens_, [](const QUrl& u) { QDesktopServices::openUrl(u); }, this,
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
    if (partsSyncRunning_) return;
    partsSyncRunning_ = true;
    partsSyncFailed_->setVisible(false);
    downloadPartsAct_->setEnabled(false);
    // Each server's parts in a folder of its own, never mixed with another's.
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + QStringLiteral("/server-parts/") + sync::ServerList::folderName(server);
    auto* job = new sync::PartsSync(server, token, root, this);
    // The first connect fetches thousands of files: a card with a real
    // progress bar, like the web's, for as long as it takes.
    if (partsDownloadCard_) partsDownloadCard_->showBusy(tr("Checking the server's parts…"));
    const auto done = [this] {
        partsSyncRunning_ = false;
        if (partsDownloadCard_) partsDownloadCard_->finish();
        updateLiveUi();
    };
    connect(job, &sync::PartsSync::progress, this, [this](int done, int total) {
        if (partsDownloadCard_)
            partsDownloadCard_->showProgress(tr("Downloading server parts…"), done, total,
                                             tr("You can keep working while they download."));
    });
    connect(job, &sync::PartsSync::failed, this, [this, job, done](const QString& message, bool) {
        job->deleteLater();
        done();
        showPartsSyncFailed(tr("Couldn't get the server's parts"), { message });
    });
    connect(job, &sync::PartsSync::finished, this, [this, job, root, done](const sync::PartsSyncResult& r) {
        job->deleteLater();
        done();
        // The server's folder joins the library paths once, then the library reloads when anything came in.
        QStringList paths = loadUserLibraryPaths();
        const bool added = !paths.contains(root);
        if (added) {
            paths << root;
            saveUserLibraryPaths(paths);
        }
        if (added || r.downloaded > 0 || r.removed > 0) onReloadLibrary();
        if (r.failed.isEmpty()) {
            statusBar()->showMessage(
                tr("Server parts up to date (%n file(s) downloaded)", nullptr, r.downloaded), 6000);
        } else {
            showPartsSyncFailed(tr("%n server part file(s) didn't download", nullptr, static_cast<int>(r.failed.size())),
                                r.failed);
        }
    });
    job->start();
}

void MainWindow::showPartsSyncFailed(const QString& summary, const QStringList& details) {
    // Stays until the next try: parts that didn't come draw as outlines.
    partsSyncFailed_->setText(QStringLiteral("⚠ ") + summary + QStringLiteral(" · ") + tr("Try again"));
    QStringList shown = details.mid(0, 8);
    if (details.size() > shown.size()) shown << tr("…and %n more", nullptr, static_cast<int>(details.size() - shown.size()));
    partsSyncFailed_->setToolTip(tr("Parts the server has but this computer doesn't show as outlines.\n\n")
                                 + shown.join(QLatin1Char('\n')));
    partsSyncFailed_->setVisible(live_->active());
}

void MainWindow::onDisconnect() {
    live_->close();
    mapView_->hideOpening();
    if (partsDownloadCard_) partsDownloadCard_->finish();
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
    liveStatus_->setText(on ? tr("Live on %1: %2").arg(liveServerName(), live_->statusText()) : QString());
    disconnectAct_->setEnabled(on);
    uploadPartsAct_->setEnabled(on);
    downloadPartsAct_->setEnabled(on && !partsSyncRunning_);
    if (!on) partsSyncFailed_->setVisible(false);
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
