#include "MainWindow.h"

#include "LayerPanel.h"
#include "BudgetSession.h"
#include "UpdateCheck.h"
#include "FindDialog.h"
#include "LibraryPathsDialog.h"
#include "PreferencesDialog.h"
#include "VenueDialog.h"
#include "VenueDimensionsDialog.h"
#include "../edit/VenueCommands.h"
#include "../saveload/VenueIO.h"
#include "MapView.h"
#include "ModuleLibraryPanel.h"
#include "ModulesPanel.h"
#include "VenueLibraryPanel.h"
#include "PartsBrowser.h"
#include "PartUsagePanel.h"
#ifdef BLD_SYNC
#include "LiveLayout.h"
#endif

#include "../core/Map.h"
#include "../parts/BrickPlacement.h"
#include "../parts/PartsLibrary.h"
#include "../core/Brick.h"
#include "../core/Ids.h"
#include "../core/Layer.h"
#include "../core/LayerArea.h"
#include "../core/LayerBrick.h"
#include "../core/LayerGrid.h"
#include "../core/AnchoredLabel.h"
#include "../core/ColorSpec.h"
#include "../edit/EditCommands.h"
#include "../edit/LabelCommands.h"
#include "../edit/LayerCommands.h"
#include "../edit/ModuleCommands.h"
#include "../edit/Budget.h"
#include "../edit/VenueValidator.h"

#include <atomic>
#include <functional>
#include "../saveload/BbmReader.h"
#include "../saveload/BbmWriter.h"
#include "../saveload/SetIO.h"
#include "../saveload/SidecarIO.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QHash>
#include <QImage>
#include <QInputDialog>
#include <QStyle>
#include <QPainter>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QCheckBox>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTime>
#include <QGuiApplication>
#include <QTimer>
#include <QHBoxLayout>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QUndoStack>
#include <QUuid>

