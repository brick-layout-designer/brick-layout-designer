// Map menu — background colour, general info, and the venue sub-menu
// (draw outline, draw-by-dimensions, add obstacle, edit properties,
// clear, venue library panel, load from file).
// Pulled out of MainWindowMenus.cpp so the remaining setupMenus()
// function focuses on File / Edit / View / Tools wiring rather than
// the venue dialog churn.

#include "MainWindow.h"

#include "../edit/venue/VenueDesign.h"
#include "MapView.h"
#include "VenueDialog.h"
#include "VenueDimensionsDialog.h"
#include "VenueLibraryPanel.h"
#include "venue/VenueDesignerDialog.h"

#include "../core/Map.h"
#include "../core/Venue.h"
#include "../edit/EditCommands.h"
#include "../edit/LayerCommands.h"
#include "../edit/VenueCommands.h"
#include "../saveload/VenueIO.h"

#include <QAction>
#include <QColorDialog>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStatusBar>
#include <QUndoStack>

namespace bld::ui {

void MainWindow::setupMapMenu() {
    auto* mapMenu = menuBar()->addMenu(tr("&Map"));
    auto* bgAct = mapMenu->addAction(tr("Background &Colour..."));
    connect(bgAct, &QAction::triggered, this, [this]{
        auto* m = mapView_->currentMap();
        if (!m) return;
        QColor init = m->backgroundColor.color;
        const QColor c = QColorDialog::getColor(init, this, tr("Background colour"),
                                                 QColorDialog::ShowAlphaChannel);
        if (!c.isValid()) return;
        mapView_->undoStack()->push(new edit::ChangeBackgroundColorCommand(
            *m, core::ColorSpec::fromArgb(c)));
        mapView_->scene()->setBackgroundBrush(c);
    });
    auto* bgImgAct = mapMenu->addAction(tr("Background &Image..."));
    bgImgAct->setToolTip(tr("Optional raster image painted under the layout (BLD-only, stored in .bbm.bld sidecar)"));
    connect(bgImgAct, &QAction::triggered, this, [this]{
        auto* m = mapView_->currentMap();
        if (!m) return;
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Background image"));
        auto* form = new QFormLayout(&dlg);
        auto* pathEdit = new QLineEdit(m->sidecar.backgroundImagePath, &dlg);
        auto* browseBtn = new QPushButton(tr("..."), &dlg);
        auto* clearBtn  = new QPushButton(tr("Clear"), &dlg);
        auto* row = new QHBoxLayout();
        row->addWidget(pathEdit); row->addWidget(browseBtn); row->addWidget(clearBtn);
        auto* rowWrap = new QWidget(&dlg); rowWrap->setLayout(row);
        form->addRow(tr("Image path:"), rowWrap);
        connect(browseBtn, &QPushButton::clicked, &dlg, [pathEdit, &dlg]{
            const QString p = QFileDialog::getOpenFileName(&dlg, tr("Choose background image"),
                pathEdit->text(),
                tr("Images (*.png *.jpg *.jpeg *.bmp *.gif);;All files (*)"));
            if (!p.isEmpty()) pathEdit->setText(p);
        });
        connect(clearBtn, &QPushButton::clicked, &dlg, [pathEdit]{ pathEdit->clear(); });

        auto* opacitySpin = new QDoubleSpinBox(&dlg);
        opacitySpin->setRange(0.0, 1.0);
        opacitySpin->setSingleStep(0.05);
        opacitySpin->setValue(m->sidecar.backgroundImageOpacity);
        form->addRow(tr("Opacity:"), opacitySpin);

        auto* xSpin = new QDoubleSpinBox(&dlg);
        xSpin->setRange(-100000, 100000); xSpin->setDecimals(1);
        auto* ySpin = new QDoubleSpinBox(&dlg);
        ySpin->setRange(-100000, 100000); ySpin->setDecimals(1);
        auto* wSpin = new QDoubleSpinBox(&dlg);
        wSpin->setRange(0, 100000); wSpin->setDecimals(1);
        auto* hSpin = new QDoubleSpinBox(&dlg);
        hSpin->setRange(0, 100000); hSpin->setDecimals(1);
        const auto& r = m->sidecar.backgroundImageRectStuds;
        if (!r.isNull()) {
            xSpin->setValue(r.x()); ySpin->setValue(r.y());
            wSpin->setValue(r.width()); hSpin->setValue(r.height());
        }
        auto* rectGroupForm = new QFormLayout();
        rectGroupForm->addRow(tr("X (studs):"), xSpin);
        rectGroupForm->addRow(tr("Y (studs):"), ySpin);
        rectGroupForm->addRow(tr("Width (studs):"), wSpin);
        rectGroupForm->addRow(tr("Height (studs):"), hSpin);
        auto* rectWrap = new QWidget(&dlg); rectWrap->setLayout(rectGroupForm);
        form->addRow(tr("Placement (0 width = native size):"), rectWrap);

        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;

        m->sidecar.backgroundImagePath = pathEdit->text();
        m->sidecar.backgroundImageOpacity = opacitySpin->value();
        if (wSpin->value() > 0 && hSpin->value() > 0) {
            m->sidecar.backgroundImageRectStuds = QRectF(
                xSpin->value(), ySpin->value(), wSpin->value(), hSpin->value());
        } else {
            m->sidecar.backgroundImageRectStuds = QRectF();
        }
        mapView_->viewport()->update();
    });
    auto* infoAct = mapMenu->addAction(tr("General &Info..."));
    connect(infoAct, &QAction::triggered, this, [this]{
        auto* m = mapView_->currentMap();
        if (!m) return;
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Map information"));
        auto* form = new QFormLayout(&dlg);
        auto* authorE = new QLineEdit(m->author, &dlg);
        auto* lugE    = new QLineEdit(m->lug, &dlg);
        auto* eventE  = new QLineEdit(m->event, &dlg);
        auto* dateE   = new QDateEdit(m->date, &dlg); dateE->setCalendarPopup(true);
        auto* commentE = new QPlainTextEdit(m->comment, &dlg);
        commentE->setMinimumHeight(100);
        form->addRow(tr("Author:"),  authorE);
        form->addRow(tr("LUG:"),     lugE);
        form->addRow(tr("Event:"),   eventE);
        form->addRow(tr("Date:"),    dateE);
        form->addRow(tr("Comment:"), commentE);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;
        edit::ChangeGeneralInfoCommand::Info next{
            authorE->text(), lugE->text(), eventE->text(),
            dateE->date(), commentE->toPlainText()
        };
        mapView_->undoStack()->push(new edit::ChangeGeneralInfoCommand(*m, std::move(next)));
    });

    mapMenu->addSeparator();
    auto* venueMenu = mapMenu->addMenu(tr("&Venue"));
    auto* designerAct = venueMenu->addAction(tr("Open Venue &Designer..."));
    designerAct->setToolTip(
        tr("Design this layout's venue: walls, doors, openings, columns, stairs, power, measurements."));
    connect(designerAct, &QAction::triggered, this, [this] {
        auto* m = mapView_->currentMap();
        if (!m) return;
        core::Venue initial = m->sidecar.venue.value_or(edit::venue::emptyVenue(tr("Venue")));
        VenueDesignerDialog dlg(
            std::move(initial), tr("Venue of this layout"), tr("Save to Layout"),
            [this](const core::Venue& v) {
                auto* map = mapView_->currentMap();
                if (!map) return tr("The layout was closed.");
                mapView_->undoStack()->push(new edit::SetVenueCommand(*map, std::make_optional(v)));
                return QString();
            },
            this);
        dlg.exec();
    });
    venueMenu->addSeparator();
    auto* drawOutlineAct = venueMenu->addAction(tr("Draw &Outline..."));
    drawOutlineAct->setToolTip(tr("Click points on the map to build the venue outline. "
                                    "Right-click or Enter finishes; Escape cancels."));
    connect(drawOutlineAct, &QAction::triggered, this, [this]{
        if (!mapView_->currentMap()) return;
        mapView_->setTool(MapView::Tool::DrawVenueOutline);
        statusBar()->showMessage(
            tr("Click points to outline the venue. Right-click / Enter to finish, Escape to cancel."),
            8000);
    });
    auto* drawByDimsAct = venueMenu->addAction(tr("Draw Outline by &Dimensions..."));
    drawByDimsAct->setToolTip(tr("Build the venue outline by entering lengths + angles "
                                   "instead of clicking points on the map."));
    connect(drawByDimsAct, &QAction::triggered, this, [this]{
        auto* m = mapView_->currentMap();
        if (!m) return;
        VenueDimensionsDialog dlg(this);
        if (dlg.exec() != QDialog::Accepted) return;
        const auto poly  = dlg.polygon();
        const auto metas = dlg.segments();
        if (poly.size() < 3) return;
        core::Venue v = m->sidecar.venue.value_or(core::Venue{});
        v.enabled = true;
        v.edges.clear();
        for (int i = 0; i < poly.size(); ++i) {
            core::VenueEdge e;
            e.polyline = { poly[i], poly[(i + 1) % poly.size()] };
            if (i < metas.size()) {
                e.kind  = metas[i].kind;
                e.label = metas[i].label;
            } else {
                e.kind  = core::EdgeKind::Wall;
            }
            v.edges.push_back(e);
        }
        mapView_->undoStack()->push(new edit::SetVenueCommand(*m, std::make_optional(v)));
        statusBar()->showMessage(
            tr("Venue outline built from %1 segments").arg(poly.size()), 3000);
    });

    auto* drawObstacleAct = venueMenu->addAction(tr("Add &Obstacle..."));
    drawObstacleAct->setToolTip(tr("Click points to add an obstacle polygon (pillar, column)."));
    connect(drawObstacleAct, &QAction::triggered, this, [this]{
        if (!mapView_->currentMap() || !mapView_->currentMap()->sidecar.venue) {
            QMessageBox::information(this, tr("Add obstacle"),
                tr("Draw the venue outline first."));
            return;
        }
        mapView_->setTool(MapView::Tool::DrawVenueObstacle);
        statusBar()->showMessage(
            tr("Click points to outline an obstacle. Right-click / Enter to finish, Escape to cancel."),
            8000);
    });
    venueMenu->addSeparator();
    auto* editVenueAct = venueMenu->addAction(tr("&Edit Venue Properties..."));
    connect(editVenueAct, &QAction::triggered, this, [this]{
        auto* m = mapView_->currentMap();
        if (!m) return;
        VenueDialog dlg(m->sidecar.venue, this);
        if (dlg.exec() != QDialog::Accepted) return;
        if (dlg.cleared()) {
            mapView_->undoStack()->push(new edit::SetVenueCommand(*m, std::nullopt));
        } else {
            mapView_->undoStack()->push(new edit::SetVenueCommand(*m, dlg.result()));
        }
    });
    auto* clearVenueAct = venueMenu->addAction(tr("&Clear Venue"));
    connect(clearVenueAct, &QAction::triggered, this, [this]{
        auto* m = mapView_->currentMap();
        if (!m || !m->sidecar.venue) return;
        const auto btn = QMessageBox::question(this, tr("Clear venue"),
            tr("Remove the venue from this project?"));
        if (btn != QMessageBox::Yes) return;
        mapView_->undoStack()->push(new edit::SetVenueCommand(*m, std::nullopt));
    });

    venueMenu->addSeparator();

    auto* showLibraryAct = venueMenu->addAction(tr("Venue &Library..."));
    showLibraryAct->setToolTip(tr("Open the Venue Library panel to browse, load, or save venues."));
    connect(showLibraryAct, &QAction::triggered, this, [this]{
        venueLibraryPanel_->show();
        venueLibraryPanel_->raise();
    });

    auto* loadVenueFileAct = venueMenu->addAction(tr("Load Venue from &File..."));
    connect(loadVenueFileAct, &QAction::triggered, this, [this]{
        auto* m = mapView_->currentMap();
        if (!m) return;
        const QString path = QFileDialog::getOpenFileName(this, tr("Load venue file"), {},
            tr("Venue (*.bld-venue);;All files (*)"));
        if (path.isEmpty()) return;
        QString err;
        auto venue = saveload::readVenueFile(path, &err);
        if (!venue) {
            QMessageBox::warning(this, tr("Load venue"),
                tr("Could not read %1: %2").arg(path, err));
            return;
        }
        if (m->sidecar.venue) {
            const auto btn = QMessageBox::question(this, tr("Replace venue"),
                tr("This project already has a venue. Replace it?"));
            if (btn != QMessageBox::Yes) return;
        }
        mapView_->undoStack()->push(new edit::SetVenueCommand(*m, venue));
    });
}

}  // namespace bld::ui
