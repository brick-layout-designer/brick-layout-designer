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

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../edit/Connectivity.h"
#include "../import/ImportConnections.h"
#include "../import/ImportToPart.h"
#include "../import/ldd/LDDLDrawMapping.h"
#include "../import/ldd/LDDMaterials.h"
#include "../import/ldd/LDDMeshBuilder.h"
#include "../import/ldd/LDDReader.h"
#include "../import/lif/LifReader.h"
#include "../import/ldraw/LDrawLibrary.h"
#include "../import/ldraw/LDrawMeshBuilder.h"
#include "../import/ldraw/LDrawMeshLoader.h"
#include "../import/ldraw/LDrawPalette.h"
#include "../import/ldraw/LDrawRasterize.h"
#include "../import/ldraw/LDrawReader.h"
#include "../import/mesh/MeshRasterize.h"
#include "../import/studio/StudioReader.h"
#include "../parts/PartsLibrary.h"
#include "../rendering/SceneBuilder.h"

#include "BackgroundTask.h"
#include "DownloadCenterDialog.h"
#include "ImportPreviewDialog.h"

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QImage>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QVector>

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
    // Shared helper: parse an LDraw-style read result, composite it to
    // a flat top-down sprite using whatever BlueBrick library parts
    // resolve, save as a new library part, rescan the parts panel, and
    // place the fresh part at the view centre so the user can work
    // with it right away. The goal (per user spec) is NOT to load the
    // imported file as a whole map — it's to turn the imported model
    // into a single composite part.
    auto importAsPart = [this](const QString& source,
                               const import::LDrawReadResult& read,
                               const QString& kindLabel) {
        if (!read.ok) {
            QMessageBox::warning(this, kindLabel,
                tr("Parse failed: %1").arg(read.error));
            return;
        }
        auto modelMap = import::toBlueBrickMap(read, &parts_);
        const bool hasRefs = modelMap && !modelMap->layers().empty();
        if (!hasRefs && read.primitives.empty()) {
            QMessageBox::warning(this, kindLabel,
                tr("No usable parts or primitives in %1").arg(source));
            return;
        }

        // Render the imported map into a QImage. Use a dedicated
        // scene + SceneBuilder so we don't disturb the user's current
        // view. When no library parts resolved (or the scene is
        // empty after compositing) we fall back to rasterizing the
        // file's inline primitives directly — type-3/4 fills + type-2
        // outlines at BlueBrick's 8 px/stud scale via
        // import::rasterizeTopDown.
        QGraphicsScene renderScene;
        renderScene.setBackgroundBrush(Qt::transparent);
        rendering::SceneBuilder renderer(renderScene, parts_);
        if (hasRefs) renderer.build(*modelMap);
        QRectF bounds = renderScene.itemsBoundingRect().adjusted(-4, -4, 4, 4);
        if (bounds.isEmpty() && !read.primitives.empty()) {
            // Bootstrap a scene from the primitive raster. We can
            // place the rasterized image as a single pixmap item and
            // treat it like any other library part from here on.
            const QImage primImg = import::rasterizeTopDown(read);
            if (!primImg.isNull()) {
                auto* item = renderScene.addPixmap(QPixmap::fromImage(primImg));
                item->setOffset(-primImg.width() / 2.0, -primImg.height() / 2.0);
                bounds = item->boundingRect().translated(item->pos())
                            .adjusted(-4, -4, 4, 4);
            }
        }
        if (bounds.isEmpty()) {
            QMessageBox::warning(this, kindLabel,
                tr("Rendered model is empty."));
            return;
        }
        constexpr double kPxPerStud = 8.0;
        const int wPx = std::max(8, static_cast<int>(std::ceil(bounds.width())));
        const int hPx = std::max(8, static_cast<int>(std::ceil(bounds.height())));
        QImage sprite(wPx, hPx, QImage::Format_ARGB32);
        sprite.fill(Qt::transparent);
        {
            QPainter p(&sprite);
            p.setRenderHint(QPainter::Antialiasing);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            renderScene.render(&p, QRectF(0, 0, wPx, hPx), bounds, Qt::KeepAspectRatio);
        }
        const int wStud = std::max(1, static_cast<int>(std::round(wPx / kPxPerStud)));
        const int hStud = std::max(1, static_cast<int>(std::round(hPx / kPxPerStud)));

        // Free connection ends of the model become the composite part's
        // <ConnexionList>, relative to the sprite centre, so it snaps like
        // a real track tile.
        const QPointF spriteCentreStuds(
            (bounds.left() + bounds.right()) * 0.5 / kPxPerStud,
            (bounds.top()  + bounds.bottom()) * 0.5 / kPxPerStud);
        const QVector<import::ImportedConnection> externalConns = hasRefs
            ? import::externalConnections(*modelMap, parts_, spriteCentreStuds)
            : QVector<import::ImportedConnection>{};

        // Preview before commit. The legacy path doesn't track granular
        // resolved/unresolved counts the way the LDraw-library and LDD
        // paths do, so the stats panel will be sparse — that's fine,
        // the sprite preview is the part the user really cares about.
        ImportPreviewDialog::Stats st;
        st.ldrawResolved = static_cast<int>(read.parts.size());
        ImportPreviewDialog dlg(source, kindLabel, sprite,
                                wStud, hStud, st, {}, externalConns, this);
        if (dlg.exec() != QDialog::Accepted) {
            statusBar()->showMessage(tr("Import cancelled."), 3000);
            return;
        }

        QString libRoot = QSettings().value(QStringLiteral("modules/libraryPath")).toString();
        if (libRoot.isEmpty()) {
            libRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                      + QStringLiteral("/imports");
        } else {
            libRoot += QStringLiteral("/imports");
        }
        QString err;
        const QString author = mapView_->currentMap()
                                   ? mapView_->currentMap()->author : QString();
        const QString fileKey = import::writeImportedModelAsLibraryPart(
            dlg.partName(), sprite, wStud, hStud, libRoot, author,
            externalConns, &err);
        if (fileKey.isEmpty()) {
            QMessageBox::warning(this, kindLabel,
                tr("Could not write library part: %1").arg(err));
            return;
        }

        // Incrementally register the new part instead of clearing the
        // library and re-scanning thousands of vendored parts on the UI
        // thread (which froze the app for 5–10s on big libraries).
        const QString xmlPath = QDir(libRoot).filePath(fileKey + QStringLiteral(".xml"));
        const QString libKey = registerImportedPart(xmlPath);
        if (!libKey.isEmpty()) mapView_->addPartAtViewCenter(libKey);

        statusBar()->showMessage(
            tr("Imported %1 as part '%2' (%3 × %4 studs, %5 source parts)")
                .arg(QFileInfo(source).fileName(), fileKey)
                .arg(wStud).arg(hStud).arg(read.parts.size()), 6000);
    };

    // Newer LDraw / Studio import path: bake the model's geometry
    // through a user-pointed LDraw library + LDConfig.ldr palette,
    // rasterize top-down, and emit a sprite. Used when the user has
    // configured `import/ldrawLibraryPath` in Preferences. Falls back
    // to the legacy BlueBrickParts-rendering closure when unset or
    // invalid so existing users don't lose the feature mid-rollout.
    auto importViaLDrawLibrary = [this](const QString& source,
                                          const import::LDrawReadResult& read,
                                          const QString& kindLabel) -> bool {
        if (!read.ok) return false;
        const QString libRoot = QSettings().value(
            QStringLiteral("import/ldrawLibraryPath")).toString();
        if (libRoot.isEmpty()) return false;
        import::LDrawLibrary lib(libRoot);
        if (!lib.looksValid()) return false;

        import::LDrawPalette palette;
        palette.loadFromLDConfig(QDir(libRoot).absoluteFilePath(
            QStringLiteral("LDConfig.ldr")));

        // Bake + rasterize on a worker thread so the UI stays
        // responsive while a multi-thousand-part LDraw / Studio
        // model is being processed (can take 30s+ for big MOCs).
        // Cancel checkpoint sits between bake and rasterize so the
        // user can bail at the largest natural boundary.
        import::LDrawMeshLoader loader(lib, palette);
        import::BakedModel baked;
        import::RasterizeResult rast;
        const bool ok = runBackground(this,
            tr("Importing %1...").arg(QFileInfo(source).fileName()),
            [&](CancelToken& cancel){
                baked = import::bakeMeshFromLDraw(read, loader, palette);
                if (cancel.requested()) return;
                if (!baked.mesh.tris.empty()) {
                    import::RasterizeOptions ropt;
                    // Author imports at 32 px/stud — 4× the map's
                    // 8 px/stud. The map renderer reads <PixelsPerStud>
                    // from the part XML and scales the pixmap by
                    // (8/32 = 0.25) at draw time, so the brick still
                    // occupies the right number of studs while small
                    // detail (studs, hazard-stripes, embossed prints)
                    // stays sharp at any zoom. marginPx=0 + whole-stud
                    // rounding keep the snap-grid alignment exact.
                    // SSAA=4 multiplied by pxPerStud=32 means we render
                    // internally at 128 px/stud, downsampled smoothly.
                    ropt.pxPerStud   = 32;
                    ropt.marginPx    = 0;
                    ropt.ssaa        = 4;
                    ropt.wireframe   = true;
                    rast = import::rasterizeMeshTopDown(baked.mesh, ropt);
                }
            });
        if (!ok) {
            statusBar()->showMessage(tr("Import cancelled."), 3000);
            return true;
        }
        if (baked.mesh.tris.empty()) {
            QMessageBox::warning(this, kindLabel,
                tr("LDraw library at %1 couldn't resolve any geometry for %2. "
                   "Errors:\n%3").arg(libRoot, source, baked.errors.join('\n')));
            return true;  // we tried and failed; don't fall back
        }
        if (rast.image.isNull()) {
            QMessageBox::warning(this, kindLabel,
                tr("Rasterized sprite is empty."));
            return true;
        }

        const int wStud = std::max(1, static_cast<int>(
            std::round(rast.spriteStuds.width())));
        const int hStud = std::max(1, static_cast<int>(
            std::round(rast.spriteStuds.height())));

        // Snap points: place the same refs the way BlueBrick would (the
        // mesh frame and BlueBrick's LDraw frame agree: y = -LDraw z) and
        // keep the free connection ends of parts BlueBrickParts knows —
        // track, road, monorail — relative to the sprite centre.
        auto placed = import::toBlueBrickMap(read, &parts_, &lib, &loader);
        const QVector<import::ImportedConnection> conns = import::externalConnections(
            *placed, parts_, rast.spriteStuds.center());

        // Show the preview dialog so the user sees what they're
        // about to add to the library. Cancelling here just returns
        // — no part files written, no library scan, nothing to undo.
        ImportPreviewDialog::Stats st;
        st.ldrawResolved = baked.resolvedRefs;
        st.unmapped      = baked.unresolvedRefs;
        ImportPreviewDialog dlg(source, kindLabel, rast.image,
                                wStud, hStud, st, baked.errors, conns, this);
        if (dlg.exec() != QDialog::Accepted) {
            statusBar()->showMessage(tr("Import cancelled."), 3000);
            return true;
        }

        // Emit as a new library part: hi-res sprite + snap points.
        QString modulesRoot = QSettings().value(
            QStringLiteral("modules/libraryPath")).toString();
        if (modulesRoot.isEmpty()) {
            modulesRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                          + QStringLiteral("/imports");
        } else {
            modulesRoot += QStringLiteral("/imports");
        }
        QString err;
        const QString author = mapView_->currentMap()
                                   ? mapView_->currentMap()->author : QString();
        const QString fileKey = import::writeImportedModelAsLibraryPart(
            dlg.partName(), rast.image, wStud, hStud, modulesRoot, author,
            conns, &err);
        if (fileKey.isEmpty()) {
            QMessageBox::warning(this, kindLabel,
                tr("Could not write library part: %1").arg(err));
            return true;
        }
        const QString xmlPath = QDir(modulesRoot).filePath(fileKey + QStringLiteral(".xml"));
        const QString libKey = registerImportedPart(xmlPath);
        if (!libKey.isEmpty()) mapView_->addPartAtViewCenter(libKey);
        statusBar()->showMessage(
            tr("Imported %1 as '%2' — %3 part(s) resolved, %4 unresolved")
                .arg(QFileInfo(source).fileName(), fileKey)
                .arg(baked.resolvedRefs).arg(baked.unresolvedRefs), 8000);
        return true;
    };

    auto* ldrawAct = importMenu->addAction(tr("&LDraw (.ldr / .dat / .mpd)..."));
    connect(ldrawAct, &QAction::triggered, this, [this, importAsPart, importViaLDrawLibrary]{
        const QString in = QFileDialog::getOpenFileName(this,
            tr("Import LDraw file"), {},
            tr("LDraw (*.ldr *.dat *.mpd);;All files (*)"));
        if (in.isEmpty()) return;
        const auto read = import::readLDraw(in);
        if (importViaLDrawLibrary(in, read, tr("LDraw import"))) return;
        importAsPart(in, read, tr("LDraw import"));
    });
    auto* studioAct = importMenu->addAction(tr("&Studio (.io)..."));
    connect(studioAct, &QAction::triggered, this, [this, importAsPart, importViaLDrawLibrary]{
        const QString in = QFileDialog::getOpenFileName(this,
            tr("Import Studio .io"), {},
            tr("Studio (*.io);;All files (*)"));
        if (in.isEmpty()) return;
        const auto read = import::readStudioIo(in);
        if (importViaLDrawLibrary(in, read, tr("Studio import"))) return;
        importAsPart(in, read, tr("Studio import"));
    });
    // LDD imports route through ldraw.xml when an LDD install path is
    // configured: rewrite each LDD designID → LDraw .dat filename and
    // each LDD materialID → LDraw colour code so the model bakes
    // through the same LDraw pipeline as native .ldr imports. Parts
    // without an LDraw mapping are dropped (LDD-only decorations
    // would need .g geometry from db.lif — handled in a follow-up).
    auto importLDDViaLDrawMapping = [this](const QString& source,
                                            import::LDrawReadResult read,
                                            const QString& kindLabel) -> bool {
        if (!read.ok) return false;
        const QString lddRoot = QSettings().value(
            QStringLiteral("import/lddInstallPath")).toString();
        const QString ldrawRoot = QSettings().value(
            QStringLiteral("import/ldrawLibraryPath")).toString();
        if (lddRoot.isEmpty()) return false;

        const QString xmlPath = QDir(lddRoot).absoluteFilePath(QStringLiteral("ldraw.xml"));
        import::LDDLDrawMapping mapping;
        if (!mapping.loadFromFile(xmlPath)) return false;

        // Materials.xml — needed both for the LDD-only fallback's
        // colours and for cross-checking. May live in
        // <lddRoot>/Assets/db/Materials.xml after the user has
        // extracted db.lif manually, or inside db.lif itself which
        // we mount via LifReader.
        import::LDDMaterials materials;
        const QString matPath = QDir(lddRoot).absoluteFilePath(
            QStringLiteral("Assets/db/Materials.xml"));
        if (!materials.loadFromFile(matPath)) {
            // Try inside db.lif at the same logical path.
            const QString dbLifPath = QDir(lddRoot).absoluteFilePath(
                QStringLiteral("Assets/db.lif"));
            import::LifReader lif;
            if (lif.open(dbLifPath)) {
                materials.loadFromBytes(lif.read(QStringLiteral("/db/Materials.xml")));
            }
        }

        // We render every brick from its LDD .g geometry, so the
        // original ref list IS the working set.
        const auto originalRead = read;

        geom::Mesh combined;
        QStringList allErrors;
        import::LDDLDrawBakedModel lddBaked;
        import::RasterizeResult rast;

        // Open db.lif up front (still on UI thread — small map).
        std::unique_ptr<import::LifReader> dbLif;
        const QString dbLifPath = QDir(lddRoot).absoluteFilePath(
            QStringLiteral("Assets/db.lif"));
        if (QFileInfo::exists(dbLifPath)) {
            dbLif = std::make_unique<import::LifReader>();
            if (!dbLif->open(dbLifPath)) dbLif.reset();
        }
        const bool ok = runBackground(this,
            tr("Importing %1...").arg(QFileInfo(source).fileName()),
            [&](CancelToken& cancel){
                // LDD-only render path. The LDraw fallback (which used to
                // resolve ldraw.xml-mapped designIDs through the LDraw
                // .dat library) is intentionally skipped — mixing LDraw-
                // mesh and LDD-mesh outputs in one sprite produced
                // inconsistent stud detail and orientation glitches that
                // are not worth reconciling. Bricks for which LDD has
                // no .g geometry are reported in lddBaked.errors and
                // will simply be missing from the sprite (matches what
                // the lxfml-viewer reference does in the same case).
                import::LDDMeshBuilder lddBuilder;
                lddBuilder.setOnDiskRoot(lddRoot);
                // Pass a null mapping so LDDMeshBuilder doesn't skip any
                // bricks just because they HAVE an LDraw mapping — we
                // want EVERY brick rendered from its .g geometry now.
                lddBuilder.setMaterials(&materials);
                if (dbLif) lddBuilder.setLifReader(dbLif.get());
                lddBaked = lddBuilder.bake(originalRead);
                for (const auto& tri : lddBaked.mesh.tris)  combined.tris.push_back(tri);
                for (const auto& e   : lddBaked.mesh.edges) combined.edges.push_back(e);
                allErrors += lddBaked.errors;
                if (cancel.requested()) return;

                if (!combined.tris.empty()) {
                    import::RasterizeOptions ropt;
                    // Author imports at 32 px/stud — 4× the map's
                    // 8 px/stud. The map renderer reads <PixelsPerStud>
                    // from the part XML and scales the pixmap by
                    // (8/32 = 0.25) at draw time, so the brick still
                    // occupies the right number of studs while small
                    // detail (studs, hazard-stripes, embossed prints)
                    // stays sharp at any zoom. marginPx=0 + whole-stud
                    // rounding keep the snap-grid alignment exact.
                    // SSAA=4 multiplied by pxPerStud=32 means we render
                    // internally at 128 px/stud, downsampled smoothly.
                    ropt.pxPerStud   = 32;
                    ropt.marginPx    = 0;
                    ropt.ssaa        = 4;
                    ropt.wireframe   = true;
                    rast = import::rasterizeMeshTopDown(combined, ropt);
                }
            });
        if (!ok) {
            statusBar()->showMessage(tr("Import cancelled."), 3000);
            return true;
        }

        if (combined.tris.empty()) {
            QMessageBox::warning(this, kindLabel,
                tr("LDD model couldn't be rendered: %1 LDD-rendered, "
                   "%2 skipped (no .g).\n\n%3")
                    .arg(lddBaked.rendered).arg(lddBaked.skipped)
                    .arg(allErrors.join('\n')));
            return true;
        }
        if (rast.image.isNull()) return true;

        // Preview before commit. Same dialog the LDraw path uses.
        ImportPreviewDialog::Stats st;
        st.ldrawResolved = 0;
        st.lddRendered   = lddBaked.rendered;
        st.translated    = 0;
        st.unmapped      = 0;
        st.skipped       = lddBaked.skipped;
        ImportPreviewDialog dlg(source, kindLabel, rast.image,
                                static_cast<int>(std::round(rast.spriteStuds.width())),
                                static_cast<int>(std::round(rast.spriteStuds.height())),
                                st, allErrors, {}, this);
        if (dlg.exec() != QDialog::Accepted) {
            statusBar()->showMessage(tr("Import cancelled."), 3000);
            return true;
        }

        const int wStud = std::max(1, static_cast<int>(
            std::round(rast.spriteStuds.width())));
        const int hStud = std::max(1, static_cast<int>(
            std::round(rast.spriteStuds.height())));
        QString modulesRoot = QSettings().value(
            QStringLiteral("modules/libraryPath")).toString();
        if (modulesRoot.isEmpty()) {
            modulesRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                          + QStringLiteral("/imports");
        } else {
            modulesRoot += QStringLiteral("/imports");
        }
        QString err;
        const QString author = mapView_->currentMap()
                                   ? mapView_->currentMap()->author : QString();
        const QString fileKey = import::writeImportedModelAsLibraryPart(
            dlg.partName(), rast.image, wStud, hStud, modulesRoot, author,
            {}, &err);
        if (fileKey.isEmpty()) {
            QMessageBox::warning(this, kindLabel,
                tr("Could not write library part: %1").arg(err));
            return true;
        }
        const QString partXml = QDir(modulesRoot).filePath(fileKey + QStringLiteral(".xml"));
        const QString libKey = registerImportedPart(partXml);
        if (!libKey.isEmpty()) mapView_->addPartAtViewCenter(libKey);
        statusBar()->showMessage(
            tr("Imported %1 as '%2' — %3 LDD-rendered (%4 skipped)")
                .arg(QFileInfo(source).fileName(), fileKey)
                .arg(lddBaked.rendered).arg(lddBaked.skipped), 10000);
        return true;
    };

    auto* lddAct = importMenu->addAction(tr("L&DD (.lxf / .lxfml)..."));
    connect(lddAct, &QAction::triggered, this, [this, importAsPart, importLDDViaLDrawMapping]{
        const QString in = QFileDialog::getOpenFileName(this,
            tr("Import LDD file"), {},
            tr("LDD (*.lxf *.lxfml);;All files (*)"));
        if (in.isEmpty()) return;
        const auto read = import::readLDD(in);
        if (importLDDViaLDrawMapping(in, read, tr("LDD import"))) return;
        importAsPart(in, read, tr("LDD import"));
    });

    auto* partListAct = tools->addAction(tr("Export &Part List (CSV)..."));
    connect(partListAct, &QAction::triggered, this, &MainWindow::onExportPartList);

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