namespace bld::ui {

namespace {
constexpr const char* kLastFileKey = "recent/lastFile";
}

MainWindow::MainWindow(parts::PartsLibrary& parts, QWidget* parent)
    : QMainWindow(parent), parts_(parts) {
    resize(1400, 900);

    // Scan every configured library path on startup so the PartsBrowser below
    // reflects the user's full set, not just the vendored submodule.
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

    mapView_ = new MapView(parts_, this);
    updates_ = new UpdateCheck(this);
    // Once the window is up; not in headless runs (tests, CI smoke launches).
    if (QGuiApplication::platformName() != QLatin1String("offscreen") && !qEnvironmentVariableIsSet("BLD_NO_UPDATE_CHECK"))
        QTimer::singleShot(3000, updates_, &UpdateCheck::checkAtStartupIfDue);
    budget_ = new BudgetSession(parts_, this);
    mapView_->setBudget(budget_);
    // Reopen last session's budget once everything listening is connected.
    QTimer::singleShot(0, budget_, &BudgetSession::restoreFromSettings);
    setCentralWidget(mapView_);

    layerPanel_ = new LayerPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, layerPanel_);
    connect(layerPanel_, &LayerPanel::addLayerRequested, this, [this](core::LayerKind k){
        if (!mapView_->currentMap()) return;
        mapView_->undoStack()->push(new edit::AddLayerCommand(*mapView_->currentMap(), k));
        layerPanel_->setMap(mapView_->currentMap(), mapView_->builder());
    });
    connect(layerPanel_, &LayerPanel::deleteLayerRequested, this, [this](int idx){
        if (!mapView_->currentMap()) return;
        mapView_->undoStack()->push(new edit::DeleteLayerCommand(*mapView_->currentMap(), idx));
        layerPanel_->setMap(mapView_->currentMap(), mapView_->builder());
    });
    connect(layerPanel_, &LayerPanel::moveLayerRequested, this, [this](int idx, int delta){
        if (!mapView_->currentMap()) return;
        mapView_->undoStack()->push(new edit::MoveLayerCommand(*mapView_->currentMap(), idx, delta));
        layerPanel_->setMap(mapView_->currentMap(), mapView_->builder());
    });
    connect(layerPanel_, &LayerPanel::renameLayerRequested, this, [this](int idx, const QString& name){
        if (!mapView_->currentMap()) return;
        mapView_->undoStack()->push(new edit::RenameLayerCommand(*mapView_->currentMap(), idx, name));
        layerPanel_->setMap(mapView_->currentMap(), mapView_->builder());
    });
    connect(layerPanel_, &LayerPanel::activeLayerChanged, this, [this](int){
        // Active-layer change is a soft UI state — no undo entry, no scene
        // rebuild. We don't need to do anything else here besides what
        // LayerPanel already did (set map_->selectedLayerIndex).
    });
    connect(layerPanel_, &LayerPanel::layerOptionsRequested, this, [this](int idx){
        auto* map = mapView_->currentMap();
        if (!map || idx < 0 || idx >= static_cast<int>(map->layers().size())) return;
        auto& layer = *map->layers()[idx];
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Layer options"));
        auto* form = new QFormLayout(&dlg);
        auto* nameE = new QLineEdit(layer.name, &dlg);
        form->addRow(tr("Name:"), nameE);
        auto* alphaSpin = new QSpinBox(&dlg);
        alphaSpin->setRange(0, 100); alphaSpin->setSuffix(QStringLiteral(" %"));
        alphaSpin->setValue(layer.transparency);
        form->addRow(tr("Transparency:"), alphaSpin);
        auto* visChk = new QCheckBox(tr("Visible"), &dlg);
        visChk->setChecked(layer.visible);
        form->addRow(visChk);

        // Hull section (vanilla's DisplayHulls on brick/text/ruler layers).
        auto* hullChk = new QCheckBox(tr("Display selection hulls"), &dlg);
        hullChk->setChecked(layer.hull.displayHulls);
        form->addRow(hullChk);
        auto* hullThick = new QSpinBox(&dlg);
        hullThick->setRange(1, 20);
        hullThick->setValue(layer.hull.thickness);
        form->addRow(tr("Hull thickness:"), hullThick);

        // Grid-layer specific section — mirrors LayerGridOptionForm.cs.
        QSpinBox* gridSize = nullptr;
        QSpinBox* gridThick = nullptr;
        QSpinBox* subDiv = nullptr;
        QCheckBox* showGrid = nullptr;
        QCheckBox* showSub = nullptr;
        QCheckBox* showCellIdx = nullptr;
        if (layer.kind() == core::LayerKind::Grid) {
            auto& G = static_cast<core::LayerGrid&>(layer);
            form->addRow(new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Grid layer")), &dlg));
            gridSize = new QSpinBox(&dlg);
            gridSize->setRange(1, 512); gridSize->setSuffix(tr(" studs"));
            gridSize->setValue(G.gridSizeInStud);
            form->addRow(tr("Cell size:"), gridSize);
            gridThick = new QSpinBox(&dlg);
            gridThick->setRange(1, 20);
            gridThick->setValue(static_cast<int>(G.gridThickness));
            form->addRow(tr("Grid line thickness:"), gridThick);
            subDiv = new QSpinBox(&dlg);
            subDiv->setRange(2, 32);
            subDiv->setValue(G.subDivisionNumber);
            form->addRow(tr("Sub-divisions per cell:"), subDiv);
            showGrid = new QCheckBox(tr("Display grid"), &dlg);
            showGrid->setChecked(G.displayGrid);
            form->addRow(showGrid);
            showSub = new QCheckBox(tr("Display sub-grid"), &dlg);
            showSub->setChecked(G.displaySubGrid);
            form->addRow(showSub);
            showCellIdx = new QCheckBox(tr("Display cell index labels"), &dlg);
            showCellIdx->setChecked(G.displayCellIndex);
            form->addRow(showCellIdx);
        }

        // Brick-layer specific section — mirrors LayerBrickOptionForm.cs
        // (DisplayBrickElevation + a stud-spacing grid override). Since
        // we don't have per-brick-layer snap overrides yet, this just
        // covers the one bool.
        QCheckBox* brickElev = nullptr;
        if (layer.kind() == core::LayerKind::Brick) {
            auto& B = static_cast<core::LayerBrick&>(layer);
            form->addRow(new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Brick layer")), &dlg));
            brickElev = new QCheckBox(tr("Display brick elevation labels"), &dlg);
            brickElev->setChecked(B.displayBrickElevation);
            form->addRow(brickElev);
        }

        // Area-layer specific section — cell size (in studs).
        QSpinBox* areaCell = nullptr;
        if (layer.kind() == core::LayerKind::Area) {
            auto& A = static_cast<core::LayerArea&>(layer);
            form->addRow(new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Area layer")), &dlg));
            areaCell = new QSpinBox(&dlg);
            areaCell->setRange(1, 256); areaCell->setSuffix(tr(" studs"));
            areaCell->setValue(A.areaCellSizeInStud);
            form->addRow(tr("Paint cell size:"), areaCell);
            form->addRow(new QLabel(
                tr("Changing cell size on a layer with painted cells will leave\n"
                   "existing cells at their old indexing — paint over to clean up."), &dlg));
        }

        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;
        mapView_->undoStack()->beginMacro(tr("Layer options"));
        if (nameE->text() != layer.name) {
            mapView_->undoStack()->push(new edit::RenameLayerCommand(*map, idx, nameE->text()));
        }
        if (alphaSpin->value() != layer.transparency) {
            mapView_->undoStack()->push(new edit::SetLayerTransparencyCommand(*map, idx, alphaSpin->value()));
        }
        mapView_->undoStack()->endMacro();
        layer.visible = visChk->isChecked();
        layer.hull.displayHulls = hullChk->isChecked();
        layer.hull.thickness = hullThick->value();
        if (layer.kind() == core::LayerKind::Grid) {
            auto& G = static_cast<core::LayerGrid&>(layer);
            G.gridSizeInStud = gridSize->value();
            G.gridThickness = static_cast<float>(gridThick->value());
            G.subDivisionNumber = subDiv->value();
            G.displayGrid = showGrid->isChecked();
            G.displaySubGrid = showSub->isChecked();
            G.displayCellIndex = showCellIdx->isChecked();
        }
        if (layer.kind() == core::LayerKind::Brick && brickElev) {
            static_cast<core::LayerBrick&>(layer).displayBrickElevation
                = brickElev->isChecked();
        }
        if (layer.kind() == core::LayerKind::Area && areaCell) {
            static_cast<core::LayerArea&>(layer).areaCellSizeInStud
                = areaCell->value();
        }
        mapView_->rebuildScene();
        layerPanel_->setMap(map, mapView_->builder());
    });

    partsBrowser_ = new PartsBrowser(parts_, this);
    addDockWidget(Qt::LeftDockWidgetArea, partsBrowser_);
    partsBrowser_->setBudget(budget_, [this]{ return static_cast<const core::Map*>(mapView_->currentMap()); });
    const auto recountBudget = [this]{
        if (budget_->exists() && budget_->showBudgetNumbers()) partsBrowser_->refreshBudget();
    };
    connect(mapView_->undoStack(), &QUndoStack::indexChanged, this, recountBudget);
    connect(mapView_, &MapView::layersChanged, this, recountBudget);
    connect(partsBrowser_, &PartsBrowser::partActivated,
            mapView_, &MapView::addPartAtViewCenter);
    // After the user deletes an imported part the on-disk files are
    // gone, but the in-memory parts library still has the entry. Run
    // a full rescan against every configured library path so the
    // deleted part disappears from the grid + part-resolve lookups.
    connect(partsBrowser_, &PartsBrowser::reimportRequested, this, [this](const QString& key){
        reimportPart(key, true);
    });
    connect(partsBrowser_, &PartsBrowser::partDeleted, this, [this]{
        QStringList paths;
        const QString vendored = defaultVendoredPartsRoot();
        if (!vendored.isEmpty()) paths << vendored;
        paths << loadUserLibraryPaths();
        rescanLibrary(paths);
        partsBrowser_->rebuild();
    });

    modulesPanel_ = new ModulesPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, modulesPanel_);
    moduleLibraryPanel_ = new ModuleLibraryPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, moduleLibraryPanel_);
    venueLibraryPanel_ = new VenueLibraryPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, venueLibraryPanel_);
    partUsagePanel_ = new PartUsagePanel(parts_, this);
    addDockWidget(Qt::RightDockWidgetArea, partUsagePanel_);
    partUsagePanel_->bindMapView(mapView_);
    partUsagePanel_->setBudget(budget_);
    connect(moduleLibraryPanel_, &ModuleLibraryPanel::moduleImportRequested,
            this, &MainWindow::onImportModuleFromLibraryPath);
    connect(venueLibraryPanel_, &VenueLibraryPanel::venueLoadRequested,
            this, [this](const core::Venue& v){
        auto* m = mapView_->currentMap();
        if (!m) return;
        if (m->sidecar.venue) {
            const auto btn = QMessageBox::question(this, tr("Replace venue"),
                tr("This project already has a venue. Replace it?"));
            if (btn != QMessageBox::Yes) return;
        }
        mapView_->undoStack()->push(new edit::SetVenueCommand(*m, std::make_optional(v)));
        statusBar()->showMessage(tr("Loaded venue \"%1\"").arg(v.name), 3000);
    });
    connect(venueLibraryPanel_, &VenueLibraryPanel::newLayoutRequested,
            this, [this](const core::Venue& v){ startLayoutFromVenue(v); });
    connect(venueLibraryPanel_, &VenueLibraryPanel::venueSaveRequested,
            this, [this]{
        auto* m = mapView_->currentMap();
        venueLibraryPanel_->saveVenue(m ? m->sidecar.venue : std::nullopt);
    });
    connect(modulesPanel_, &ModulesPanel::moduleDeleteRequested, this, [this](const QString& id){
        if (!mapView_->currentMap()) return;
        mapView_->undoStack()->push(
            new edit::DeleteModuleCommand(*mapView_->currentMap(), id));
        modulesPanel_->setMap(mapView_->currentMap());
    });
    connect(modulesPanel_, &ModulesPanel::createModuleRequested,
            this, &MainWindow::onCreateModuleFromSelection);
    connect(modulesPanel_, &ModulesPanel::importBbmRequested,
            this, &MainWindow::onImportBbmAsModule);

    // Toggle the module's members on/off in the scene selection. If every
    // member is already selected (and nothing outside the module is
    // selected), clicking the row again deselects. Otherwise set the
    // selection to the module's members — lets the user use the normal
    // selection-based actions (arrow-key nudge, rotate, etc.) on the
    // whole module at once.
    connect(modulesPanel_, &ModulesPanel::selectMembersRequested, this, [this](const QString& id){
        auto* map = mapView_->currentMap();
        if (!map) return;
        const core::Module* mod = nullptr;
        for (const auto& m : map->sidecar.modules) if (m.id == id) { mod = &m; break; }
        if (!mod) return;

        // Check whether the current scene selection already equals the
        // module's member set.
        QSet<QString> currentSel;
        for (QGraphicsItem* it : mapView_->scene()->selectedItems()) {
            if (it->data(2).toString().isEmpty()) continue;
            currentSel.insert(it->data(1).toString());
        }
        const bool alreadySelected = !mod->memberIds.isEmpty() && currentSel == mod->memberIds;

        mapView_->deselectAll();
        if (alreadySelected) return;   // toggle off

        for (QGraphicsItem* it : mapView_->scene()->items()) {
            if (it->data(2).toString().isEmpty()) continue;
            if (mod->memberIds.contains(it->data(1).toString())) it->setSelected(true);
        }
    });

