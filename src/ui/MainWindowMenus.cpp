// Menu-bar setup pulled out of MainWindow.cpp so the latter can focus on
// construction, docks, file I/O, and high-level coordination. One class,
// multiple translation units — MOC only needs the header.

#include "MainWindow.h"
#include "UpdateCheck.h"

#include "FindDialog.h"
#include "LayerPanel.h"
#include "MapView.h"
#include "ModuleLibraryPanel.h"
#include "VenueLibraryPanel.h"
#include "ViewsPanel.h"
#include "ModulesPanel.h"
#include "PartsBrowser.h"
#include "PartUsagePanel.h"
#include "PreferencesDialog.h"
#include "SavedViews.h"
#include "VenueDialog.h"
#include "VenueDimensionsDialog.h"
#include "help/HelpButton.h"
#include "help/HelpPages.h"
#include "help/ShortcutsDialog.h"
#include "tours/Tours.h"
#include "theme/AppPrefs.h"
#include "theme/PanelHeader.h"
#include "../core/AnchoredLabel.h"
#include "../core/ColorSpec.h"
#include "../core/Ids.h"
#include "../core/Map.h"
#include "../edit/EditCommands.h"
#include "../edit/LabelCommands.h"
#include "../edit/LayerCommands.h"
#include "../edit/VenueCommands.h"
#include "../import/ldraw/LDrawReader.h"
#include "../import/studio/StudioReader.h"
#include "../import/ldd/LDDReader.h"
#include "../import/ImportToPart.h"
#include "../edit/Connectivity.h"
#include "../core/LayerBrick.h"
#include "../parts/PartsLibrary.h"
#include "../rendering/SceneBuilder.h"
#include "../saveload/VenueIO.h"

#include <QAction>
#include <QKeySequence>
#include <QCoreApplication>
#include <QUrl>
#include <QDesktopServices>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QPageLayout>
#include <QPageSize>
#include <QPrintDialog>
#include <QPrinter>
#include <QLineEdit>
#include <QMenu>
#include <QTimer>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QUndoStack>

namespace bld::ui {

void MainWindow::setupMenus() {
    auto* file = menuBar()->addMenu(tr("&File"));
    auto* newAct = file->addAction(tr("&New"));
    newAct->setShortcut(QKeySequence::New);
    connect(newAct, &QAction::triggered, this, &MainWindow::onNew);

    auto* openAct = file->addAction(tr("&Open..."));
    openAct->setObjectName(QStringLiteral("file.open"));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::onOpen);

    recentMenu_ = file->addMenu(tr("Open &Recent"));
    rebuildRecentMenu();
    setupLiveMenu(file);

