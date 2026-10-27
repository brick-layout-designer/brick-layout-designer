// Parts-library management: manage paths, rescan, load/save user-added paths,
// and the related reload-all menu action. Split out of MainWindow.cpp so
// the main window's ctor / dock wiring is readable.

#include "MainWindow.h"

#include "LibraryPathsDialog.h"
#include "MapView.h"
#include "PartUsagePanel.h"
#include "PartsBrowser.h"

#include "../import/PartsZip.h"
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
        tr("Reloaded parts: %1 parts in %2 folder(s)")
            .arg(parts_.partCount()).arg(newPaths.size()), 4000);
}

void MainWindow::rescanLibrary(const QStringList& paths) {
    parts_.clear();
    for (const QString& p : paths) parts_.addSearchPath(p);
    parts_.scan();
    // Constructor calls this before the docks exist.
    if (partUsagePanel_) partUsagePanel_->clearIconCache();
    // A set may have changed: its placed copies say so.
    if (mapView_) mapView_->refreshSetUpdates();
}

bool MainWindow::isCustomSetFile(const QString& xmlPath) const {
    const QString path = QDir::cleanPath(QFileInfo(xmlPath).absoluteFilePath());
    if (!path.endsWith(QLatin1String(".set.xml"), Qt::CaseInsensitive)) return false;
    // A server's custom parts: server-parts/<server>/custom/ (its libraries are libs/).
    const QString serverParts = QDir::cleanPath(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/server-parts"));
    if (path.startsWith(serverParts + QLatin1Char('/'), Qt::CaseInsensitive))
        return path.contains(QLatin1String("/custom/"), Qt::CaseInsensitive);
    for (const QString& root : ownPartsRoots())
        if (path.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive)) return true;
    return false;
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

    // A re-import that replaced an existing part: drop the stale entry
    // and cached sprite, and redraw copies already on the map.
    QString stem = QFileInfo(xmlAbsPath).completeBaseName();
    if (stem.endsWith(QLatin1String(".set"), Qt::CaseInsensitive)) stem.chop(4);  // a set's key
    const bool replaced = parts_.metadata(stem).has_value();
    if (replaced) parts_.forget(stem);

    const QString libKey = parts_.scanFile(xmlAbsPath);
    if (libKey.isEmpty()) return {};
    partsBrowser_->addOne(libKey);
    if (replaced) mapView_->rebuildScene();
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

bool MainWindow::addPartsFromZip(const QString& zipPath, QString* error) {
    const QString root = importedPartsRoot();
    if (root.isEmpty()) {
        if (error) *error = tr("There is no folder on this computer to keep your own parts in.");
        return false;
    }
    const import::PartsZipResult added = import::addPartsFromZip(zipPath, root);
    if (!added.ok) {
        if (error) *error = added.error;
        return false;
    }
    onReloadLibrary();
    // Show what came in: the set that places the whole design, else a part.
    QString first;
    for (const QString& key : added.partKeys) {
        const auto meta = parts_.metadata(key);
        if (!meta) continue;
        if (first.isEmpty()) first = key;
        if (meta->kind == parts::PartKind::Group) {
            first = key;
            break;
        }
    }
    if (!first.isEmpty()) showPartInBrowser(first);
    QString message = tr("Added %n part(s) from %1, in the Parts panel under “%2”.", nullptr,
                         static_cast<int>(added.partKeys.size()))
                          .arg(QFileInfo(zipPath).fileName(), added.folders.join(QStringLiteral("”, “")));
    // Its sets' pieces are only placed inside them (Show hidden parts lists them).
    if (!added.onlyInSets.isEmpty())
        message += QLatin1Char(' ')
                   + tr("%n part(s) only used inside its sets are hidden; tick Show hidden parts to see them.",
                        nullptr, static_cast<int>(added.onlyInSets.size()));
    statusBar()->showMessage(message, 10000);
    return true;
}

QStringList MainWindow::ownPartsRoots() const {
    const QString vendored = QDir::cleanPath(defaultVendoredPartsRoot());
    const QString serverParts = QDir::cleanPath(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/server-parts"));
    QStringList roots;
    if (const QString imports = importedPartsRoot(); !imports.isEmpty()) roots << QDir::cleanPath(imports);
    for (const QString& p : loadUserLibraryPaths()) {
        const QString path = QDir::cleanPath(p);
        if (path.isEmpty() || roots.contains(path) || path == vendored) continue;
        if (path == serverParts || path.startsWith(serverParts + QLatin1Char('/'))) continue;
        roots << path;
    }
    return roots;
}

QList<sync::LocalPart> MainWindow::scanOwnParts() const {
    QList<sync::LocalPart> parts;
    QSet<QString> seen;
    for (const QString& root : ownPartsRoots()) {
        if (!QDir(root).exists()) continue;
        // The first folder with a part number wins, as in the parts library.
        for (const sync::LocalPart& p : sync::PartsUpload::scanFolder(root))
            if (!seen.contains(p.key.toUpper())) {
                seen.insert(p.key.toUpper());
                parts << p;
            }
    }
    return parts;
}

QStringList MainWindow::allLibraryPaths() const {
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
    return allPaths;
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
    // The library's pictures were dropped: read the layout's again, with the loading card.
    mapView_->reloadPictures();
    statusBar()->showMessage(
        tr("Reloaded parts: %1 parts in %2 folder(s)")
            .arg(parts_.partCount()).arg(allPaths.size()), 4000);
}

}  // namespace bld::ui