    // Move module by a user-entered delta (studs). Already undoable.
    connect(modulesPanel_, &ModulesPanel::moveRequested, this,
            [this](const QString& id, double dx, double dy){
        if (!mapView_->currentMap() || (dx == 0 && dy == 0)) return;
        mapView_->undoStack()->push(new edit::MoveModuleCommand(
            *mapView_->currentMap(), id, QPointF(dx, dy)));
    });

    connect(modulesPanel_, &ModulesPanel::rotateRequested, this,
            [this](const QString& id, double deg){
        if (!mapView_->currentMap()) return;
        mapView_->undoStack()->push(new edit::RotateModuleCommand(
            *mapView_->currentMap(), parts_, id, deg));
    });

    connect(modulesPanel_, &ModulesPanel::flattenRequested, this, [this](const QString& id){
        if (!mapView_->currentMap()) return;
        mapView_->undoStack()->push(new edit::FlattenModuleCommand(*mapView_->currentMap(), id));
        modulesPanel_->setMap(mapView_->currentMap());
    });

    // Rename: undoable name edit via a simple input dialog.
    connect(modulesPanel_, &ModulesPanel::renameRequested, this, [this](const QString& id){
        auto* map = mapView_->currentMap();
        if (!map) return;
        const core::Module* mod = nullptr;
        for (const auto& m : map->sidecar.modules) if (m.id == id) { mod = &m; break; }
        if (!mod) return;
        bool ok = false;
        const QString newName = QInputDialog::getText(this, tr("Rename module"),
            tr("Module name:"), QLineEdit::Normal, mod->name, &ok);
        if (!ok || newName == mod->name) return;
        mapView_->undoStack()->push(new edit::RenameModuleCommand(*map, id, newName));
        modulesPanel_->setMap(map);
    });

