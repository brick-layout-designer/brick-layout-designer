// File I/O + autosave + recent files + export part list.
// Split out of MainWindow.cpp so the main window's core lifecycle and
// dock wiring stay readable. Every definition here is a member of the
// class declared in MainWindow.h — we're just spreading the out-of-line
// bodies across translation units.

#include "MainWindow.h"
#include "BudgetSession.h"

#include "../edit/PartList.h"
#include "../edit/VenueCommands.h"
#include "../import/LayoutFile.h"
#include "../import/mapformats/LDrawMap.h"
#include "../import/mapformats/FourDBrixMap.h"
#include "../import/mapformats/TrackDesignerMap.h"

#include "LayerPanel.h"
#include "MapView.h"
#ifdef BLD_SYNC
#include "LiveLayout.h"
#endif
#include "ModulesPanel.h"
#include "PartsBrowser.h"

#include "../core/ColorSpec.h"
#include "../core/Ids.h"
#include "../core/LayerBrick.h"
#include "../core/LayerGrid.h"
#include "../core/Map.h"
#include "../parts/PartsLibrary.h"
#include "../saveload/BbmReader.h"
#include "../saveload/BbmWriter.h"
#include "../saveload/SidecarIO.h"

#include <QAction>
#include <QByteArray>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QCheckBox>
#include <QFileDialog>
#include <QSaveFile>
#include <QFileInfo>
#include <QHash>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTime>
#include <QUndoStack>

#include <atomic>

namespace bld::ui {

namespace {
constexpr const char* kLastFileKey   = "recent/lastFile";
constexpr const char* kRecentListKey = "recent/list";
constexpr int         kRecentMax     = 12;

// 5-second throttle for the undo-stack-triggered autosave — near-realtime
// crash protection without thrashing the disk on bursts of edits.
std::atomic<qint64> gLastAutosaveMs{0};
constexpr qint64 kAutosaveThrottleMs = 5000;
}  // namespace

// ---------- Title -----------------------------------------------------------

void MainWindow::updateTitle() {
    QString name = currentFilePath_.isEmpty() ? tr("[untitled]") : QFileInfo(currentFilePath_).fileName();
    bool dirty = !mapView_->undoStack()->isClean();
#ifdef BLD_SYNC
    // Live: the server keeps it saved.
    if (live_ && live_->active()) {
        name = tr("%1 — Live").arg(live_->title());
        dirty = false;
    }
#endif
    // As BlueBrick, the open budget follows the map name.
    QString budget;
    if (budget_ && budget_->exists())
        budget = QStringLiteral(" — %1%2").arg(budget_->displayName(),
                                                  budget_->isModified() ? QStringLiteral(" *") : QString());
    setWindowTitle(tr("%1%2%3 — Brick Layout Designer")
                       .arg(name, dirty ? QStringLiteral(" *") : QString(), budget));
}

// ---------- Open / Save / New ----------------------------------------------

namespace {

// Map formats besides .bbm that BlueBrick also opens and saves.
bool isLDrawMap(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("ldr") || suffix == QLatin1String("mpd");
}
bool isTrackDesignerMap(const QString& path) {
    return QFileInfo(path).suffix().compare(QLatin1String("tdl"), Qt::CaseInsensitive) == 0;
}
bool isFourDBrixMap(const QString& path) {
    return QFileInfo(path).suffix().compare(QLatin1String("ncp"), Qt::CaseInsensitive) == 0;
}
bool isOtherMapFormat(const QString& path) {
    return isLDrawMap(path) || isTrackDesignerMap(path) || isFourDBrixMap(path);
}

const char* kOpenFilter = QT_TRANSLATE_NOOP("bld::ui::MainWindow",
    "All supported maps (*.bld-layout *.bbm *.ldr *.mpd *.tdl *.ncp);;Brick Layout Designer layout (*.bld-layout);;"
    "BlueBrick map (*.bbm);;"
    "LDraw (*.ldr);;LDraw multi-part (*.mpd);;TrackDesigner (*.tdl);;4DBrix nControl (*.ncp);;All files (*)");
const char* kSaveFilter = QT_TRANSLATE_NOOP("bld::ui::MainWindow",
    "Brick Layout Designer layout (*.bld-layout);;BlueBrick map (*.bbm);;LDraw (*.ldr);;LDraw multi-part (*.mpd);;TrackDesigner (*.tdl);;"
    "4DBrix nControl (*.ncp)");

// Where background images inside .bld-layout files are unpacked to.
QString layoutAssetDir() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/layout-assets");
}

}  // namespace

