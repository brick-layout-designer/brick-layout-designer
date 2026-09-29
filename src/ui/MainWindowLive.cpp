// File › Connect to Server… and the live-layout state of the main window
// (sync phase P4). Built only with -DBLD_SYNC=ON.

#include "MainWindow.h"

#include "LayerPanel.h"
#include "LiveLayout.h"
#include "MapView.h"
#include "ModulesPanel.h"
#include "VenueLibraryPanel.h"

#include "ConnectDialog.h"
#include "ServerApi.h"
#include "TokenStore.h"

#include <QAction>
#include <QDesktopServices>
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
    const sync::ConnectResult& r = *chosen;
    if (live_->active()) live_->close();
    api.setBase(r.server);
    currentFilePath_.clear();
    live_->open(api.layoutSocketUrl(r.layoutId), r.token, r.readOnly, r.title);
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
    undoAct_->setVisible(!on);
    redoAct_->setVisible(!on);
    liveUndoAct_->setVisible(on);
    liveRedoAct_->setVisible(on);
    liveUndoAct_->setEnabled(on && live_->canUndo());
    liveRedoAct_->setEnabled(on && live_->canRedo());
    updateTitle();
}

} // namespace bld::ui