    // Clone: duplicate a module in-place so the user can have multiple
    // independent instances. Offset the clone by a small stud delta (or by
    // the module's extent) so the clone doesn't sit on top of the source.
    connect(modulesPanel_, &ModulesPanel::cloneRequested, this, [this](const QString& id){
        auto* map = mapView_->currentMap();
        if (!map) return;
        const core::Module* mod = nullptr;
        for (const auto& m : map->sidecar.modules) if (m.id == id) { mod = &m; break; }
        if (!mod) return;
        // Offset = the module's bounding-box width (so clone lands next to
        // the source instead of overlapping it). If the module has no
        // members, use a trivial 4-stud offset.
        QPointF offset(4, 4);
        QRectF bb;
        for (const auto& L : map->layers()) {
            if (!L || L->kind() != core::LayerKind::Brick) continue;
            for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks) {
                if (mod->memberIds.contains(b.guid)) bb = bb.united(b.displayArea);
            }
        }
        if (!bb.isEmpty()) offset = QPointF(bb.width() + 2.0, 0.0);
        mapView_->undoStack()->push(new edit::CloneModuleCommand(
            *map, id, offset, QString()));
        modulesPanel_->setMap(map);
        statusBar()->showMessage(tr("Module cloned"), 3000);
    });

    // Save module to the configured module library folder as its own .bbm.
    // Matches "Save Selection as Module" but auto-targets the library folder
    // and doesn't require re-picking members.
    connect(modulesPanel_, &ModulesPanel::saveToLibraryRequested, this,
            [this](const QString& id){
        auto* map = mapView_->currentMap();
        if (!map) return;
        const core::Module* mod = nullptr;
        for (const auto& m : map->sidecar.modules) if (m.id == id) { mod = &m; break; }
        if (!mod) return;

        // Build a standalone map with ONE brick layer per source layer
        // that contributes members — preserves z-order / layer identity
        // so re-import drops bricks onto the right layers instead of
        // merging them all into one.
        core::Map out;
        out.author = map->author;
        out.lug    = map->lug;
        out.event  = mod->name.isEmpty() ? QStringLiteral("Module") : mod->name;
        int total = 0;
        for (const auto& L : map->layers()) {
            if (!L || L->kind() != core::LayerKind::Brick) continue;
            auto outL = std::make_unique<core::LayerBrick>();
            outL->guid = core::newBbmId();
            outL->name = L->name.isEmpty() ? QStringLiteral("Module") : L->name;
            outL->transparency = L->transparency;
            outL->visible = L->visible;
            outL->hull = L->hull;
            for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks) {
                if (mod->memberIds.contains(b.guid)) outL->bricks.push_back(b);
            }
            if (outL->bricks.empty()) continue;
            total += static_cast<int>(outL->bricks.size());
            out.layers().push_back(std::move(outL));
        }
        if (total == 0) {
            QMessageBox::information(this, tr("Save to library"),
                tr("This module currently has no brick members."));
            return;
        }
        out.nbItems = total;

        // Read the current module-library folder from QSettings so we pick
        // up any change the user just made in Preferences → Library (the
        // ModuleLibraryPanel caches its path_ from construction time, so
        // its libraryPath() can lag the live setting). Fall back to the
        // panel's value, then to <AppData>/modules.
        QString dir = QSettings().value(QStringLiteral("modules/libraryPath")).toString();
        if (dir.isEmpty()) dir = moduleLibraryPanel_->libraryPath();
        if (dir.isEmpty()) {
            dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + QStringLiteral("/modules");
        }
        moduleLibraryPanel_->setLibraryPath(dir);  // sync panel + persist
        // Ensure the directory exists BEFORE we try to write into it. Show a
        // specific error (instead of the writeBbm "no such file" one) if we
        // can't create it — user can then fix permissions or pick another
        // folder.
        if (!QDir().mkpath(dir)) {
            QMessageBox::warning(this, tr("Save to library"),
                tr("Cannot create or access the module library folder:\n%1\n\n"
                   "Pick a different folder in Preferences → Library.").arg(dir));
            return;
        }

        // Sanitize the filename: strip characters that are invalid on
        // Windows/macOS/Linux filesystems and any leading/trailing dots
        // or spaces. Falls back to "Module" if the result would be empty.
        auto sanitizeFilename = [](QString n) -> QString {
            static const QRegularExpression bad(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])"));
            n.replace(bad, QStringLiteral("_"));
            while (n.startsWith(QLatin1Char('.')) || n.startsWith(QLatin1Char(' '))) n.remove(0, 1);
            while (n.endsWith(QLatin1Char('.'))  || n.endsWith(QLatin1Char(' ')))  n.chop(1);
            if (n.isEmpty()) n = QStringLiteral("Module");
            return n;
        };

        bool ok = false;
        const QString defaultName = sanitizeFilename(mod->name.isEmpty() ? tr("Module") : mod->name);
        const QString rawName = QInputDialog::getText(
            this, tr("Save to library"), tr("Module name (filename):"),
            QLineEdit::Normal, defaultName, &ok);
        if (!ok || rawName.isEmpty()) return;
        const QString name = sanitizeFilename(rawName);
        const QString target = QDir(dir).filePath(name + QStringLiteral(".bbm"));
        if (QFile::exists(target)) {
            const auto btn = QMessageBox::question(this, tr("Save to library"),
                tr("%1 already exists. Overwrite?").arg(target));
            if (btn != QMessageBox::Yes) return;
        }

        // Double-check the immediate parent exists (mkpath above should
        // cover this, but be defensive — the user might have entered a name
        // with a subfolder, or something raced).
        QDir().mkpath(QFileInfo(target).absolutePath());

        auto r = saveload::writeBbm(out, target);
        if (!r.ok) {
            QMessageBox::warning(this, tr("Save to library"),
                tr("%1\n\nTarget path:\n%2").arg(r.error, target));
            return;
        }

        // Update the in-memory module so Re-scan from source works from here on.
        for (auto& m : map->sidecar.modules) {
            if (m.id == id) {
                m.sourceFile = target;
                m.importedAt = QDateTime::currentDateTimeUtc();
                break;
            }
        }
        const int savedCount = out.nbItems;
        moduleLibraryPanel_->refresh();
        modulesPanel_->setMap(map);
        statusBar()->showMessage(
            tr("Saved module '%1' to library (%2 bricks)")
                .arg(name).arg(savedCount), 4000);
    });

    // Re-scan: reload the module's sourceFile, replace its member bricks
    // with fresh imports (new guids), pick up any upstream edits.
    connect(modulesPanel_, &ModulesPanel::rescanRequested, this, [this](const QString& id){
        auto* map = mapView_->currentMap();
        if (!map) return;
        const core::Module* mod = nullptr;
        for (const auto& m : map->sidecar.modules) if (m.id == id) { mod = &m; break; }
        if (!mod) return;
        if (mod->sourceFile.isEmpty()) {
            QMessageBox::information(this, tr("Re-scan module"),
                tr("This module has no source .bbm file (it was created from a selection)."));
            return;
        }
        if (!QFile::exists(mod->sourceFile)) {
            QMessageBox::warning(this, tr("Re-scan module"),
                tr("Source file not found:\n%1").arg(mod->sourceFile));
            return;
        }
        auto res = saveload::readBbm(mod->sourceFile);
        if (!res.ok()) {
            QMessageBox::warning(this, tr("Re-scan module"),
                tr("Could not read %1: %2").arg(mod->sourceFile, res.error));
            return;
        }
        parts::placement::fixStaleAreas(*res.map, parts_);
        // Find the target layer (first brick layer currently holding a member).
        int targetLayer = -1;
        for (int li = 0; li < static_cast<int>(map->layers().size()); ++li) {
            auto* L = map->layers()[li].get();
            if (!L || L->kind() != core::LayerKind::Brick) continue;
            for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks) {
                if (mod->memberIds.contains(b.guid)) { targetLayer = li; break; }
            }
            if (targetLayer >= 0) break;
        }
        if (targetLayer < 0) targetLayer = 0;
        std::vector<core::Brick> fresh;
        for (const auto& L : res.map->layers()) {
            if (!L || L->kind() != core::LayerKind::Brick) continue;
            for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks) {
                core::Brick copy = b;
                copy.guid.clear();
                fresh.push_back(std::move(copy));
            }
        }
        if (fresh.empty()) {
            QMessageBox::information(this, tr("Re-scan module"),
                tr("Source file has no brick layers to import."));
            return;
        }
        mapView_->undoStack()->push(new edit::RescanModuleCommand(
            *map, targetLayer, id, std::move(fresh)));
        modulesPanel_->setMap(map);
    });

    setupMenus();

    // ----- Toolbar — matches BlueBrick's MainForm.Designer.cs item order:
    //   New / Open / Save | Undo / Redo | Delete / Cut / Copy / Paste |
    //   SnapGrid (split) / RotationAngle (drop-down) / RotateCCW /
    //   RotateCW / SendToBack / BringToFront | Tool (split).
    // Icons come from the Qt platform style so the toolbar inherits the
    // host theme's look without bundling assets.
    auto* toolbar = addToolBar(tr("Toolbar"));
    toolbar->setObjectName(QStringLiteral("toolbar.main"));
    toolbar->setMovable(true);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    const QStyle* st = style();

    auto addBtn = [toolbar](const QIcon& icon, const QString& tip,
                            std::function<void()> onClick) {
        auto* a = toolbar->addAction(icon, tip);
        a->setToolTip(tip);
        QObject::connect(a, &QAction::triggered, onClick);
        return a;
    };

    addBtn(st->standardIcon(QStyle::SP_FileIcon),       tr("New"),    [this]{ onNew(); });
    addBtn(st->standardIcon(QStyle::SP_DialogOpenButton), tr("Open"), [this]{ onOpen(); });
    addBtn(st->standardIcon(QStyle::SP_DialogSaveButton), tr("Save"), [this]{ onSave(); });
    toolbar->addSeparator();

    {
        auto* undoIconAct = mapView_->undoStack()->createUndoAction(this);
        undoIconAct->setIcon(st->standardIcon(QStyle::SP_ArrowBack));
        undoIconAct->setToolTip(tr("Undo"));
        toolbar->addAction(undoIconAct);
        auto* redoIconAct = mapView_->undoStack()->createRedoAction(this);
        redoIconAct->setIcon(st->standardIcon(QStyle::SP_ArrowForward));
        redoIconAct->setToolTip(tr("Redo"));
        toolbar->addAction(redoIconAct);
    }
    toolbar->addSeparator();

    addBtn(st->standardIcon(QStyle::SP_TrashIcon),     tr("Delete"), [this]{ mapView_->deleteSelected(); });
    addBtn(st->standardIcon(QStyle::SP_DialogDiscardButton), tr("Cut"), [this]{ mapView_->cutSelection(); });
    addBtn(st->standardIcon(QStyle::SP_DirIcon),       tr("Copy"),   [this]{ mapView_->copySelection(); });
    addBtn(st->standardIcon(QStyle::SP_DialogApplyButton), tr("Paste"), [this]{ mapView_->pasteClipboard(); });
    toolbar->addSeparator();

    // Snap-grid split button: click toggles snap on/off, dropdown picks
    // step. Vanilla offers off + 32/16/8/4/2/1/0.5 studs.
    auto* snapBtn = new QToolButton(this);
    snapBtn->setPopupMode(QToolButton::MenuButtonPopup);
    snapBtn->setToolTip(tr("Snap to grid (click to toggle, ▾ to pick step)"));
    // Override the toolbar's icon-only style so the current snap step
    // is visible at a glance — the dropdown pick is meaningless if the
    // user can't see what's currently selected.
    snapBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* snapMenu = new QMenu(snapBtn);
    const std::vector<std::pair<QString, double>> snapOptions = {
        { QStringLiteral("32"),  32.0 },
        { QStringLiteral("16"),  16.0 },
        { QStringLiteral("8"),    8.0 },
        { QStringLiteral("4"),    4.0 },
        { QStringLiteral("2"),    2.0 },
        { QStringLiteral("1"),    1.0 },
        { QStringLiteral("0.5"),  0.5 },
    };
    auto* snapGroup = new QActionGroup(snapBtn);
    snapGroup->setExclusive(true);
    QHash<double, QAction*> snapActByValue;
    for (const auto& o : snapOptions) {
        auto* a = snapMenu->addAction(o.first);
        a->setCheckable(true);
        a->setData(o.second);
        snapGroup->addAction(a);
        snapActByValue.insert(o.second, a);
        connect(a, &QAction::triggered, this, [this, snapBtn, val = o.second]{
            mapView_->setSnapStepStuds(val);
            snapBtn->setChecked(true);
            snapBtn->setText(QString::number(val));
            QSettings s; s.beginGroup(QStringLiteral("editing"));
            s.setValue(QStringLiteral("snapStepStuds"), val); s.endGroup();
        });
    }
    snapBtn->setMenu(snapMenu);
    snapBtn->setCheckable(true);
    connect(snapBtn, &QToolButton::clicked, this, [this, snapBtn](bool on){
        if (on) {
            // Toggle on: use the previously-checked menu entry, or 32.
            double v = 32.0;
            for (QAction* a : snapBtn->menu()->actions()) {
                if (a->isChecked()) { v = a->data().toDouble(); break; }
            }
            mapView_->setSnapStepStuds(v);
            snapBtn->setText(QString::number(v));
        } else {
            mapView_->setSnapStepStuds(0.0);
            snapBtn->setText(tr("Snap"));
        }
        QSettings s; s.beginGroup(QStringLiteral("editing"));
        s.setValue(QStringLiteral("snapStepStuds"),
                   on ? mapView_->snapStepStuds() : 0.0);
        s.endGroup();
    });
    toolbar->addWidget(snapBtn);

    // Rotation-angle drop-down (no split — picking a value sets the step).
    auto* rotAngleBtn = new QToolButton(this);
    rotAngleBtn->setIcon(st->standardIcon(QStyle::SP_BrowserReload));
    rotAngleBtn->setPopupMode(QToolButton::InstantPopup);
    rotAngleBtn->setToolTip(tr("Rotation step"));
    auto* rotMenu = new QMenu(rotAngleBtn);
    const std::vector<std::pair<QString, double>> rotOptions = {
        { QStringLiteral("90°"),    90.0 },
        { QStringLiteral("45°"),    45.0 },
        { QStringLiteral("22.5°"),  22.5 },
        { QStringLiteral("11.25°"), 11.25 },
        { QStringLiteral("5°"),     5.0 },
        { QStringLiteral("1°"),     1.0 },
    };
    auto* rotGroup = new QActionGroup(rotAngleBtn);
    rotGroup->setExclusive(true);
    QHash<double, QAction*> rotActByValue;
    for (const auto& o : rotOptions) {
        auto* a = rotMenu->addAction(o.first);
        a->setCheckable(true);
        a->setData(o.second);
        rotGroup->addAction(a);
        rotActByValue.insert(o.second, a);
        connect(a, &QAction::triggered, this, [this, rotAngleBtn, label = o.first, val = o.second]{
            mapView_->setRotationStepDegrees(val);
            rotAngleBtn->setText(label);
            QSettings s; s.beginGroup(QStringLiteral("editing"));
            s.setValue(QStringLiteral("rotationStepDegrees"), val); s.endGroup();
        });
    }
    rotAngleBtn->setMenu(rotMenu);
    toolbar->addWidget(rotAngleBtn);

    addBtn(st->standardIcon(QStyle::SP_MediaSeekBackward), tr("Rotate CCW"), [this]{
        mapView_->rotateSelected(static_cast<float>(-mapView_->rotationStepDegrees()));
    });
    addBtn(st->standardIcon(QStyle::SP_MediaSeekForward),  tr("Rotate CW"),  [this]{
        mapView_->rotateSelected(static_cast<float>(mapView_->rotationStepDegrees()));
    });
    addBtn(st->standardIcon(QStyle::SP_MediaSkipBackward), tr("Send to Back"),  [this]{
        mapView_->sendSelectionToBack();
    });
    addBtn(st->standardIcon(QStyle::SP_MediaSkipForward),  tr("Bring to Front"), [this]{
        mapView_->bringSelectionToFront();
    });
    toolbar->addSeparator();

    // Tool split-button — last item per BlueBrick. Click cycles to the
    // next tool, dropdown picks one explicitly. The "Select" tool is the
    // default and isn't in the dropdown (it's just "no special tool").
    auto* toolBtn = new QToolButton(this);
    toolBtn->setPopupMode(QToolButton::MenuButtonPopup);
    toolBtn->setToolTip(tr("Drawing tool"));
    auto* toolMenu = new QMenu(toolBtn);
    struct ToolEntry { MapView::Tool t; QString label; QStyle::StandardPixmap icon; };
    const std::vector<ToolEntry> toolEntries = {
        { MapView::Tool::Select,            tr("Select"),         QStyle::SP_ArrowRight },
        { MapView::Tool::PaintArea,         tr("Paint area"),     QStyle::SP_DialogYesButton },
        { MapView::Tool::EraseArea,         tr("Erase area"),     QStyle::SP_DialogNoButton },
        { MapView::Tool::DrawLinearRuler,   tr("Add ruler"),      QStyle::SP_ToolBarHorizontalExtensionButton },
        { MapView::Tool::DrawCircularRuler, tr("Add circle"),     QStyle::SP_DirHomeIcon },
    };
    auto* toolGroup = new QActionGroup(toolBtn);
    toolGroup->setExclusive(true);
    QHash<int, QAction*> toolActByEnum;
    for (const auto& e : toolEntries) {
        const QIcon ic = st->standardIcon(e.icon);
        auto* a = toolMenu->addAction(ic, e.label);
        a->setCheckable(true);
        a->setData(static_cast<int>(e.t));
        toolGroup->addAction(a);
        toolActByEnum.insert(static_cast<int>(e.t), a);
        connect(a, &QAction::triggered, this, [this, toolBtn, ic, label = e.label, t = e.t]{
            mapView_->setTool(t);
            toolBtn->setIcon(ic);
            toolBtn->setText(label);
            toolBtn->setToolTip(label);
        });
    }
    toolBtn->setMenu(toolMenu);
    {
        // Default surface = Paint, matching BlueBrick.
        const QIcon ic = st->standardIcon(QStyle::SP_DialogYesButton);
        toolBtn->setIcon(ic);
    }
    connect(toolBtn, &QToolButton::clicked, this, [this, toolMenu]{
        // Click on the button face cycles to the next tool in the menu —
        // matches BlueBrick's ButtonClick = next-paint-tool behaviour.
        const auto acts = toolMenu->actions();
        QAction* current = nullptr;
        int currentIdx = -1;
        for (int i = 0; i < acts.size(); ++i) {
            if (acts[i]->isChecked()) { current = acts[i]; currentIdx = i; break; }
        }
        const int n = acts.size();
        if (n == 0) return;
        const int next = (currentIdx + 1) % n;
        if (current) current->setChecked(false);
        acts[next]->setChecked(true);
        acts[next]->trigger();
    });
    toolbar->addWidget(toolBtn);

    // Paint colour picker — vanilla puts this inside the Paint submenu;
    // adding it to the toolbar tail keeps colour one click away.
    auto* colorBtn = new QToolButton(this);
    colorBtn->setToolTip(tr("Paint colour"));
    auto refreshColorBtn = [this, colorBtn]{
        QPixmap pm(20, 20);
        pm.fill(mapView_->paintColor());
        colorBtn->setIcon(QIcon(pm));
    };
    {
        QSettings s; s.beginGroup(QStringLiteral("editing"));
        QColor savedColor(s.value(QStringLiteral("paintColor"),
                                    QColor(0, 128, 0).name()).toString());
        s.endGroup();
        // Force opaque if the persisted color happened to land at alpha 0
        // (an old corrupted setting, or a user picking transparent in the
        // colour dialog). A 0-alpha paint colour silently does nothing on
        // the canvas, which is the most confusing failure mode possible.
        if (savedColor.isValid() && savedColor.alpha() == 0) savedColor.setAlpha(255);
        if (savedColor.isValid()) mapView_->setPaintColor(savedColor);
    }
    refreshColorBtn();
    toolbar->addWidget(colorBtn);
    connect(colorBtn, &QToolButton::clicked, this, [this, refreshColorBtn]{
        const QColor c = QColorDialog::getColor(mapView_->paintColor(), this,
                                                 tr("Paint colour"),
                                                 QColorDialog::ShowAlphaChannel);
        if (!c.isValid()) return;
        mapView_->setPaintColor(c);
        refreshColorBtn();
        QSettings s; s.beginGroup(QStringLiteral("editing"));
        s.setValue(QStringLiteral("paintColor"), c.name(QColor::HexArgb));
        s.endGroup();
    });

    // Restore persisted snap + rotation step from QSettings, sync the
    // toolbar widgets, and select the default tool.
    {
        QSettings s;
        s.beginGroup(QStringLiteral("editing"));
        const double snap = s.value(QStringLiteral("snapStepStuds"), 0.0).toDouble();
        const double rot  = s.value(QStringLiteral("rotationStepDegrees"), 90.0).toDouble();
        s.endGroup();
        mapView_->setSnapStepStuds(snap);
        mapView_->setRotationStepDegrees(rot);
        if (snap > 0.0) {
            snapBtn->setChecked(true);
            snapBtn->setText(QString::number(snap));
            if (auto* a = snapActByValue.value(snap)) a->setChecked(true);
        } else {
            snapBtn->setChecked(false);
            snapBtn->setText(tr("Snap"));
            // Pre-check 32 as the value to fall back to when toggled on.
            if (auto* a = snapActByValue.value(32.0)) a->setChecked(true);
        }
        if (auto* a = rotActByValue.value(rot)) {
            a->setChecked(true);
            rotAngleBtn->setText(a->text());
        }
        if (auto* a = toolActByEnum.value(static_cast<int>(MapView::Tool::Select))) {
            a->setChecked(true);
        }
    }

    updateTitle();

    connect(mapView_->undoStack(), &QUndoStack::indexChanged,
            this, [this](int){ updateTitle(); });
    connect(mapView_->undoStack(), &QUndoStack::cleanChanged,
            this, [this](bool){ updateTitle(); });
    // Any undo-stack change can add or remove a module (set expansion,
    // Group-into-Module, import-as-module, flatten, clone, rename,
    // plus undo/redo of any of those). Refresh the panel off the stack
    // so every path stays consistent without each call site remembering.
    connect(mapView_->undoStack(), &QUndoStack::indexChanged,
            this, [this](int){
        if (modulesPanel_) modulesPanel_->setMap(mapView_->currentMap());
    });

    // Permanent right-side status label showing map dimensions in studs,
    // modules (96 studs), and meters (1 stud = 8 mm). Upstream shows the same
    // trio in its status bar. Refreshed on every undo-stack change so it
    // stays live while the user edits.
    auto* dimsLabel = new QLabel(this);
    statusBar()->addPermanentWidget(dimsLabel);
    auto refreshDims = [this, dimsLabel]{
        if (!mapView_->currentMap()) { dimsLabel->clear(); return; }
        const QRectF bb = mapView_->scene()->itemsBoundingRect();
        if (bb.isEmpty()) { dimsLabel->setText(tr("empty")); return; }
        const double studsPerPx = 1.0 / 8.0;
        const double wStud = bb.width()  * studsPerPx;
        const double hStud = bb.height() * studsPerPx;
        dimsLabel->setText(tr("%1 × %2 studs  (%3 × %4 m)")
            .arg(wStud, 0, 'f', 0).arg(hStud, 0, 'f', 0)
            .arg(wStud * 0.008, 0, 'f', 2).arg(hStud * 0.008, 0, 'f', 2));
    };
    connect(mapView_->undoStack(), &QUndoStack::indexChanged, this, [refreshDims](int){ refreshDims(); });
    QTimer::singleShot(0, this, refreshDims);

    // Live selection readout — helps confirm that clicks and drag-select
    // are actually producing a selection. Updates whenever the scene's
    // selection set changes.
    QLabel* selLabel = new QLabel(tr("no selection"), this);
    statusBar()->addPermanentWidget(selLabel);
    connect(mapView_, &MapView::selectionChanged, this, [this, selLabel]{
        const int n = mapView_->scene()->selectedItems().size();
        selLabel->setText(n == 0 ? tr("no selection")
                                 : tr("selected: %1").arg(n));
    });

    // Venue-validator readout — counts walkway-buffer / outside-outline /
    // obstacle-overlap violations. Clicking the label opens a modeless
    // list dialog. Non-blocking: we never stop the user placing a brick
    // that trips a rule, just surface the warning count.
    QLabel* venueLabel = new QLabel(this);
    venueLabel->setCursor(Qt::PointingHandCursor);
    statusBar()->addPermanentWidget(venueLabel);
    auto refreshVenueStatus = [this, venueLabel]{
        auto* map = mapView_->currentMap();
        if (!map || !map->sidecar.venue || !map->sidecar.venue->enabled) {
            venueLabel->clear();
            venueLabel->setToolTip({});
            return;
        }
        const auto violations = edit::validateVenue(*map);
        if (violations.isEmpty()) {
            venueLabel->setText(tr("Venue: OK"));
            venueLabel->setStyleSheet(QStringLiteral("color: #2a7d2a;"));
            venueLabel->setToolTip(tr("No layout problems against the current venue"));
        } else {
            venueLabel->setText(tr("Venue: %1 issue(s)").arg(violations.size()));
            venueLabel->setStyleSheet(QStringLiteral("color: #cc6600; font-weight: bold;"));
            QStringList lines;
            for (const auto& v : violations) {
                lines.append(QStringLiteral("• ") + v.description);
                if (lines.size() >= 12) { lines.append(QStringLiteral("…")); break; }
            }
            venueLabel->setToolTip(lines.join(QChar('\n')));
        }
    };
    connect(mapView_->undoStack(), &QUndoStack::indexChanged,
            this, [refreshVenueStatus](int){ refreshVenueStatus(); });
    connect(mapView_, &MapView::layersChanged, this,
            [refreshVenueStatus]{ refreshVenueStatus(); });
    QTimer::singleShot(0, this, refreshVenueStatus);

    // Budget status readout — mirrors the venue one. Silent without a budget.
    QLabel* budgetLabel = new QLabel(this);
    statusBar()->addPermanentWidget(budgetLabel);
    auto refreshBudgetStatus = [this, budgetLabel]{
        auto* map = mapView_->currentMap();
        const auto* budget = budget_->budget();
        if (!map || !budget) {
            budgetLabel->clear();
            budgetLabel->setToolTip({});
            return;
        }
        const auto violations = edit::checkBudget(*map, *budget);
        if (violations.isEmpty()) {
            budgetLabel->setText(tr("Budget: OK"));
            budgetLabel->setStyleSheet(QStringLiteral("color: #2a7d2a;"));
            budgetLabel->setToolTip(tr("Every budgeted part is within its limit"));
        } else {
            budgetLabel->setText(tr("Budget: %1 over").arg(violations.size()));
            budgetLabel->setStyleSheet(QStringLiteral("color: #a03030; font-weight: bold;"));
            QStringList lines;
            for (const auto& v : violations) {
                lines.append(tr("• %1: %2 / %3  (+%4)")
                    .arg(v.partNumber).arg(v.used).arg(v.limit).arg(v.overBy));
                if (lines.size() >= 15) { lines.append(QStringLiteral("…")); break; }
            }
            budgetLabel->setToolTip(lines.join(QChar('\n')));
        }
    };
    connect(budget_, &BudgetSession::changed, this, refreshBudgetStatus);
    connect(mapView_->undoStack(), &QUndoStack::indexChanged,
            this, [refreshBudgetStatus](int){ refreshBudgetStatus(); });
    QTimer::singleShot(0, this, refreshBudgetStatus);
    connect(mapView_, &MapView::layersChanged, this, [this]{
        layerPanel_->setMap(mapView_->currentMap(), mapView_->builder());
    });

    // Auto-save: 60s tick, writes to AppDataLocation/autosave.bld-layout whenever
    // the undo stack is dirty. Cheap enough to run unconditionally; the
    // writeMapTo path is the same used for manual saves.
    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setInterval(60 * 1000);
    connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::performAutosave);
    autosaveTimer_->start();

    // Permanent autosave indicator pinned to the status bar's right side.
    // Shows the last successful autosave time; flashes green briefly when
    // a save just completed so the user knows their work is being captured.
    // The transient status-bar message can be missed; a sticky label can't.
    auto* autosaveLabel = new QLabel(tr("Autosave: —"), this);
    autosaveLabel->setToolTip(tr("Last successful autosave time"));
    statusBar()->addPermanentWidget(autosaveLabel);
    connect(autosaveTimer_, &QTimer::timeout, this, [this, autosaveLabel]{
        if (!mapView_->currentMap()) return;
        if (mapView_->undoStack()->isClean()) return;
        autosaveLabel->setText(tr("Autosave: %1").arg(QTime::currentTime().toString("HH:mm:ss")));
        autosaveLabel->setStyleSheet(QStringLiteral("QLabel{color:#2c7a2c;font-weight:600;}"));
        QTimer::singleShot(1500, autosaveLabel, [autosaveLabel]{
            autosaveLabel->setStyleSheet({});
        });
    });
    // Also update on the throttled save path so quick edits show the
    // freshest time without waiting for the 60 s tick.
    connect(mapView_->undoStack(), &QUndoStack::indexChanged,
            this, [this, autosaveLabel](int){
        if (!mapView_->currentMap() || mapView_->undoStack()->isClean()) return;
        // performAutosaveThrottled has its own timing; this label just
        // reflects "we're modified — autosave is watching" rather than a
        // hard guarantee a write just happened.
        autosaveLabel->setText(tr("Autosave: %1*").arg(QTime::currentTime().toString("HH:mm:ss")));
    });
    // Also autosave on every undo-stack change, throttled so rapid
    // edits don't hit the disk more than once every 5 seconds. This
    // caps crash-related data loss to that window — which is good
    // enough without needing async-signal-safe signal handlers.
    connect(mapView_->undoStack(), &QUndoStack::indexChanged,
            this, [this](int){ performAutosaveThrottled(); });

    statusBar()->showMessage(
        tr("Parts library: %1 parts indexed").arg(parts_.partCount()));

    // Restore the window geometry + dock layout the user left at last exit,
    // and assign stable object names so Qt can identify each dock on save/restore.
    layerPanel_->setObjectName(QStringLiteral("dock.layers"));
    partsBrowser_->setObjectName(QStringLiteral("dock.parts"));
    modulesPanel_->setObjectName(QStringLiteral("dock.modules"));
    moduleLibraryPanel_->setObjectName(QStringLiteral("dock.moduleLibrary"));
    venueLibraryPanel_->setObjectName(QStringLiteral("dock.venueLibrary"));
    partUsagePanel_->setObjectName(QStringLiteral("dock.partUsage"));
    QSettings s;
    s.beginGroup(QStringLiteral("ui"));
    const QByteArray geom = s.value(QStringLiteral("geometry")).toByteArray();
    const QByteArray state = s.value(QStringLiteral("state")).toByteArray();
    s.endGroup();
    if (!geom.isEmpty())  restoreGeometry(geom);
    if (!state.isEmpty()) restoreState(state);
}