bool MainWindow::openFile(const QString& path) {
    if (!maybeSave()) return false;
#ifdef BLD_SYNC
    // Another layout replaces the live one: leave the server session.
    if (live_ && live_->active()) {
        live_->close();
        updateLiveUi();
    }
#endif
    if (isOtherMapFormat(path)) {
        auto ldraw = isTrackDesignerMap(path) ? import::readTrackDesignerMap(path, parts_)
                   : isFourDBrixMap(path)     ? import::readFourDBrixMap(path, parts_)
                                              : import::readLDrawMap(path, parts_);
        if (!ldraw.ok()) {
            QMessageBox::warning(this, tr("Open failed"), tr("%1\n\n%2").arg(path, ldraw.error));
            return false;
        }
        const int layerCount = static_cast<int>(ldraw.map->layers().size());
        mapView_->loadMap(std::move(ldraw.map));
        layerPanel_->setMap(mapView_->currentMap(), mapView_->builder());
        modulesPanel_->setMap(mapView_->currentMap());
        currentFilePath_ = path;
        mapView_->undoStack()->setClean();
        cleanUndoIndex_ = 0;
        updateTitle();
        statusBar()->showMessage(ldraw.warnings.isEmpty()
            ? tr("Opened %1 — %2 layers").arg(path).arg(layerCount)
            : tr("Opened %1 — %2 layers (%3)").arg(path).arg(layerCount).arg(ldraw.warnings.join(QStringLiteral("; "))));
        QSettings().setValue(QString::fromLatin1(kLastFileKey), path);
        pushRecentFile(path);
        return true;
    }
    if (import::isLayoutFile(path)) {
        auto layout = import::readLayoutFile(path, layoutAssetDir());
        if (!layout.ok()) {
            QMessageBox::warning(this, tr("Open failed"), tr("%1\n\n%2").arg(path, layout.error));
            return false;
        }
        // Before the map, so its bricks find their parts.
        const QStringList partNotes = takeInLayoutParts(layout.partFiles);
        showLoadedMap(std::move(layout.map), path, layout.warnings + partNotes);
        return true;
    }
    auto result = saveload::readBbm(path);
    if (!result.ok()) {
        QMessageBox::warning(this, tr("Open failed"),
            tr("%1\n\n%2").arg(path, result.error));
        return false;
    }
    if (!result.error.isEmpty()) {
        statusBar()->showMessage(tr("Loaded with warnings: %1").arg(result.error), 5000);
    }

    // Sidecar: optional. Hash-check the .bbm bytes to flag drift (user edited
    // the .bbm in vanilla BlueBrick between our writes).
    const QString sidecarPath = saveload::sidecarPathFor(path);
    if (QFile::exists(sidecarPath)) {
        QFile bf(path);
        QByteArray bbmBytes;
        if (bf.open(QIODevice::ReadOnly)) bbmBytes = bf.readAll();
        auto sres = saveload::readSidecar(sidecarPath, bbmBytes, result.map->sidecar);
        if (sres.ok && sres.hashMismatch) {
            statusBar()->showMessage(
                tr("Sidecar hash mismatch — .bbm was modified externally. Fork-only metadata preserved but may drift."), 8000);
        } else if (!sres.ok) {
            statusBar()->showMessage(tr("Sidecar load failed: %1").arg(sres.error), 5000);
        }
    }

    showLoadedMap(std::move(result.map), path, {});
    return true;
}

