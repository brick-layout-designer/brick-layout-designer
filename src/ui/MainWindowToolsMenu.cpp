// Tools menu — manage libraries, reload library, Import submenu
// (LDraw / Studio / LDD → composite library part), Export Part List,
// Preferences. Import has the heaviest payload (render to sprite,
// rebuild connectivity, write composite part XML + GIF, rescan
// library) — that's why this chunk warranted its own TU.
//
// All imports share one `importAsPart` closure so LDraw / Studio /
// LDD go through identical rendering + connectivity + library-
// persist code. Imports ALWAYS produce a composite library part,
// never a loose map — that's the user-confirmed intent.

#include "MainWindow.h"

#include "MapView.h"
#include "ModuleLibraryPanel.h"
#include "PartsBrowser.h"
#include "PreferencesDialog.h"

#include "../parts/PartsLibrary.h"

#include "DownloadCenterDialog.h"

#include <QAction>

#include <tuple>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>

#include <algorithm>
#include <cmath>
#include <memory>

namespace bld::ui {

void MainWindow::setupToolsMenu() {
    auto* tools = menuBar()->addMenu(tr("&Tools"));
    auto* libAct = tools->addAction(tr("Manage Parts &Libraries..."));
    connect(libAct, &QAction::triggered, this, &MainWindow::onManageLibraries);
    auto* reloadAct = tools->addAction(tr("&Reload Parts Library"));
    connect(reloadAct, &QAction::triggered, this, &MainWindow::onReloadLibrary);
    tools->addSeparator();

    auto* importMenu = tools->addMenu(tr("&Import"));
    const auto addImport = [this, importMenu](const QString& label, const QString& title,
                                              const QString& filter) {
        auto* act = importMenu->addAction(label);
        connect(act, &QAction::triggered, this, [this, title, filter]{
            const QString in = QFileDialog::getOpenFileName(this, title, {}, filter);
            if (!in.isEmpty()) importModelFile(in);
        });
    };
    addImport(tr("&LDraw (.ldr / .dat / .mpd)..."), tr("Import LDraw file"),
              tr("LDraw (*.ldr *.dat *.mpd);;All files (*)"));
    addImport(tr("&Studio (.io)..."), tr("Import Studio .io"),
              tr("Studio (*.io);;All files (*)"));
    addImport(tr("L&DD (.lxf / .lxfml)..."), tr("Import LDD file"),
              tr("LDD (*.lxf *.lxfml);;All files (*)"));
    importMenu->addSeparator();
    auto* batchAct = importMenu->addAction(tr("&Batch Import..."));
    batchAct->setToolTip(tr("Turn many LDraw / Studio / LDD files into library parts at once"));
    connect(batchAct, &QAction::triggered, this, &MainWindow::onBatchImport);
    auto* reimportAct = importMenu->addAction(tr("&Re-import Changed Parts..."));
    reimportAct->setToolTip(tr("Re-import every imported part whose source model changed since"));
    connect(reimportAct, &QAction::triggered, this, &MainWindow::onReimportChangedParts);

    // Part list (BlueBrick's part usage export): HTML, text or CSV.
    auto* partListMenu = tools->addMenu(tr("&Part List"));
    auto* partListAct = partListMenu->addAction(tr("&Export Part List..."));
    connect(partListAct, &QAction::triggered, this, &MainWindow::onExportPartList);
    partListMenu->addSeparator();
    for (const auto& [label, key, def] : { std::tuple{ tr("&Split by Sheet"), "partList/splitPerLayer", false },
                                     std::tuple{ tr("Include &Hidden Sheets"), "partList/includeHiddenLayers", true } }) {
        auto* opt = partListMenu->addAction(label);
        opt->setCheckable(true);
        const QString settingsKey = QLatin1String(key);
        opt->setChecked(QSettings().value(settingsKey, def).toBool());
        connect(opt, &QAction::toggled, this, [settingsKey](bool on){ QSettings().setValue(settingsKey, on); });
    }

    tools->addSeparator();
    auto* dlAct = tools->addAction(tr("&Download Additional Parts..."));
    dlAct->setToolTip(tr("Search the official + community part-package servers and install zip archives into your library"));
    connect(dlAct, &QAction::triggered, this, [this]{
        // Pick a default install root the same way the simple download
        // helper used to: first configured user library path, or the
        // app-data fallback. The dialog uses this as the extraction
        // destination AND as the source for the "already installed"
        // version comparison.
        QStringList userPaths = loadUserLibraryPaths();
        QString destRoot = userPaths.isEmpty()
            ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                .filePath(QStringLiteral("parts"))
            : userPaths.first();
        DownloadCenterDialog dlg(destRoot, this);
        if (dlg.exec() == QDialog::Accepted && dlg.installedCount() > 0) {
            const QString root = dlg.libraryRoot();
            if (!userPaths.contains(root)) {
                userPaths.append(root);
                saveUserLibraryPaths(userPaths);
            }
            rescanLibrary(userPaths);
            statusBar()->showMessage(
                tr("Installed %1 package(s); library reloaded.")
                    .arg(dlg.installedCount()), 5000);
        }
    });

    // Preferences moved to Edit menu (BlueBrick parity).
}

}  // namespace bld::ui