MainWindow::~MainWindow() {
#ifdef BLD_SYNC
    // Close a live layout while the map view it draws on still exists.
    if (live_) {
        live_->disconnect(this);
        live_->close();
    }
#endif
}


void MainWindow::onCreateModuleFromSelection() {
    auto* map = mapView_->currentMap();
    if (!map) return;
    std::vector<edit::CreateModuleCommand::Member> members;
    for (QGraphicsItem* it : mapView_->scene()->selectedItems()) {
        if (it->data(2).toString() != QStringLiteral("brick")) continue;
        members.push_back({ it->data(0).toInt(), it->data(1).toString() });
    }
    if (members.empty()) {
        QMessageBox::information(this, tr("Create module"),
            tr("Select one or more bricks first."));
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Create module"), tr("Module name:"),
        QLineEdit::Normal, tr("New Module"), &ok);
    if (!ok || name.isEmpty()) return;
    mapView_->undoStack()->push(new edit::CreateModuleCommand(*map, name, std::move(members)));
    modulesPanel_->setMap(map);
    statusBar()->showMessage(tr("Module created"), 3000);
}

void MainWindow::onSaveSelectionAsModule() {
    auto* map = mapView_->currentMap();
    if (!map) return;
    // Gather selected bricks BY source layer so the written .bbm has one
    // brick layer per source layer. Re-importing preserves the z-order
    // and layer membership of every piece — otherwise tracks end up on
    // top of scenery, etc.
    struct PickedBrick { int layerIndex; core::Brick brick; };
    std::vector<PickedBrick> picks;
    for (QGraphicsItem* it : mapView_->scene()->selectedItems()) {
        if (it->data(2).toString() != QStringLiteral("brick")) continue;
        const int li = it->data(0).toInt();
        const QString guid = it->data(1).toString();
        if (li < 0 || li >= static_cast<int>(map->layers().size())) continue;
        auto* L = map->layers()[li].get();
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<core::LayerBrick&>(*L).bricks) {
            if (b.guid == guid) { picks.push_back({ li, b }); break; }
        }
    }
    if (picks.empty()) {
        QMessageBox::information(this, tr("Save module"),
            tr("Select one or more bricks first."));
        return;
    }

    // Build the module map with one LayerBrick per source layer. Iterate
    // host layers in their original order so the exported z-order matches
    // the source project.
    core::Map module;
    module.author = map->author;
    module.lug = map->lug;
    module.event = QObject::tr("Module");
    int total = 0;
    for (int li = 0; li < static_cast<int>(map->layers().size()); ++li) {
        auto* src = map->layers()[li].get();
        if (!src || src->kind() != core::LayerKind::Brick) continue;
        auto outL = std::make_unique<core::LayerBrick>();
        outL->guid = core::newBbmId();
        outL->name = src->name.isEmpty() ? QStringLiteral("Module") : src->name;
        outL->transparency = src->transparency;
        outL->visible = src->visible;
        outL->hull = src->hull;
        for (const auto& p : picks)
            if (p.layerIndex == li) outL->bricks.push_back(p.brick);
        if (outL->bricks.empty()) continue;
        total += static_cast<int>(outL->bricks.size());
        module.layers().push_back(std::move(outL));
    }
    module.nbItems = total;

    // Pick target path: if module library path is configured, default there;
    // otherwise fall back to QFileDialog's default.
    QString startDir = moduleLibraryPanel_->libraryPath();
    QDir().mkpath(startDir);  // best-effort
    const QString rawName = QInputDialog::getText(
        this, tr("Save module"), tr("Module name:"),
        QLineEdit::Normal, tr("New Module"));
    if (rawName.isEmpty()) return;
    // Sanitize the filename — same logic as the save-to-library handler.
    auto sanitize = [](QString n) -> QString {
        static const QRegularExpression bad(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])"));
        n.replace(bad, QStringLiteral("_"));
        while (n.startsWith(QLatin1Char('.')) || n.startsWith(QLatin1Char(' '))) n.remove(0, 1);
        while (n.endsWith(QLatin1Char('.'))  || n.endsWith(QLatin1Char(' ')))  n.chop(1);
        if (n.isEmpty()) n = QStringLiteral("Module");
        return n;
    };
    const QString defaultName = sanitize(rawName);
    QString target = QFileDialog::getSaveFileName(
        this, tr("Save selection as module"),
        startDir.isEmpty() ? defaultName + ".bbm" : QDir(startDir).filePath(defaultName + ".bbm"),
        tr("BlueBrick map (*.bbm)"));
    if (target.isEmpty()) return;
    if (!target.endsWith(QStringLiteral(".bbm"), Qt::CaseInsensitive)) target += QStringLiteral(".bbm");

    auto r = saveload::writeBbm(module, target);
    if (!r.ok) {
        QMessageBox::warning(this, tr("Save module failed"), r.error);
        return;
    }
    statusBar()->showMessage(tr("Saved %1 bricks to %2").arg(picks.size()).arg(target), 4000);
    moduleLibraryPanel_->refresh();
}