void MainWindow::showLoadedMap(std::unique_ptr<core::Map> map, const QString& path, const QStringList& warnings) {
    const int layerCount = static_cast<int>(map->layers().size());
    const int nbItems = map->nbItems;
    mapView_->loadMap(std::move(map));
    layerPanel_->setMap(mapView_->currentMap(), mapView_->builder());
    modulesPanel_->setMap(mapView_->currentMap());
    currentFilePath_ = path;
    saveFormatChosen_ = false;
    mapView_->undoStack()->setClean();
    cleanUndoIndex_ = 0;
    updateTitle();
    const QString opened = tr("Opened %1 — %2 layers, %3 items").arg(path).arg(layerCount).arg(nbItems);
    statusBar()->showMessage(warnings.isEmpty()
                                 ? opened
                                 : QStringLiteral("%1 (%2)").arg(opened, warnings.join(QStringLiteral("; "))));
    QSettings().setValue(QString::fromLatin1(kLastFileKey), path);
    pushRecentFile(path);
}

void MainWindow::onOpen() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open map"), {}, tr(kOpenFilter));
    if (!path.isEmpty()) openFile(path);
}

bool MainWindow::writeMapTo(const QString& path) {
    auto* map = mapView_->currentMap();
    if (!map) return false;
    if (isOtherMapFormat(path)) {
        QString err;
        const bool written = isTrackDesignerMap(path) ? import::writeTrackDesignerMap(*map, path, parts_, &err)
                           : isFourDBrixMap(path)     ? import::writeFourDBrixMap(*map, path, parts_, &err)
                                                      : import::writeLDrawMap(*map, path, parts_, &err);
        if (!written) {
            QMessageBox::warning(this, tr("Save failed"), err);
            return false;
        }
        currentFilePath_ = path;
        mapView_->undoStack()->setClean();
        updateTitle();
        statusBar()->showMessage(tr("Saved %1").arg(path), 3000);
        QSettings().setValue(QString::fromLatin1(kLastFileKey), path);
        pushRecentFile(path);
        QFile::remove(autosavePath());
        return true;
    }
    if (import::isLayoutFile(path)) {
        QString err;
        QStringList warnings;
        if (!import::writeLayoutFile(*map, path, &err, &warnings, partsToEmbed())) {
            QMessageBox::warning(this, tr("Save failed"), err);
            return false;
        }
        if (!warnings.isEmpty())
            QMessageBox::warning(this, tr("Saved with a problem"), warnings.join(QStringLiteral("\n")));
        currentFilePath_ = path;
        mapView_->undoStack()->setClean();
        updateTitle();
        statusBar()->showMessage(tr("Saved %1").arg(path), 3000);
        QSettings().setValue(QString::fromLatin1(kLastFileKey), path);
        pushRecentFile(path);
        QFile::remove(autosavePath());
        return true;
    }
    auto res = saveload::writeBbm(*map, path);
    if (!res.ok) {
        QMessageBox::warning(this, tr("Save failed"), res.error);
        return false;
    }

    // Sidecar: always write when the map carries fork-only metadata; clean up
    // any stale sidecar if the map no longer has any.
    const QString sidecarPath = saveload::sidecarPathFor(path);
    if (!map->sidecar.isEmpty()) {
        QFile bf(path);
        if (bf.open(QIODevice::ReadOnly)) {
            const QByteArray bbmBytes = bf.readAll();
            QString err;
            if (!saveload::writeSidecar(sidecarPath, bbmBytes, map->sidecar, &err)) {
                QMessageBox::warning(this, tr("Sidecar save failed"),
                    tr("The .bbm saved successfully but the sidecar failed:\n%1").arg(err));
            }
        }
    } else if (QFile::exists(sidecarPath)) {
        QFile::remove(sidecarPath);
    }

    currentFilePath_ = path;
    mapView_->undoStack()->setClean();
    updateTitle();
    statusBar()->showMessage(tr("Saved %1").arg(path), 3000);
    QSettings().setValue(QString::fromLatin1(kLastFileKey), path);
    pushRecentFile(path);
    // Manual save supersedes any autosave for the session.
    QFile::remove(autosavePath());
    return true;
}