    file->addSeparator();
    auto* saveAct = file->addAction(tr("&Save"));
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, &MainWindow::onSave);

    auto* saveAsAct = file->addAction(tr("Save &As..."));
    saveAsAct->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAct, &QAction::triggered, this, &MainWindow::onSaveAs);

    file->addSeparator();
    auto* exportBbmAct = file->addAction(tr("Export as &BlueBrick Map (.bbm)..."));
    exportBbmAct->setToolTip(tr("A copy BlueBrick can open, with what BlueBrick supports"));
    connect(exportBbmAct, &QAction::triggered, this, &MainWindow::onExportBbm);
    auto* exportAct = file->addAction(tr("Export as &Image..."));
    exportImageAct_ = exportAct;
    connect(exportAct, &QAction::triggered, this, [this]{
        if (!mapView_->currentMap()) return;
        auto* scene = mapView_->scene();
        const QRectF bounds = scene->itemsBoundingRect().adjusted(-20, -20, 20, 20);
        if (bounds.isEmpty()) {
            QMessageBox::information(this, tr("Export"), tr("The map is empty."));
            return;
        }
        // ExportImageForm.cs parity: width, keep-aspect/height, watermark,
        // background fill, transparent background, antialias, path. Each
        // setting is persisted under export/* so the next export remembers
        // the user's preferences.
        QSettings s;
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Export image"));
        auto* form = new QFormLayout(&dlg);
        form->addRow(help::headingWithHelp(tr("Export image"), QStringLiteral("dialog.exportImage"), &dlg));
        auto* pathEdit = new QLineEdit(
            currentFilePath_.isEmpty()
                ? s.value(QStringLiteral("export/path")).toString()
                : QFileInfo(currentFilePath_).baseName() + QStringLiteral(".png"), &dlg);
        auto* browseBtn = new QPushButton(tr("..."), &dlg);
        auto* pathRow = new QHBoxLayout(); pathRow->addWidget(pathEdit); pathRow->addWidget(browseBtn);
        auto* pathWrap = new QWidget(&dlg); pathWrap->setLayout(pathRow);
        form->addRow(tr("Output file:"), pathWrap);
        connect(browseBtn, &QPushButton::clicked, &dlg, [pathEdit, &dlg]{
            const QString p = QFileDialog::getSaveFileName(&dlg, tr("Export map as image"),
                pathEdit->text(), tr("PNG (*.png);;JPEG (*.jpg *.jpeg)"));
            if (!p.isEmpty()) pathEdit->setText(p);
        });

        auto* widthSpin = new QSpinBox(&dlg);
        widthSpin->setRange(128, 16384);
        widthSpin->setValue(s.value(QStringLiteral("export/width"), 1600).toInt());
        form->addRow(tr("Width (px):"), widthSpin);
        auto* keepAspect = new QCheckBox(tr("Keep aspect ratio (height auto)"), &dlg);
        keepAspect->setChecked(s.value(QStringLiteral("export/keepAspect"), true).toBool());
        form->addRow(keepAspect);
        auto* heightSpin = new QSpinBox(&dlg);
        heightSpin->setRange(64, 16384);
        heightSpin->setValue(s.value(QStringLiteral("export/height"), 1200).toInt());
        heightSpin->setEnabled(!keepAspect->isChecked());
        connect(keepAspect, &QCheckBox::toggled, heightSpin, [heightSpin](bool on){
            heightSpin->setEnabled(!on);
        });
        form->addRow(tr("Height (px):"), heightSpin);

        auto* watermarkChk = new QCheckBox(tr("Embed general-info watermark"), &dlg);
        watermarkChk->setChecked(s.value(QStringLiteral("export/watermark"), false).toBool());
        form->addRow(watermarkChk);
        auto* transparentChk = new QCheckBox(tr("Transparent background"), &dlg);
        transparentChk->setChecked(s.value(QStringLiteral("export/transparent"), false).toBool());
        form->addRow(transparentChk);
        auto* antialiasChk = new QCheckBox(tr("Antialias"), &dlg);
        antialiasChk->setChecked(s.value(QStringLiteral("export/antialias"), true).toBool());
        form->addRow(antialiasChk);
        // ExportImageForm's "Electric circuits": remembered in the map
        // (<ExportElectricCircuit>), whatever the view shows now.
        auto* electricChk = new QCheckBox(tr("Electric circuits"), &dlg);
        electricChk->setChecked(mapView_->currentMap()->exportInfo.electricCircuit);
        form->addRow(electricChk);

        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;
        const QString path = pathEdit->text();
        if (path.isEmpty()) return;

        const int width  = widthSpin->value();
        const int height = keepAspect->isChecked()
            ? std::max(64, static_cast<int>(width * (bounds.height() / bounds.width())))
            : heightSpin->value();
        views::SceneImageOptions opts;
        opts.background = mapView_->currentMap()->backgroundColor.color;
        opts.transparent = transparentChk->isChecked();
        opts.antialias = antialiasChk->isChecked();
        opts.aspect = keepAspect->isChecked() ? Qt::KeepAspectRatio : Qt::IgnoreAspectRatio;
        if (watermarkChk->isChecked()) {
            const auto* m = mapView_->currentMap();
            opts.watermark = tr("%1 / %2 / %3").arg(m->author, m->lug, m->event);
        }
        mapView_->currentMap()->exportInfo.electricCircuit = electricChk->isChecked();
        const QString electricKey = QStringLiteral("view/electricCircuits");
        const bool shownElectric = s.value(electricKey, false).toBool();
        const bool swapElectric = shownElectric != electricChk->isChecked();
        if (swapElectric) {
            s.setValue(electricKey, electricChk->isChecked());
            mapView_->rebuildScene();
        }
        const QImage img = views::renderSceneImage(*scene, bounds, QSize(width, height), opts);
        if (swapElectric) {
            s.setValue(electricKey, shownElectric);
            mapView_->rebuildScene();
        }
        if (!img.save(path)) {
            QMessageBox::warning(this, tr("Export failed"), tr("Could not write %1").arg(path));
            return;
        }
        s.setValue(QStringLiteral("export/path"),        path);
        s.setValue(QStringLiteral("export/width"),       width);
        s.setValue(QStringLiteral("export/height"),      height);
        s.setValue(QStringLiteral("export/keepAspect"),  keepAspect->isChecked());
        s.setValue(QStringLiteral("export/watermark"),   watermarkChk->isChecked());
        s.setValue(QStringLiteral("export/transparent"), transparentChk->isChecked());
        s.setValue(QStringLiteral("export/antialias"),   antialiasChk->isChecked());
        statusBar()->showMessage(tr("Exported %1x%2 to %3").arg(width).arg(height).arg(path), 5000);
    });

    // A picture of the layout or a saved view, with no questions; the
    // detailed Export as Image above is its "More options…".
    auto* shareAct = file->addAction(tr("Share &Picture…"));
    shareAct->setObjectName(QStringLiteral("action.sharePicture"));
    shareAct->setToolTip(tr("Save or copy a picture of the layout, what's on screen or a saved view"));
    connect(shareAct, &QAction::triggered, this, [this] { openSharePicture(); });
    auto* exportViewsAct = file->addAction(tr("Export All &Views…"));
    exportViewsAct->setObjectName(QStringLiteral("action.exportAllViews"));
    exportViewsAct->setToolTip(tr("One picture of each saved view, in a folder"));
    connect(exportViewsAct, &QAction::triggered, this, [this] { exportAllViews(false); });

    auto* pdfAct = file->addAction(tr("Export as P&DF..."));
    connect(pdfAct, &QAction::triggered, this, [this]{
        if (!mapView_->currentMap()) return;
        auto* scene = mapView_->scene();
        const QRectF bounds = scene->itemsBoundingRect().adjusted(-20, -20, 20, 20);
        if (bounds.isEmpty()) {
            QMessageBox::information(this, tr("Export"), tr("The map is empty."));
            return;
        }
        const QString suggested = currentFilePath_.isEmpty()
            ? QStringLiteral("layout.pdf")
            : QFileInfo(currentFilePath_).baseName() + QStringLiteral(".pdf");
        const QString path = QFileDialog::getSaveFileName(this,
            tr("Export map as PDF"), suggested, tr("PDF (*.pdf)"));
        if (path.isEmpty()) return;

        // A3 at 300dpi is plenty for typical BlueBrick layouts; the
        // QPrinter::HighResolution preset gives us 1200dpi though which
        // keeps sprites crisp when users scale the PDF up further.
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(path);
        // Fit the layout to a sensible page. Portrait vs landscape is
        // chosen to match the layout's aspect ratio so content fills
        // the page rather than leaving big margins.
        QPageLayout layout(
            QPageSize(QPageSize::A3),
            bounds.width() >= bounds.height()
                ? QPageLayout::Landscape
                : QPageLayout::Portrait,
            QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter);
        printer.setPageLayout(layout);

        QPainter p(&printer);
        if (!p.isActive()) {
            QMessageBox::warning(this, tr("Export failed"),
                tr("Could not open the PDF for writing."));
            return;
        }
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        scene->render(&p, QRectF(printer.pageLayout().paintRectPixels(printer.resolution())),
                      bounds, Qt::KeepAspectRatio);
        p.end();
        statusBar()->showMessage(tr("Exported PDF to %1").arg(path), 5000);
    });

    auto* printAct = file->addAction(tr("&Print..."));
    printAct->setShortcut(QKeySequence::Print);
    connect(printAct, &QAction::triggered, this, [this]{
        if (!mapView_->currentMap()) return;
        auto* scene = mapView_->scene();
        const QRectF bounds = scene->itemsBoundingRect().adjusted(-20, -20, 20, 20);
        if (bounds.isEmpty()) {
            QMessageBox::information(this, tr("Print"), tr("The map is empty."));
            return;
        }
        // Standard print pipeline: open the system print dialog so the
        // user can pick destination, paper size, copies, and margins.
        // Then tile the scene across pages so train-club layouts that
        // are wider than a single sheet print as a paste-up — vanilla
        // BlueBrick's print path does the same.
        QPrinter printer(QPrinter::HighResolution);
        QPrintDialog dlg(&printer, this);
        dlg.setWindowTitle(tr("Print layout"));
        if (dlg.exec() != QDialog::Accepted) return;

        QPainter p(&printer);
        if (!p.isActive()) {
            QMessageBox::warning(this, tr("Print failed"),
                tr("Could not start the print job."));
            return;
        }
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        // Page (paint) rect in printer device pixels.
        const QRectF pagePx = printer.pageLayout().paintRectPixels(printer.resolution());
        // Pick a scale that prints "actual size" — 1 stud = 8 mm in
        // BlueBrick convention. Use the device's resolution to convert
        // mm into device pixels, then 1 stud = 8 * (px per mm).
        const double dpmm = printer.resolution() / 25.4;
        const double pxPerStud = 8.0 * dpmm;
        const double sceneStudW = bounds.width()  / rendering::SceneBuilder::kPixelsPerStud;
        const double sceneStudH = bounds.height() / rendering::SceneBuilder::kPixelsPerStud;
        const double tileStudW = pagePx.width()  / pxPerStud;
        const double tileStudH = pagePx.height() / pxPerStud;
        const int cols = std::max(1, static_cast<int>(std::ceil(sceneStudW / tileStudW)));
        const int rows = std::max(1, static_cast<int>(std::ceil(sceneStudH / tileStudH)));

        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                if (!(row == 0 && col == 0)) printer.newPage();
                const QRectF sourceStuds(
                    bounds.x() + col * tileStudW * rendering::SceneBuilder::kPixelsPerStud,
                    bounds.y() + row * tileStudH * rendering::SceneBuilder::kPixelsPerStud,
                    tileStudW * rendering::SceneBuilder::kPixelsPerStud,
                    tileStudH * rendering::SceneBuilder::kPixelsPerStud);
                scene->render(&p, pagePx, sourceStuds, Qt::KeepAspectRatio);
            }
        }
        p.end();
        statusBar()->showMessage(
            tr("Printed %1 page(s)").arg(rows * cols), 5000);
    });

    file->addSeparator();
    auto* quit = file->addAction(tr("&Quit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QMainWindow::close);

    // Edit menu — order mirrors BlueBrick MainForm.Designer.cs:
    //   Undo / Redo
    //   Cut / Copy / Paste / Duplicate / Delete
    //   Find & Replace
    //   Select All / Deselect All / Select Path / Group▸ (Group, Ungroup)
    //   Transform▸ (Move Step▸ / Send Back / Bring Front / -- /
    //               Rotation Step▸ / Rotate CW / Rotate CCW)
    //   Insert▸    (BLD-specific: Add Text, Add Anchored Label)
    //   Preferences
    auto* edit = menuBar()->addMenu(tr("&Edit"));
    undoAct_ = mapView_->undoStack()->createUndoAction(this, tr("&Undo"));
    undoAct_->setShortcut(QKeySequence::Undo);
    edit->addAction(undoAct_);
    redoAct_ = mapView_->undoStack()->createRedoAction(this, tr("&Redo"));
    redoAct_->setShortcut(QKeySequence::Redo);
    edit->addAction(redoAct_);
    // Shown instead of the two above while a live layout is open.
    edit->addAction(liveUndoAct_);
    edit->addAction(liveRedoAct_);

    edit->addSeparator();
    auto* cutAct = edit->addAction(tr("Cu&t"));
    cutAct->setShortcut(QKeySequence::Cut);
    connect(cutAct, &QAction::triggered, [this]{ mapView_->cutSelection(); });
    auto* copyAct = edit->addAction(tr("&Copy"));
    copyAct->setShortcut(QKeySequence::Copy);
    connect(copyAct, &QAction::triggered, [this]{ mapView_->copySelection(); });
    auto* pasteAct = edit->addAction(tr("&Paste"));
    pasteAct->setShortcut(QKeySequence::Paste);
    connect(pasteAct, &QAction::triggered, [this]{ mapView_->pasteClipboard(); });
    auto* dupAct = edit->addAction(tr("&Duplicate"));
    dupAct->setShortcut(QKeySequence(tr("Ctrl+D")));
    connect(dupAct, &QAction::triggered, [this]{ mapView_->duplicateSelection(); });
    auto* del = edit->addAction(tr("De&lete"));
    del->setShortcut(Qt::Key_Delete);
    connect(del, &QAction::triggered, [this]{ mapView_->deleteSelected(); });

    edit->addSeparator();
    auto* findAct = edit->addAction(tr("&Find && Replace..."));
    findAct->setShortcut(QKeySequence::Find);
    connect(findAct, &QAction::triggered, this, [this]{
        auto* dlg = new FindDialog(*mapView_, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    });

    edit->addSeparator();
    auto* selAllAct = edit->addAction(tr("Select &All"));
    selAllAct->setShortcut(QKeySequence::SelectAll);
    connect(selAllAct, &QAction::triggered, [this]{ mapView_->selectAll(); });
    auto* selNoneAct = edit->addAction(tr("Deselect &All"));
    selNoneAct->setShortcut(QKeySequence(tr("Ctrl+Shift+A")));
    connect(selNoneAct, &QAction::triggered, [this]{ mapView_->deselectAll(); });
    auto* selPathAct = edit->addAction(tr("Select &Path"));
    selPathAct->setShortcut(QKeySequence(tr("Ctrl+P")));
    selPathAct->setToolTip(tr("Extend selection to every brick connected to current selection"));
    connect(selPathAct, &QAction::triggered, [this]{ mapView_->selectPath(); });

    auto* groupSub = edit->addMenu(tr("&Group"));
    auto* groupAct = groupSub->addAction(tr("&Group"));
    groupAct->setShortcut(QKeySequence(tr("Ctrl+G")));
    connect(groupAct, &QAction::triggered, [this]{ mapView_->groupSelection(); });
    auto* ungroupAct = groupSub->addAction(tr("&Ungroup"));
    ungroupAct->setShortcut(QKeySequence(tr("Ctrl+Shift+G")));
    connect(ungroupAct, &QAction::triggered, [this]{ mapView_->ungroupSelection(); });

    edit->addSeparator();
    // Transform submenu (BlueBrick parity).
    auto* transformMenu = edit->addMenu(tr("&Transform"));
    auto* moveStepMenu = transformMenu->addMenu(tr("&Move Step"));
    auto addNudge = [this, moveStepMenu](const QString& label, double dx, double dy){
        auto* a = moveStepMenu->addAction(label);
        connect(a, &QAction::triggered, this, [this, dx, dy]{
            mapView_->nudgeSelected(dx, dy);
        });
    };
    addNudge(tr("&Up"),    0.0, -1.0);
    addNudge(tr("&Down"),  0.0,  1.0);
    addNudge(tr("&Left"), -1.0,  0.0);
    addNudge(tr("&Right"), 1.0,  0.0);
    auto* toBackAct = transformMenu->addAction(tr("Send to &Back"));
    toBackAct->setShortcut(QKeySequence(tr("Ctrl+Shift+[")));
    connect(toBackAct, &QAction::triggered, [this]{ mapView_->sendSelectionToBack(); });
    auto* toFrontAct = transformMenu->addAction(tr("Bring to &Front"));
    toFrontAct->setShortcut(QKeySequence(tr("Ctrl+Shift+]")));
    connect(toFrontAct, &QAction::triggered, [this]{ mapView_->bringSelectionToFront(); });
    transformMenu->addSeparator();
    // Rotation step picker — duplicates toolbar dropdown so it's reachable
    // without a mouse. Each entry sets the current rotation step.
    auto* rotStepMenu = transformMenu->addMenu(tr("Rotation &Step"));
    const std::vector<std::pair<QString, double>> rotStepOpts = {
        { tr("90°"),    90.0 },
        { tr("45°"),    45.0 },
        { tr("22.5°"),  22.5 },
        { tr("11.25°"), 11.25 },
        { tr("5°"),     5.0 },
        { tr("1°"),     1.0 },
    };
    for (const auto& o : rotStepOpts) {
        auto* a = rotStepMenu->addAction(o.first);
        connect(a, &QAction::triggered, this, [this, val = o.second]{
            mapView_->setRotationStepDegrees(val);
            QSettings s; s.beginGroup(QStringLiteral("editing"));
            s.setValue(QStringLiteral("rotationStepDegrees"), val); s.endGroup();
        });
    }
    auto* rotCW = transformMenu->addAction(tr("Rotate C&W"));
    rotCW->setShortcut(QKeySequence(tr("Shift+R")));
    connect(rotCW, &QAction::triggered, [this]{
        mapView_->rotateSelected(static_cast<float>(mapView_->rotationStepDegrees()));
    });
    auto* rotCCW = transformMenu->addAction(tr("Rotate &CCW"));
    rotCCW->setShortcut(Qt::Key_R);
    connect(rotCCW, &QAction::triggered, [this]{
        mapView_->rotateSelected(static_cast<float>(-mapView_->rotationStepDegrees()));
    });

    // Insert submenu — BLD additions for items BlueBrick doesn't have.
    auto* insertMenu = edit->addMenu(tr("&Insert"));
    auto* addTextAct = insertMenu->addAction(tr("&Text..."));
    addTextAct->setShortcut(QKeySequence(tr("Ctrl+T")));
    connect(addTextAct, &QAction::triggered, this, [this]{
        if (!mapView_->currentMap()) return;
        bool ok = false;
        const QString text = QInputDialog::getText(
            this, tr("Add text"), tr("Label text:"),
            QLineEdit::Normal, {}, &ok);
        if (ok && !text.isEmpty()) mapView_->addTextAtViewCenter(text);
    });
    auto* addLabel = insertMenu->addAction(tr("&Anchored Label..."));
    addLabel->setShortcut(QKeySequence(tr("Ctrl+L")));
    connect(addLabel, &QAction::triggered, this, [this]{
        auto* map = mapView_->currentMap();
        if (!map) return;
        QString targetId;
        core::AnchorKind kind = core::AnchorKind::World;
        QPointF offsetStuds;
        auto sel = mapView_->scene()->selectedItems();
        if (sel.size() == 1 && sel[0]->data(2).toString() == QStringLiteral("brick")) {
            targetId = sel[0]->data(1).toString();
            kind = core::AnchorKind::Brick;
            offsetStuds = QPointF(2.0, -2.0);
        } else {
            const QPointF scenePos = mapView_->mapToScene(mapView_->viewport()->rect().center());
            offsetStuds = mapView_->gridPoint(QPointF(scenePos.x() / 8.0, scenePos.y() / 8.0));
        }
        bool ok = false;
        const QString text = QInputDialog::getText(
            this, tr("Anchored label"), tr("Label text:"),
            QLineEdit::Normal, {}, &ok);
        if (!ok || text.isEmpty()) return;

        core::AnchoredLabel L;
        L.id = core::newBbmId();
        L.text = text;
        L.color = core::ColorSpec::fromKnown(QColor(Qt::black), QStringLiteral("Black"));
        L.kind = kind;
        L.targetId = targetId;
        L.offset = offsetStuds;
        mapView_->undoStack()->push(new edit::AddAnchoredLabelCommand(*map, std::move(L)));
    });

    edit->addSeparator();
    auto* settingsAct = edit->addAction(tr("&Settings..."));
    settingsAct->setObjectName(QStringLiteral("action.settings"));
    settingsAct->setShortcut(QKeySequence::Preferences);
    settingsAct->setMenuRole(QAction::PreferencesRole);
    settingsAct->setToolTip(tr("Light or dark, color, bigger text, Expert mode and help buttons"));
    connect(settingsAct, &QAction::triggered, this, &MainWindow::openSettings);
    auto* prefsAct = edit->addAction(tr("&Preferences..."));
    prefsAct->setMenuRole(QAction::NoRole);
    preferencesAct_ = prefsAct;
    connect(prefsAct, &QAction::triggered, this, [this]{
        PreferencesDialog dlg(this);
        dlg.exec();
        QSettings s; s.beginGroup(QStringLiteral("editing"));
        mapView_->setSnapStepStuds(s.value(QStringLiteral("snapStepStuds"), 0.0).toDouble());
        mapView_->setRotationStepDegrees(s.value(QStringLiteral("rotationStepDegrees"), 90.0).toDouble());
        s.endGroup();
        const QString libDir = QSettings().value(QStringLiteral("modules/libraryPath")).toString();
        if (!libDir.isEmpty() && libDir != moduleLibraryPanel_->libraryPath()) {
            moduleLibraryPanel_->setLibraryPath(libDir);
        }
        mapView_->rebuildScene();
    });

    auto* view = menuBar()->addMenu(tr("&View"));
    auto* zIn = view->addAction(tr("Zoom &In"));
    zIn->setShortcut(QKeySequence::ZoomIn);
    connect(zIn, &QAction::triggered, this, &MainWindow::onZoomIn);

    auto* zOut = view->addAction(tr("Zoom &Out"));
    zOut->setShortcut(QKeySequence::ZoomOut);
    connect(zOut, &QAction::triggered, this, &MainWindow::onZoomOut);

    auto* fit = view->addAction(tr("&Fit to View"));
    fit->setShortcut(QKeySequence(Qt::Key_F));
    connect(fit, &QAction::triggered, this, &MainWindow::onFitToView);

    view->addSeparator();
    // The web's Panels menu: tick a panel to show it (a hidden one comes
    // back on the right side), untick to hide it.
    panelsMenu_ = view->addMenu(tr("&Panels"));
    panelsMenu_->setObjectName(QStringLiteral("PanelsMenu"));
    for (QDockWidget* d : panelDocks()) {
        auto* act = panelsMenu_->addAction(d->windowTitle());
        act->setObjectName(QStringLiteral("panels.") + d->objectName());
        act->setCheckable(true);
        act->setChecked(!d->isHidden());
        connect(d, &QDockWidget::windowTitleChanged, act, &QAction::setText);
        // Shown or hidden once the menu has closed.
        connect(act, &QAction::triggered, d, [d](bool on) {
            QTimer::singleShot(0, d, [d, on] { theme::PanelHeader::setShown(d, on); });
        });
    }
    // Ticks as they are now.
    connect(panelsMenu_, &QMenu::aboutToShow, this, [this] {
        const QList<QDockWidget*> docks = panelDocks();
        const QList<QAction*> acts = panelsMenu_->actions();
        for (qsizetype i = 0; i < docks.size() && i < acts.size(); ++i)
            acts[i]->setChecked(!docks[i]->isHidden());
    });

    view->addSeparator();
    auto* statusToggle = view->addAction(tr("&Status Bar"));
    statusToggle->setCheckable(true);
    statusToggle->setChecked(true);
    connect(statusToggle, &QAction::toggled, statusBar(), &QStatusBar::setVisible);

    auto* scrollToggle = view->addAction(tr("Map &Scroll Bars"));
    scrollToggle->setCheckable(true);
    // Off by default — middle-button pan is the primary navigation, and
    // visible scrollbars steal wheel events. User can flip them back on
    // per-session if they want the affordance.
    scrollToggle->setChecked(false);
    connect(scrollToggle, &QAction::toggled, this, [this](bool on){
        const Qt::ScrollBarPolicy p = on ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff;
        mapView_->setHorizontalScrollBarPolicy(p);
        mapView_->setVerticalScrollBarPolicy(p);
    });

    view->addSeparator();
    auto addRenderToggle = [this, view](const QString& label, const QString& settingsKey, bool defaultOn){
        auto* act = view->addAction(label);
        act->setCheckable(true);
        QSettings s;
        act->setChecked(s.value(settingsKey, defaultOn).toBool());
        connect(act, &QAction::toggled, this, [this, settingsKey](bool on){
            QSettings().setValue(settingsKey, on);
            mapView_->rebuildScene();
        });
    };
    addRenderToggle(tr("&Electric Circuits"),      QStringLiteral("view/electricCircuits"),    false);
    addRenderToggle(tr("Connection &Points"),      QStringLiteral("view/connectionPoints"),    false);
    addRenderToggle(tr("Ruler Attach P&oints"),     QStringLiteral("view/rulerAttachPoints"),   false);
    addRenderToggle(tr("&Watermark"),               QStringLiteral("view/watermark"),           true);
    addRenderToggle(tr("Brick &Hulls"),             QStringLiteral("view/brickHulls"),          false);
    addRenderToggle(tr("Brick E&levation Labels"),  QStringLiteral("view/brickElevation"),      false);
    addRenderToggle(tr("&Module Names"),             QStringLiteral("view/moduleNames"),         true);

    // Tools menu (libraries + Import submenu + preferences) lives in
    // MainWindowToolsMenu.cpp — it's ~180 lines on its own.
    setupToolsMenu();

    // Map menu (background + info + the venue sub-menu) lives in
    // MainWindowMapMenu.cpp — it's ~240 lines on its own.
    setupMapMenu();

    setupBudgetMenu();

    auto* modules = menuBar()->addMenu(tr("&Modules"));
    auto* createModAct = modules->addAction(tr("&Make a Module..."));
    createModAct->setObjectName(QStringLiteral("makeModule"));
    createModAct->setToolTip(tr("Make the picked parts one module in this layout. To use it in other layouts, "
                                "choose Save to Module library... from its menu."));
    connect(createModAct, &QAction::triggered, this, &MainWindow::onMakeModule);
    auto* importModAct = modules->addAction(tr("&Import .bbm as Module..."));
    connect(importModAct, &QAction::triggered, this, &MainWindow::onImportBbmAsModule);
    auto* saveSetAct = modules->addAction(tr("Save Selection as &Set..."));
    saveSetAct->setToolTip(tr("Export the current brick selection as a "
                              "BrickTracks-style .set.xml drop-in for the parts library"));
    connect(saveSetAct, &QAction::triggered, this, &MainWindow::onSaveSelectionAsSet);

    auto* help = menuBar()->addMenu(tr("&Help"));
    helpMenu_ = help;
    auto* startAct = help->addAction(tr("&Getting Started"));
    startAct->setObjectName(QStringLiteral("help.gettingStarted"));
    connect(startAct, &QAction::triggered, this, [] {
        // The short page, then the manual, then the manual online.
        for (const QString& page : { QString::fromLatin1(help::kGettingStartedPage), QStringLiteral("index.html") }) {
            const QString path = help::helpPagePath(page);
            if (!path.isEmpty()) {
                help::openHelpPage(QUrl::fromLocalFile(path));
                return;
            }
        }
        help::openHelpPage(QUrl(QStringLiteral(
            "https://github.com/brick-layout-designer/brick-layout-designer/tree/main/help/en")));
    });
    // The guided tours (the same ones, and the same words, as the web app).
    for (const tours::Tour* t : tours::desktopTours()) {
        auto* a = help->addAction(tr("Tour: %1").arg(t->title));
        a->setObjectName(QStringLiteral("help.tour.") + t->id);
        connect(a, &QAction::triggered, this, [this, id = t->id] { startTourNamed(id); });
    }
    auto* contentsAct = help->addAction(tr("&Contents"));
    helpContentsAct_ = contentsAct;
    contentsAct->setShortcut(QKeySequence::HelpContents);
    connect(contentsAct, &QAction::triggered, this, [] {
        // BlueBrick's manual as offline HTML, in the UI language if there
        // is one, else English; the online copy if it wasn't installed.
        const QString index = help::helpPagePath(QStringLiteral("index.html"));
        help::openHelpPage(index.isEmpty() ? QUrl(QStringLiteral(
            "https://github.com/brick-layout-designer/brick-layout-designer/tree/main/help/en"))
                                           : QUrl::fromLocalFile(index));
    });
    auto* keysAct = help->addAction(tr("&Keyboard Shortcuts"));
    keysAct->setObjectName(QStringLiteral("help.shortcuts"));
    connect(keysAct, &QAction::triggered, this, [this] {
        help::ShortcutsDialog dialog(help::collectShortcuts(menuBar()), this);
        dialog.exec();
    });
    // "Turn help buttons off" / "on", following the setting wherever it changes.
    auto* toggleHelpAct = help->addAction(QString());
    toggleHelpAct->setObjectName(QStringLiteral("help.toggleButtons"));
    const auto labelToggle = [toggleHelpAct] {
        toggleHelpAct->setText(theme::PrefsStore::instance().prefs().helpIcons ? tr("Turn Help Buttons &Off")
                                                                               : tr("Turn Help Buttons &On"));
    };
    labelToggle();
    connect(&theme::PrefsStore::instance(), &theme::PrefsStore::changed, toggleHelpAct, labelToggle);
    connect(toggleHelpAct, &QAction::triggered, this, [] {
        auto& store = theme::PrefsStore::instance();
        theme::AppPrefs p = store.prefs();
        p.helpIcons = !p.helpIcons;
        store.update(p);
    });
    help->addSeparator();
    auto* updateAct = help->addAction(tr("Check for &Updates..."));
    connect(updateAct, &QAction::triggered, this, [this]{ updates_->checkNow(); });
    help->addSeparator();
    auto* aboutAct = help->addAction(tr("&About BLD..."));
    connect(aboutAct, &QAction::triggered, this, &MainWindow::onAbout);
    auto* aboutQtAct = help->addAction(tr("About &Qt..."));
    connect(aboutQtAct, &QAction::triggered, qApp, &QApplication::aboutQt);
}

}  // namespace bld::ui