void MainWindow::onSaveSelectionAsSet() {
    // Export the current brick selection as a BrickTracks-style
    // `.set.xml` — a <group> with one <SubPart> per selected brick,
    // carrying position (rotated-hull-bbox centre, set-local studs) and
    // angle. Matches the schema of the set files under parts/BrickTracks/
    // and parts/TrixBrix/, so the written file drops straight into a
    // BlueBrick-compatible parts library and expands correctly in both
    // vanilla BlueBrick and this app.
    //
    // Only bricks are supported — sets don't carry rulers / labels /
    // areas / text. Pick the geometric centroid of the selected bricks'
    // hull-bbox-centres as the origin so "drop the set on the cursor"
    // lands roughly where the user expects.
    auto* map = mapView_->currentMap();
    if (!map) return;
    struct Picked { core::Brick brick; };
    std::vector<Picked> picks;
    for (QGraphicsItem* it : mapView_->scene()->selectedItems()) {
        if (it->data(2).toString() != QStringLiteral("brick")) continue;
        const int li = it->data(0).toInt();
        const QString guid = it->data(1).toString();
        if (li < 0 || li >= static_cast<int>(map->layers().size())) continue;
        auto* L = map->layers()[li].get();
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<core::LayerBrick&>(*L).bricks) {
            if (b.guid == guid) { picks.push_back({ b }); break; }
        }
    }
    if (picks.empty()) {
        QMessageBox::information(this, tr("Save set"),
            tr("Select one or more bricks first."));
        return;
    }

    // BlueBrick stores each subpart's sp.position as its displayArea
    // centre (Brick.Center).
    std::vector<QPointF> hullCentres;
    hullCentres.reserve(picks.size());
    double sumX = 0.0, sumY = 0.0;
    for (const auto& p : picks) {
        const QPointF hullCentre = p.brick.displayArea.center();
        hullCentres.push_back(hullCentre);
        sumX += hullCentre.x();
        sumY += hullCentre.y();
    }
    const QPointF centroid(sumX / picks.size(), sumY / picks.size());

    saveload::SetManifest manifest;
    manifest.author = map->author.isEmpty()
        ? QStringLiteral("Brick Layout Designer")
        : map->author;
    manifest.canUngroup = true;
    manifest.subparts.reserve(picks.size());
    for (size_t i = 0; i < picks.size(); ++i) {
        saveload::SetSubpart sp;
        sp.partKey = picks[i].brick.partNumber;
        sp.positionStuds = hullCentres[i] - centroid;
        sp.angleDegrees = picks[i].brick.orientation;
        manifest.subparts.push_back(std::move(sp));
    }

    const QString rawName = QInputDialog::getText(
        this, tr("Save set"), tr("Set name:"),
        QLineEdit::Normal, tr("New Set"));
    if (rawName.isEmpty()) return;
    manifest.name = rawName;
    // Filename sanitisation (same rules as save-selection-as-module).
    QString safeName = rawName;
    static const QRegularExpression bad(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])"));
    safeName.replace(bad, QStringLiteral("_"));
    while (safeName.startsWith(QLatin1Char('.')) || safeName.startsWith(QLatin1Char(' ')))
        safeName.remove(0, 1);
    while (safeName.endsWith(QLatin1Char('.')) || safeName.endsWith(QLatin1Char(' ')))
        safeName.chop(1);
    if (safeName.isEmpty()) safeName = QStringLiteral("Set");
    if (!safeName.endsWith(QStringLiteral(".set"), Qt::CaseInsensitive))
        safeName += QStringLiteral(".set");

    // Default target: first configured user library path so placing +
    // reloading round-trips. Falls back to project directory otherwise.
    QSettings s;
    QStringList userPaths = s.value(QStringLiteral("LibraryPaths/UserPaths"))
                              .toStringList();
    const QString startDir = userPaths.isEmpty()
        ? QDir::homePath()
        : userPaths.first();
    QDir().mkpath(startDir);
    QString target = QFileDialog::getSaveFileName(
        this, tr("Save selection as set"),
        QDir(startDir).filePath(safeName + QStringLiteral(".xml")),
        tr("BlueBrick set (*.set.xml)"));
    if (target.isEmpty()) return;
    if (!target.endsWith(QStringLiteral(".set.xml"), Qt::CaseInsensitive)) {
        if (target.endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive))
            target = target.left(target.size() - 4) + QStringLiteral(".set.xml");
        else
            target += QStringLiteral(".set.xml");
    }

    QString err;
    if (!saveload::writeSetXml(target, manifest, &err)) {
        QMessageBox::warning(this, tr("Save set failed"), err);
        return;
    }
    statusBar()->showMessage(
        tr("Saved set %1 (%2 subparts) to %3")
            .arg(rawName).arg(picks.size()).arg(target), 4000);
    // Rescan the library so the new set shows up immediately in the
    // Parts panel without needing a manual reload.
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
}