QMap<QString, QByteArray> MainWindow::partsToEmbed() const {
    const auto* map = mapView_->currentMap();
    return map ? import::layoutPartFiles(*map, parts_, defaultVendoredPartsRoot()) : QMap<QString, QByteArray>();
}

QStringList MainWindow::takeInLayoutParts(const QMap<QString, QByteArray>& files) {
    if (files.isEmpty()) return {};
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/layout-parts");
    const auto installed = import::installLayoutParts(files, dir, parts_);
    if (!installed.newParts.isEmpty()) {
        // The folder joins the library paths once, so the parts stay after a restart.
        QStringList paths = loadUserLibraryPaths();
        if (!paths.contains(dir)) {
            paths << dir;
            saveUserLibraryPaths(paths);
        }
        for (const auto& xml : installed.newParts) registerImportedPart(xml);
    }
    QStringList notes;
    if (!installed.newParts.isEmpty())
        notes << tr("%n part(s) from the layout added to your library", nullptr,
                    static_cast<int>(installed.newParts.size()));
    if (!installed.differing.isEmpty())
        notes << tr("your own %1 kept, which differ from the layout's").arg(installed.differing.join(QStringLiteral(", ")));
    if (!installed.failed.isEmpty())
        notes << tr("could not save %1").arg(installed.failed.join(QStringLiteral(", ")));
    return notes;
}

bool MainWindow::onSave() {
    if (!mapView_->currentMap()) return false;
    if (currentFilePath_.isEmpty()) return onSaveAs();
    if (!import::isLayoutFile(currentFilePath_) && !saveFormatChosen_) return chooseSaveFormat();
    return writeMapTo(currentFilePath_);
}

bool MainWindow::chooseSaveFormat() {
    const QFileInfo info(currentFilePath_);
    const QString native = info.dir().filePath(info.completeBaseName() + QStringLiteral(".bld-layout"));
    QMessageBox box(QMessageBox::Question, tr("Save layout"),
        tr("%1 is a %2 file.\n\n"
           "Save it as a Brick Layout Designer layout (%3) to keep everything in one file: "
           "labels, modules, the venue and the background image included.\n\n"
           "Keep it as %2 if you still open it in another program; File › Export › BlueBrick Map "
           "can make a .bbm copy at any time.")
            .arg(info.fileName(), info.suffix().toLower().prepend(QLatin1Char('.')), QFileInfo(native).fileName()),
        QMessageBox::Cancel, this);
    QPushButton* switchBtn = box.addButton(tr("Save as .bld-layout"), QMessageBox::AcceptRole);
    QPushButton* keepBtn = box.addButton(tr("Keep %1").arg(info.suffix().toLower().prepend(QLatin1Char('.'))),
                                  QMessageBox::DestructiveRole);
    box.setDefaultButton(switchBtn);
    box.exec();
    if (box.clickedButton() == keepBtn) {
        saveFormatChosen_ = true;
        return writeMapTo(currentFilePath_);
    }
    if (box.clickedButton() != switchBtn) return false;
    if (QFile::exists(native)
        && QMessageBox::question(this, tr("Save layout"), tr("%1 already exists. Replace it?").arg(native))
               != QMessageBox::Yes)
        return false;
    return writeMapTo(native);
}

