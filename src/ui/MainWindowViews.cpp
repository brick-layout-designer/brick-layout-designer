// Saved views and pictures in the main window: the Views panel under the
// Build tab, the "Showing … · Show everything" strip over the map, File >
// Share Picture… and Export All Views… (the web's saved views,
// references/LAYOUT-FILE.md "Saved views").

#include "MainWindow.h"

#include "MapView.h"
#include "SavedViews.h"
#include "SharePictureDialog.h"
#include "ViewsPanel.h"
#ifdef BLD_SYNC
#include "LiveLayout.h"
#endif

#include "../core/Layer.h"
#include "../core/LayerGrid.h"
#include "../core/Map.h"
#include "../edit/ViewCommands.h"

#include <QAction>
#include <QFileInfo>
#include <QStatusBar>
#include <QTimer>
#include <QUndoStack>

namespace bld::ui {

namespace {

// Whether the map shows a grid now (a new view starts with the same).
bool gridShownNow(MapView& view) {
    const core::Map* map = view.currentMap();
    if (!map) return false;
    if (const auto& f = view.viewFilter()) return f->grid;
    for (const auto& l : map->layers())
        if (l && l->kind() == core::LayerKind::Grid && l->visible) return true;
    return false;
}

const core::SavedView* findView(const core::Map* map, const QString& id) {
    if (!map || id.isEmpty()) return nullptr;
    for (const auto& v : map->sidecar.views)
        if (v.id == id) return &v;
    return nullptr;
}

}  // namespace

void MainWindow::setupViews() {
    viewsPanel_ = new ViewsPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, viewsPanel_);
    viewsPanel_->setScreenRect([this] { return mapView_->screenRectStuds(); });
    viewsPanel_->setGridShown([this] { return gridShownNow(*mapView_); });

    connect(viewsPanel_, &ViewsPanel::viewsEdited, this,
            [this](const std::vector<core::SavedView>& views, const QString& what) {
                core::Map* map = mapView_->currentMap();
                if (!map) return;
                // The view being looked at shows its new sheets, grid and
                // labels (refreshViews, on the undo stack's change), but the
                // map stays put: the user may be on the next area already,
                // as on the web.
                mapView_->undoStack()->push(new edit::SetViewsCommand(*map, views, what));
            });
    connect(viewsPanel_, &ViewsPanel::goToViewRequested, this, [this](const core::SavedView& v) { goToView(v); });
    // Clicking the view on show again: stop showing it, the map where it is.
    connect(viewsPanel_, &ViewsPanel::leaveViewRequested, this, &MainWindow::clearActiveView);
    connect(viewsPanel_, &ViewsPanel::showEverythingRequested, this, &MainWindow::showEverything);
    // Queued: the dialog runs its own event loop, which mustn't run inside
    // the click of a row button that a change to the views may replace.
    connect(viewsPanel_, &ViewsPanel::sharePictureRequested, this, [this](const QString& id) { openSharePicture(id); },
            Qt::QueuedConnection);
    connect(viewsPanel_, &ViewsPanel::exportAllRequested, this, [this] { exportAllViews(false); },
            Qt::QueuedConnection);

    viewIndicator_ = new ViewIndicator(mapView_);
    connect(viewIndicator_, &ViewIndicator::showEverythingRequested, this, &MainWindow::showEverything);

    connect(mapView_, &MapView::mapLoaded, this, &MainWindow::refreshViews);
    connect(mapView_->undoStack(), &QUndoStack::indexChanged, this, [this](int) { refreshViews(); });
    refreshViews();
}

void MainWindow::goToView(const core::SavedView& view) {
    const core::Map* map = mapView_->currentMap();
    if (!map) return;
    mapView_->setViewFilter(MapView::ViewFilter{ view.sheets, view.grid, view.labels });
    activeViewId_ = view.id;
    viewsPanel_->setActiveView(view.id);
    viewIndicator_->setViewName(view.name.isEmpty() ? tr("View") : view.name);
    // Module frames and names count, as they're drawn round the modules.
    if (const auto region = views::viewRegionStuds(view, *map, mapView_->builder()))
        mapView_->showRegionStuds(*region);
}

void MainWindow::clearActiveView() {
    activeViewId_.clear();
    if (!viewsPanel_) return;  // still being built
    mapView_->setViewFilter(std::nullopt);
    viewsPanel_->setActiveView({});
    viewIndicator_->setViewName({});
}

void MainWindow::showEverything() {
    clearActiveView();
    onFitToView();
}

void MainWindow::refreshViews() {
    if (!viewsPanel_) return;
    const core::Map* map = mapView_->currentMap();
    viewsPanel_->setMap(map);
    if (activeViewId_.isEmpty()) return;
    const core::SavedView* v = findView(map, activeViewId_);
    if (!v) {
        // Deleted, here or by someone else on a live layout.
        clearActiveView();
        return;
    }
    mapView_->setViewFilter(MapView::ViewFilter{ v->sheets, v->grid, v->labels });
    viewsPanel_->setActiveView(v->id);
    viewIndicator_->setViewName(v->name.isEmpty() ? tr("View") : v->name);
}

QString MainWindow::layoutTitle() const {
#ifdef BLD_SYNC
    if (live_ && live_->active() && !live_->title().isEmpty()) return live_->title();
#endif
    if (!currentFilePath_.isEmpty()) return QFileInfo(currentFilePath_).completeBaseName();
    return tr("Layout");
}

void MainWindow::openSharePicture(const QString& choice) {
    const core::Map* map = mapView_->currentMap();
    if (!map) return;
    SharePictureDialog::Input in;
    in.map = map;
    in.parts = &parts_;
    in.layoutTitle = layoutTitle();
    in.initialChoice = choice;
    if (const auto rect = mapView_->screenRectStuds()) {
        const auto& f = mapView_->viewFilter();
        views::PictureSpec screen;
        screen.region = *rect;
        screen.sheets = f ? f->sheets : std::nullopt;
        screen.grid = gridShownNow(*mapView_);
        screen.labels = f ? f->labels : true;
        in.screen = screen;
    }
    SharePictureDialog dialog(std::move(in), this);
    connect(&dialog, &SharePictureDialog::moreOptionsRequested, this, [this] {
        if (exportImageAct_) QTimer::singleShot(0, exportImageAct_, &QAction::trigger);
    });
    dialog.exec();
}

void MainWindow::exportAllViews(bool chooseFolder) {
    const core::Map* map = mapView_->currentMap();
    if (!map) return;
    const QString msg = runExportAllViews(this, *map, parts_, layoutTitle(), chooseFolder);
    if (!msg.isEmpty()) statusBar()->showMessage(msg, 8000);
}

}  // namespace bld::ui