// Build the per-source-layer batches for importing a module .bbm. Each
// brick layer in the file becomes one LayerBatch whose layerName matches
// the source's layer name. The command then resolves names against the
// host map's brick layers and creates new layers when no name matches —
// preserving the module's original z-order.
static std::vector<edit::ImportBbmAsModuleCommand::LayerBatch>
batchesFromModuleMap(const core::Map& loaded) {
    std::vector<edit::ImportBbmAsModuleCommand::LayerBatch> batches;
    for (const auto& L : loaded.layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        edit::ImportBbmAsModuleCommand::LayerBatch batch;
        batch.layerName = L->name.isEmpty() ? QStringLiteral("Module") : L->name;
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks) {
            core::Brick copy = b;
            copy.guid.clear();  // command mints fresh guids
            batch.bricks.push_back(std::move(copy));
        }
        if (!batch.bricks.empty()) batches.push_back(std::move(batch));
    }
    return batches;
}

void MainWindow::onImportModuleFromLibraryPath(const QString& bbmPath) {
    auto* map = mapView_->currentMap();
    if (!map) return;
    auto loaded = saveload::readBbm(bbmPath);
    if (!loaded.ok()) {
        QMessageBox::warning(this, tr("Import failed"), loaded.error);
        return;
    }
    parts::placement::fixStaleAreas(*loaded.map, parts_);
    auto batches = batchesFromModuleMap(*loaded.map);
    if (batches.empty()) return;
    int total = 0;
    for (const auto& b : batches) total += b.bricks.size();
    const QString name = QFileInfo(bbmPath).baseName();
    mapView_->undoStack()->push(new edit::ImportBbmAsModuleCommand(
        *map, bbmPath, name, std::move(batches)));
    modulesPanel_->setMap(map);
    layerPanel_->setMap(map, mapView_->builder());
    statusBar()->showMessage(
        tr("Imported %1 bricks from module '%2'").arg(total).arg(name), 4000);
}