void MainWindow::onExportBbm() {
    auto* map = mapView_->currentMap();
    if (!map) return;
    const QFileInfo info(currentFilePath_);
    const QString suggested = currentFilePath_.isEmpty()
        ? QStringLiteral("layout.bbm")
        : info.dir().filePath(info.completeBaseName() + QStringLiteral(".bbm"));
    QString path = QFileDialog::getSaveFileName(this, tr("Export BlueBrick map"), suggested,
                                                tr("BlueBrick map (*.bbm)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".bbm"), Qt::CaseInsensitive)) path += QStringLiteral(".bbm");
    // BlueBrick has no labels, modules, venues or background images.
    QStringList lost;
    if (!map->sidecar.anchoredLabels.empty()) lost << tr("anchored labels");
    if (!map->sidecar.modules.empty()) lost << tr("modules");
    if (map->sidecar.venue) lost << tr("the venue");
    if (!map->sidecar.backgroundImagePath.isEmpty()) lost << tr("the background image");
    if (!lost.isEmpty()
        && QMessageBox::question(this, tr("Export BlueBrick map"),
               tr("BlueBrick can't hold %1, so the .bbm leaves them out. "
                  "Your layout keeps them.\n\nExport anyway?").arg(lost.join(QStringLiteral(", "))))
               != QMessageBox::Yes)
        return;
    const auto res = saveload::writeBbm(*map, path);
    if (!res.ok) {
        QMessageBox::warning(this, tr("Export failed"), res.error);
        return;
    }
    statusBar()->showMessage(tr("Exported %1").arg(path), 4000);
}

bool MainWindow::onSaveAs() {
    if (!mapView_->currentMap()) return false;
    QString selectedFilter;
    // Offer the native format first, even for a file opened as something else.
    const QFileInfo current(currentFilePath_);
    const QString suggested = currentFilePath_.isEmpty()
        ? QString()
        : current.dir().filePath(current.completeBaseName() + QStringLiteral(".bld-layout"));
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save map"), suggested, tr(kSaveFilter), &selectedFilter);
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().isEmpty()) {
        path += selectedFilter.contains(QStringLiteral("*.mpd")) ? QStringLiteral(".mpd")
              : selectedFilter.contains(QStringLiteral("*.ldr")) ? QStringLiteral(".ldr")
              : selectedFilter.contains(QStringLiteral("*.tdl")) ? QStringLiteral(".tdl")
              : selectedFilter.contains(QStringLiteral("*.ncp")) ? QStringLiteral(".ncp")
              : selectedFilter.contains(QStringLiteral("*.bbm")) ? QStringLiteral(".bbm")
                                                                  : QStringLiteral(".bld-layout");
    }
    // As in BlueBrick: other formats can't hold everything a .bbm does.
    const QString warnKey = QStringLiteral("general/warnNonBbmSave");
    if (!path.endsWith(QStringLiteral(".bbm"), Qt::CaseInsensitive) && !import::isLayoutFile(path)
        && QSettings().value(warnKey, true).toBool()) {
        QMessageBox box(QMessageBox::Question, tr("Save as %1").arg(QFileInfo(path).suffix().toUpper()),
            tr("This format can't store everything in the map: text, area and grid layers, "
               "module / label / venue data and parts the format has no equivalent for are lost, "
               "and layer names may change. Keep a .bbm copy if you need them.\n\nSave anyway?"),
            QMessageBox::Yes | QMessageBox::No, this);
        auto* dontAsk = new QCheckBox(tr("Don't show this again"), &box);
        box.setCheckBox(dontAsk);
        const int answer = box.exec();
        if (dontAsk->isChecked()) QSettings().setValue(warnKey, false);
        if (answer != QMessageBox::Yes) return false;
    }
    saveFormatChosen_ = true;  // picked here
    return writeMapTo(path);
}

bool MainWindow::maybeSave() {
    if (!mapView_->currentMap() || mapView_->undoStack()->isClean()) return true;
    const auto btn = QMessageBox::question(
        this, tr("Unsaved changes"),
        tr("The current layout has unsaved changes. Save before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (btn == QMessageBox::Save)    return onSave();
    if (btn == QMessageBox::Discard) return true;
    return false;
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!maybeSave() || !maybeSaveBudget()) { e->ignore(); return; }
    // Persist window geometry + dock layout for the next launch.
    QSettings s;
    s.beginGroup(QStringLiteral("ui"));
    s.setValue(QStringLiteral("geometry"), saveGeometry());
    s.setValue(QStringLiteral("state"),    saveState());
    s.endGroup();
    e->accept();
}

void MainWindow::ensureDocument() {
    if (mapView_->currentMap()) return;
    onNew();
}

void MainWindow::onNew() {
    newDocument();
}

bool MainWindow::newDocument() {
    if (!maybeSave()) return false;
#ifdef BLD_SYNC
    // Another layout replaces the live one: leave the server session.
    if (live_ && live_->active()) {
        live_->close();
        updateLiveUi();
    }
#endif

    // If the user configured a "new map template" file in Preferences
    // (general/newMapTemplate), load that as the starting point — vanilla
    // BlueBrick's StartNewFileUsingDefaultTemplate parity.
    const QString templatePath =
        QSettings().value(QStringLiteral("general/newMapTemplate")).toString();
    if (!templatePath.isEmpty() && QFile::exists(templatePath)) {
        auto res = saveload::readBbm(templatePath);
        if (res.ok() && res.map) {
            mapView_->loadMap(std::move(res.map));
            layerPanel_->setMap(mapView_->currentMap(), mapView_->builder());
            modulesPanel_->setMap(mapView_->currentMap());
            currentFilePath_.clear();
            mapView_->undoStack()->clear();
            mapView_->undoStack()->setClean();
            updateTitle();
            statusBar()->showMessage(tr("New layout from template %1").arg(templatePath), 3000);
            return true;
        }
    }

    auto blank = std::make_unique<core::Map>();
    // BlueBrick's StartNewFile defaults: cornflower-blue background + a
    // Grid layer at the bottom + a Bricks layer on top. Grid first so
    // it renders behind the bricks; bricks are the active layer so
    // drop-onto-map works immediately. Use fromKnown so the saved .bbm
    // preserves the "CornflowerBlue" name vanilla recognises.
    blank->backgroundColor = core::ColorSpec::fromKnown(
        QColor(100, 149, 237), QStringLiteral("CornflowerBlue"));
    auto grid = std::make_unique<core::LayerGrid>();
    grid->guid = core::newBbmId();
    grid->name = tr("Grid");
    blank->layers().push_back(std::move(grid));
    auto layer = std::make_unique<core::LayerBrick>();
    layer->guid = core::newBbmId();
    layer->name = tr("Bricks");
    blank->layers().push_back(std::move(layer));
    blank->selectedLayerIndex = static_cast<int>(blank->layers().size()) - 1;
    blank->nbItems = 0;
    mapView_->loadMap(std::move(blank));
    layerPanel_->setMap(mapView_->currentMap(), mapView_->builder());
    modulesPanel_->setMap(mapView_->currentMap());
    currentFilePath_.clear();
    mapView_->undoStack()->clear();
    mapView_->undoStack()->setClean();
    updateTitle();
    statusBar()->showMessage(tr("New layout"), 3000);
    return true;
}

void MainWindow::startLayoutFromVenue(const core::Venue& venue) {
    if (!newDocument()) return;
    auto* m = mapView_->currentMap();
    if (!m) return;
    core::Venue v = venue;
    v.enabled = true;
    // The venue is part of the new layout's starting point, like the
    // template, so it isn't an undo step and the layout starts clean.
    mapView_->undoStack()->push(new edit::SetVenueCommand(*m, std::make_optional(v)));
    mapView_->undoStack()->clear();
    mapView_->undoStack()->setClean();
    statusBar()->showMessage(tr("New layout from venue \"%1\"").arg(v.name), 3000);
}

// ---------- Part-list export / About --------------------------------------

void MainWindow::onExportPartList() {
    auto* map = mapView_->currentMap();
    if (!map) return;
    QSettings settings;
    edit::PartListOptions options;
    options.splitPerLayer = settings.value(QStringLiteral("partList/splitPerLayer"), false).toBool();
    options.includeHiddenLayers = settings.value(QStringLiteral("partList/includeHiddenLayers"), true).toBool();
    options.budget = budget_->budget();
    options.defaultBudgetIsInfinite = BudgetSession::defaultBudgetIsInfinite();
    const auto groups = edit::buildPartList(*map, parts_, options);
    if (groups.empty() || (groups.size() == 1 && groups.front().rows.empty())) {
        QMessageBox::information(this, tr("Export part list"),
            tr("The current layout contains no bricks."));
        return;
    }
    // As BlueBrick: .txt and .csv by extension, HTML otherwise.
    QString selectedFilter;
    const QString base = currentFilePath_.isEmpty() ? QStringLiteral("parts") : QFileInfo(currentFilePath_).completeBaseName();
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export part list"), base + QStringLiteral(".html"),
        tr("HTML (*.html *.htm);;Text (*.txt);;CSV (*.csv)"), &selectedFilter);
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) {
        path += selectedFilter.contains(QStringLiteral("*.txt")) ? QStringLiteral(".txt")
              : selectedFilter.contains(QStringLiteral("*.csv")) ? QStringLiteral(".csv")
                                                                  : QStringLiteral(".html");
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    const QString mapName = currentFilePath_.isEmpty() ? tr("Untitled.bbm") : QFileInfo(currentFilePath_).fileName();
    const QString title = tr("Part List for file \"%1\"").arg(mapName);
    const bool hasBudget = options.budget != nullptr;
    QString text;
    if (suffix == QLatin1String("txt")) {
        text = edit::partListText(*map, groups, title, hasBudget);
    } else if (suffix == QLatin1String("csv")) {
        text = edit::partListCsv(groups, hasBudget);
    } else {
        text = edit::partListHtml(*map, groups, title, hasBudget, [this](const QString& part) {
            const QPixmap pm = parts_.pixmap(part);
            if (pm.isNull()) return QImage();
            return pm.toImage().scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        });
    }
    QSaveFile f(path);
    const QByteArray bytes = text.toUtf8();
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        QMessageBox::warning(this, tr("Export failed"),
            tr("Cannot write %1: %2").arg(path, f.errorString()));
        return;
    }
    int kinds = 0, total = 0;
    for (const auto& g : groups) { kinds += static_cast<int>(g.rows.size()); total += g.total.count; }
    statusBar()->showMessage(tr("Exported %1 part rows, %2 bricks, to %3").arg(kinds).arg(total).arg(path), 5000);
}

