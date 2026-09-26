// Parts-library management: manage paths, rescan, load/save user-added paths,
// and the related reload-all menu action. Split out of MainWindow.cpp so
// the main window's ctor / dock wiring is readable.

#include "MainWindow.h"

#include "LibraryPathsDialog.h"
#include "MapView.h"
#include "PartUsagePanel.h"
#include "PartsBrowser.h"

#include "../parts/PartsLibrary.h"

#include <QFileInfo>

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>

namespace bld::ui {

void MainWindow::onManageLibraries() {
    QStringList current;
    const QString vendored = defaultVendoredPartsRoot();
    if (!vendored.isEmpty() && QDir(vendored).exists()) current << vendored;
    current += loadUserLibraryPaths();

    LibraryPathsDialog dlg(current, this);
    if (dlg.exec() != QDialog::Accepted) return;

    QStringList newPaths = dlg.paths();
    // Persist user additions only — filter the vendored path so it isn't
    // duplicated on next launch.
    QStringList userOnly;
    for (const QString& p : newPaths) {
        if (p != vendored) userOnly << p;
    }
    saveUserLibraryPaths(userOnly);
    rescanLibrary(newPaths);
    partsBrowser_->rebuild();
    statusBar()->showMessage(
        tr("Reloaded library: %1 parts across %2 path(s)")
            .arg(parts_.partCount()).arg(newPaths.size()), 4000);
}

void MainWindow::rescanLibrary(const QStringList& paths) {
    parts_.clear();
    for (const QString& p : paths) parts_.addSearchPath(p);
    parts_.scan();
    // Constructor calls this before the docks exist.
    if (partUsagePanel_) partUsagePanel_->clearIconCache();
}

QStringList MainWindow::loadUserLibraryPaths() const {
    QSettings s;
    s.beginGroup(LibraryPathsDialog::kSettingsGroup);
    const QStringList v = s.value(LibraryPathsDialog::kSettingsKey).toStringList();
    s.endGroup();
    return v;
}

void MainWindow::saveUserLibraryPaths(const QStringList& paths) {
    QSettings s;
    s.beginGroup(LibraryPathsDialog::kSettingsGroup);
    s.setValue(LibraryPathsDialog::kSettingsKey, paths);
    s.endGroup();
}

QString MainWindow::defaultVendoredPartsRoot() const {
    const QString exeDir = QCoreApplication::applicationDirPath();
    // Probes in preference order:
    //   macOS .app bundle: Contents/MacOS/ → ../Resources/parts/BlueBrickParts/parts
    //     (../Resources/BlueBrickParts/parts in bundles before 1.1)
    //   AppImage: usr/bin/ → ../share/brick-layout-designer/parts/BlueBrickParts/parts
    //   Linux tar.gz / Windows zip: <exeDir>/parts/BlueBrickParts/parts
    //   Flat zip variant: <exeDir>/BlueBrickParts/parts
    //   Build-tree fallback (macOS cmake --build): ../../../../parts/BlueBrickParts/parts
    for (const QString& rel : {
             QStringLiteral("/../Resources/parts/BlueBrickParts/parts"),
             QStringLiteral("/../Resources/BlueBrickParts/parts"),
             QStringLiteral("/../share/brick-layout-designer/parts/BlueBrickParts/parts"),
             QStringLiteral("/parts/BlueBrickParts/parts"),
             QStringLiteral("/BlueBrickParts/parts"),
             QStringLiteral("/../../../parts/BlueBrickParts/parts") }) {
        if (QDir(exeDir + rel).exists()) return QDir(exeDir + rel).absolutePath();
    }
    return {};
}

QString MainWindow::registerImportedPart(const QString& xmlAbsPath) {
    if (xmlAbsPath.isEmpty()) return {};

    // Make sure the parent directory is on the search-path list so future
    // full rescans (Reload Library, Manage Libraries) keep finding this
    // part. addSearchPath is a no-op when the path is already present.
    parts_.addSearchPath(QFileInfo(xmlAbsPath).absolutePath());

    const QString libKey = parts_.scanFile(xmlAbsPath);
    if (libKey.isEmpty()) return {};
    partsBrowser_->addOne(libKey);
    return libKey;
}

QString MainWindow::importedPartsRoot() const {
    const QString configured = QSettings().value(QStringLiteral("modules/libraryPath")).toString();
    const QString base = configured.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        : configured;
    if (base.isEmpty()) return {};
    return base + QStringLiteral("/imports");
}

void MainWindow::onReloadLibrary() {
    QStringList allPaths;
    const QString vendored = defaultVendoredPartsRoot();
    if (!vendored.isEmpty() && QDir(vendored).exists()) allPaths << vendored;
    for (const QString& p : loadUserLibraryPaths()) {
        if (!allPaths.contains(p) && QDir(p).exists()) allPaths << p;
    }
    const QString imports = importedPartsRoot();
    if (!imports.isEmpty() && QDir(imports).exists() && !allPaths.contains(imports)) {
        allPaths << imports;
    }
    rescanLibrary(allPaths);
    partsBrowser_->rebuild();
    mapView_->rebuildScene();
    statusBar()->showMessage(
        tr("Reloaded library: %1 parts across %2 path(s)")
            .arg(parts_.partCount()).arg(allPaths.size()), 4000);
}

}  // namespace bld::ui