void MainWindow::onImportBbmAsModule() {
    auto* map = mapView_->currentMap();
    if (!map) return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import .bbm as module"), {},
        tr("BlueBrick map (*.bbm)"));
    if (path.isEmpty()) return;
    auto loaded = saveload::readBbm(path);
    if (!loaded.ok()) {
        QMessageBox::warning(this, tr("Import failed"), loaded.error);
        return;
    }
    parts::placement::fixStaleAreas(*loaded.map, parts_);
    auto batches = batchesFromModuleMap(*loaded.map);
    if (batches.empty()) {
        QMessageBox::information(this, tr("Import module"),
            tr("The selected file has no brick layers to import."));
        return;
    }
    int imported = 0;
    for (const auto& b : batches) imported += b.bricks.size();
    const QString name = QFileInfo(path).baseName();
    mapView_->undoStack()->push(new edit::ImportBbmAsModuleCommand(
        *map, path, name, std::move(batches)));
    modulesPanel_->setMap(map);
    layerPanel_->setMap(map, mapView_->builder());
    statusBar()->showMessage(
        tr("Imported %1 bricks as module '%2'").arg(imported).arg(name), 4000);
}


void MainWindow::onZoomIn()  { mapView_->scale(1.2, 1.2); }
void MainWindow::onZoomOut() { mapView_->scale(1 / 1.2, 1 / 1.2); }

void MainWindow::onFitToView() {
    if (!mapView_->currentMap() || mapView_->scene()->itemsBoundingRect().isEmpty()) return;
    mapView_->fitInView(mapView_->scene()->itemsBoundingRect().adjusted(-50, -50, 50, 50),
                         Qt::KeepAspectRatio);
}

}