void MainWindow::onAbout() {
    QMessageBox::about(this, tr("About Brick Layout Designer"),
        tr("<h3>Brick Layout Designer</h3>"
           "<p>Cross-platform C++/Qt 6 fork of <b>BlueBrick</b> by Alban Nanty and contributors.</p>"
           "<p>Adds cross-layer modules, anchored text labels, and event venues on top of the "
           "vanilla <i>.bbm</i> format (extra metadata stored in a sidecar <i>.bbm.bld</i> so "
           "vanilla BlueBrick 1.9.2 keeps opening our files).</p>"
           "<p>Licensed under GPL-3.0 — same as upstream BlueBrick.</p>"
           "<p>Parts library: %1 parts indexed.</p>")
        .arg(parts_.partCount()));
}

// ---------- Recent files ---------------------------------------------------

void MainWindow::rebuildRecentMenu() {
    if (!recentMenu_) return;
    recentMenu_->clear();
    const QStringList list = QSettings().value(kRecentListKey).toStringList();
    for (const QString& p : list) {
        QAction* act = recentMenu_->addAction(QFileInfo(p).fileName());
        act->setToolTip(p);
        connect(act, &QAction::triggered, this, [this, p]{ openFile(p); });
    }
    if (list.isEmpty()) {
        auto* empty = recentMenu_->addAction(tr("(no recent files)"));
        empty->setEnabled(false);
    } else {
        recentMenu_->addSeparator();
        auto* clear = recentMenu_->addAction(tr("Clear Menu"));
        connect(clear, &QAction::triggered, this, [this]{
            QSettings().remove(QString::fromLatin1(kRecentListKey));
            rebuildRecentMenu();
        });
    }
}

