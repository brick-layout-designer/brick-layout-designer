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
#include "ModulesPanel.h"
#include "PartsBrowser.h"
#include "PartUsagePanel.h"
#include "PreferencesDialog.h"
#include "VenueDialog.h"
#include "VenueDimensionsDialog.h"
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
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::onOpen);

    recentMenu_ = file->addMenu(tr("Open &Recent"));
    rebuildRecentMenu();

    file->addSeparator();
    auto* saveAct = file->addAction(tr("&Save"));
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, &MainWindow::onSave);

    auto* saveAsAct = file->addAction(tr("Save &As..."));
    saveAsAct->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAct, &QAction::triggered, this, &MainWindow::onSaveAs);

    file->addSeparator();
    auto* exportAct = file->addAction(tr("Export as &Image..."));
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
        QImage img(width, height,
                   transparentChk->isChecked() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
        img.fill(transparentChk->isChecked()
                     ? Qt::transparent
                     : mapView_->currentMap()->backgroundColor.color);
        {
            QPainter p(&img);
            if (antialiasChk->isChecked()) {
                p.setRenderHint(QPainter::Antialiasing);
                p.setRenderHint(QPainter::SmoothPixmapTransform);
            }
            scene->render(&p, QRectF(0, 0, width, height), bounds,
                          keepAspect->isChecked() ? Qt::KeepAspectRatio : Qt::IgnoreAspectRatio);
            if (watermarkChk->isChecked()) {
                const auto* m = mapView_->currentMap();
                const QString stamp = tr("%1 / %2 / %3").arg(m->author, m->lug, m->event);
                QFont f; f.setPointSize(std::max(8, height / 60));
                p.setFont(f);
                p.setPen(QColor(0, 0, 0, 140));
                p.drawText(QRectF(0, 0, width, height).adjusted(10, 0, -10, -10),
                           Qt::AlignRight | Qt::AlignBottom, stamp);
            }
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
            offsetStuds = QPointF(scenePos.x() / 8.0, scenePos.y() / 8.0);
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
    auto* prefsAct = edit->addAction(tr("&Preferences..."));
    prefsAct->setShortcut(QKeySequence::Preferences);
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
    auto addDockToggle = [view](QDockWidget* d, const QString& label){
        auto* act = view->addAction(label);
        act->setCheckable(true);
        act->setChecked(d->isVisible());
        QObject::connect(act, &QAction::toggled, d, &QDockWidget::setVisible);
        QObject::connect(d, &QDockWidget::visibilityChanged, act, &QAction::setChecked);
    };
    addDockToggle(partsBrowser_,       tr("&Parts Panel"));
    addDockToggle(layerPanel_,         tr("&Layers Panel"));
    addDockToggle(modulesPanel_,       tr("&Modules Panel"));
    addDockToggle(moduleLibraryPanel_, tr("Module Li&brary Panel"));
    addDockToggle(venueLibraryPanel_,  tr("&Venue Library Panel"));
    addDockToggle(partUsagePanel_,     tr("&Used Parts Panel"));

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
    auto* createModAct = modules->addAction(tr("Create from &Selection..."));
    connect(createModAct, &QAction::triggered, this, &MainWindow::onCreateModuleFromSelection);
    auto* importModAct = modules->addAction(tr("&Import .bbm as Module..."));
    connect(importModAct, &QAction::triggered, this, &MainWindow::onImportBbmAsModule);
    auto* saveModAct = modules->addAction(tr("&Save Selection as Module..."));
    connect(saveModAct, &QAction::triggered, this, &MainWindow::onSaveSelectionAsModule);
    auto* saveSetAct = modules->addAction(tr("Save Selection as &Set..."));
    saveSetAct->setToolTip(tr("Export the current brick selection as a "
                              "BrickTracks-style .set.xml drop-in for the parts library"));
    connect(saveSetAct, &QAction::triggered, this, &MainWindow::onSaveSelectionAsSet);

    auto* help = menuBar()->addMenu(tr("&Help"));
    auto* contentsAct = help->addAction(tr("&Contents"));
    contentsAct->setShortcut(QKeySequence::HelpContents);
    connect(contentsAct, &QAction::triggered, this, [this]{
        // BlueBrick's manual as offline HTML, in the UI language if there
        // is one, else English; the online copy if it wasn't installed.
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString lang = QSettings().value(QStringLiteral("general/language")).toString().left(2);
        QStringList roots;
        for (const char* rel : { "/../Resources/help", "/../share/brick-layout-designer/help", "/help" })
            roots << exeDir + QLatin1String(rel);
        roots << QStringLiteral(BLD_HELP_SOURCE_DIR);
        for (const QString& root : std::as_const(roots)) {
            for (const QString& l : { lang, QStringLiteral("en") }) {
                const QString index = QDir(root).filePath(l + QStringLiteral("/index.html"));
                if (!l.isEmpty() && QFileInfo::exists(index)) {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(index).absoluteFilePath()));
                    return;
                }
            }
        }
        QDesktopServices::openUrl(QUrl(QStringLiteral(
            "https://github.com/brick-layout-designer/brick-layout-designer/tree/main/help/en")));
    });
    auto* updateAct = help->addAction(tr("Check for &Updates..."));
    connect(updateAct, &QAction::triggered, this, [this]{ updates_->checkNow(); });
    help->addSeparator();
    auto* aboutAct = help->addAction(tr("&About BLD..."));
    connect(aboutAct, &QAction::triggered, this, &MainWindow::onAbout);
    auto* aboutQtAct = help->addAction(tr("About &Qt..."));
    connect(aboutQtAct, &QAction::triggered, qApp, &QApplication::aboutQt);
}

}  // namespace bld::ui
