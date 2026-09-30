// File › Connect to Server… and the live-layout state of the main window
// (sync phase P4). Built only with -DBLD_SYNC=ON.

#include "MainWindow.h"

#include "LayerPanel.h"
#include "LiveLayout.h"
#include "MapView.h"

#include "../core/Map.h"
#include "../saveload/BbmWriter.h"
#include "../saveload/SidecarIO.h"
#include "ModulesPanel.h"
#include "VenueLibraryPanel.h"

#include "ConnectDialog.h"
#include "PartsUpload.h"
#include "ServerApi.h"
#include "TokenStore.h"
#include "UploadPartsDialog.h"

#include <QAction>
#include <QBuffer>
#include <QDesktopServices>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>
#include <QUndoStack>

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
    connect(live_, &LiveLayout::ended, this, [this](const QString& reason) {
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
    connect(upload, &sync::PartsUpload::failed, this, [this, upload, quiet](const QString& message, bool) {
        if (!quiet) statusBar()->showMessage(tr("Could not check the server's parts: %1").arg(message), 6000);
        upload->deleteLater();
    });
    connect(upload, &sync::PartsUpload::missingReady, this,
            [this, upload, quiet](const QList<sync::LocalPart>& missing) {
                if (missing.isEmpty()) {
                    if (!quiet) statusBar()->showMessage(tr("The server already has all your parts."), 4000);
                    upload->deleteLater();
                    return;
                }
                sync::ServerApi api;
                api.setBase(liveServer_);
                api.setToken(liveToken_);
                sync::UploadPartsDialog dialog(api, *upload, missing, this);
                if (dialog.exec() == QDialog::Accepted)
                    statusBar()->showMessage(
                        tr("Uploaded %n part(s) to the server", nullptr, dialog.uploadedCount()), 5000);
                upload->deleteLater();
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
    live_->open(api.layoutSocketUrl(r.layoutId), r.token, r.readOnly, r.title);
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
    updateLiveUi();
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

void MainWindow::onDisconnect() {
    live_->close();
    updateLiveUi();
    statusBar()->showMessage(tr("Disconnected. The layout stays open here as an unsaved copy."), 5000);
}

void MainWindow::onLiveReloaded() {
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
    updateTitle();
}

} // namespace bld::ui