void MainWindow::pushRecentFile(const QString& path) {
    QSettings s;
    QStringList list = s.value(kRecentListKey).toStringList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > kRecentMax) list.removeLast();
    s.setValue(kRecentListKey, list);
    rebuildRecentMenu();
}

// ---------- Autosave + restore ---------------------------------------------

QString MainWindow::autosavePath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/autosave.bld-layout");
}

void MainWindow::performAutosave() {
    if (!mapView_->currentMap() || mapView_->undoStack()->isClean()) return;
    const QString path = autosavePath();
    QString error;
    if (import::writeLayoutFile(*mapView_->currentMap(), path, &error, nullptr, partsToEmbed())) {
        // Record the original file alongside so the startup prompt can
        // mention the source filename.
        QSettings().setValue(QStringLiteral("autosave/sourceFile"), currentFilePath_);
        statusBar()->showMessage(tr("Autosaved (%1)").arg(QTime::currentTime().toString("HH:mm:ss")), 2000);
    }
}

// Called on every undo-stack mutation. Rate-limits so rapid edits don't
// thrash the disk; the cap means at most ~5 s of work is at risk on a crash.
void MainWindow::performAutosaveThrottled() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 last = gLastAutosaveMs.load(std::memory_order_relaxed);
    if (now - last < kAutosaveThrottleMs) return;
    gLastAutosaveMs.store(now, std::memory_order_relaxed);
    performAutosave();
}

bool MainWindow::restoreAutosaveIfAny(const QString& lastFile) {
    const QString path = autosavePath();
    if (!QFile::exists(path)) return false;
    const QFileInfo ai(path);
    if (!lastFile.isEmpty()) {
        const QFileInfo li(lastFile);
        if (li.exists() && li.lastModified() >= ai.lastModified()) return false;
    }
    const QString source = QSettings().value(QStringLiteral("autosave/sourceFile")).toString();
    const auto btn = QMessageBox::question(
        this, tr("Restore autosave?"),
        tr("An unsaved layout from the last session was recovered:\n%1\n\n"
           "Original file: %2\n"
           "Restore it?")
            .arg(path, source.isEmpty() ? tr("(new file)") : source),
        QMessageBox::Yes | QMessageBox::No);
    if (btn != QMessageBox::Yes) return false;

    // Load the autosave content directly into the map. Bypassing
    // openFile(path) means openFile doesn't set currentFilePath_ to the
    // autosave file itself — we want the title / Ctrl+S target / Recent
    // Files entry to all reflect the ORIGINAL file, but with the
    // autosave's unsaved content on top.
    // The autosave is a whole layout file: labels, modules and venue included.
    auto result = import::readLayoutFile(path, layoutAssetDir());
    if (!result.ok()) {
        QMessageBox::warning(this, tr("Restore failed"),
            tr("Couldn't read the autosave: %1").arg(result.error));
        return false;
    }

    mapView_->loadMap(std::move(result.map));
    layerPanel_->setMap(mapView_->currentMap(), mapView_->builder());
    modulesPanel_->setMap(mapView_->currentMap());

    // Point at the original file so Ctrl+S overwrites the right place. We
    // leave the undo stack NOT clean so the title shows the dirty
    // asterisk — the user must know the recovered content still needs
    // a save to persist to the original file.
    currentFilePath_ = source;
    saveFormatChosen_ = false;
    if (!source.isEmpty()) pushRecentFile(source);
    updateTitle();

    statusBar()->showMessage(source.isEmpty()
        ? tr("Restored unsaved layout from autosave")
        : tr("Restored unsaved changes on top of %1").arg(source),
        5000);
    return true;
}

}  // namespace bld::ui
