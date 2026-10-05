// The main window's friendlier shell (redesign phase a): a toolbar of
// labelled tools, the Build / Venue / Notes / Parts list task tabs, the same
// header on every panel, and a status bar with the piece count, size,
// current sheet and zoom. Every menu and action stays where it was; the
// toolbar only gathers the everyday ones. UI text says "Sheets" for layers;
// code and files keep their names.

#include "MainWindow.h"

#include "LayerPanel.h"
#include "MapView.h"
#include "ModuleLibraryPanel.h"
#include "ModulesPanel.h"
#include "PartUsagePanel.h"
#include "PartsBrowser.h"
#include "SettingsDialog.h"
#include "tours/Tours.h"
#include "VenueLibraryPanel.h"
#include "ViewsPanel.h"
#include "help/HelpButton.h"
#include "theme/AppPrefs.h"
#include "theme/Icons.h"
#include "theme/PanelHeader.h"
#include "PrefsSync.h"

#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"

#include <QAction>
#include <QActionGroup>
#include <QButtonGroup>
#include <QColorDialog>
#include <QEvent>
#include <QFrame>
#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QSettings>
#include <QStatusBar>
#include <QMenuBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>

namespace bld::ui {

namespace {

// Watches the map's viewport paint and reports when the zoom changed.
class ZoomWatcher : public QObject {
public:
    ZoomWatcher(MapView* view, QLabel* label) : QObject(label), view_(view), label_(label) {
        view->viewport()->installEventFilter(this);
    }
    void refresh() {
        const double z = view_->transform().m11();
        if (qFuzzyCompare(z, last_)) return;
        last_ = z;
        label_->setText(QStringLiteral("%1%").arg(qRound(z * 100)));
    }

protected:
    bool eventFilter(QObject*, QEvent* e) override {
        if (e->type() == QEvent::Paint) refresh();
        return false;
    }

private:
    MapView* view_;
    QLabel* label_;
    double last_ = -1;
};

}  // namespace

void MainWindow::setupShell() {
    auto& prefs = theme::PrefsStore::instance();

    // Panels: plain names and the same header everywhere, each with its
    // "?" (the web's panel help keys).
    layerPanel_->setWindowTitle(tr("Sheets"));
    venueLibraryPanel_->setWindowTitle(tr("Venue library"));
    partUsagePanel_->setWindowTitle(tr("Parts list"));
    theme::PanelHeader::install(partsBrowser_, QStringLiteral("panel.parts"), prefs);
    theme::PanelHeader::install(layerPanel_, QStringLiteral("panel.sheets"), prefs);
    theme::PanelHeader::install(viewsPanel_, QStringLiteral("panel.views"), prefs);
    theme::PanelHeader::install(partUsagePanel_, QStringLiteral("panel.partsList"), prefs);
    theme::PanelHeader::install(modulesPanel_, QStringLiteral("panel.modules"), prefs);
    theme::PanelHeader::install(moduleLibraryPanel_, QStringLiteral("panel.moduleLibrary"), prefs);
    theme::PanelHeader::install(venueLibraryPanel_, QStringLiteral("panel.roomLibrary"), prefs);

    // ----- Toolbar: labelled everyday tools, the task tabs in the middle,
    // Help on the right. Cut / Copy / Paste and the stacking order live in
    // the Edit menu and the map's right-click menu.
    auto* toolbar = addToolBar(tr("Toolbar"));
    mainToolbar_ = toolbar;
    toolbar->setObjectName(QStringLiteral("toolbar.main"));  // restoreState() knows it by this
    toolbar->setProperty("mainToolbar", true);
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    toolbar->setIconSize(QSize(20, 20));

    const auto addTool = [this, toolbar](const QString& icon, const QString& text, const QString& tip,
                                         std::function<void()> onClick) {
        auto* a = toolbar->addAction(text);
        a->setToolTip(tip);
        a->setObjectName(QStringLiteral("tool.") + icon);
        connect(a, &QAction::triggered, this, std::move(onClick));
        shellIcons_.append({ a, icon });
        return a;
    };

    // New / Open / Save stay in the File menu (and their usual shortcuts),
    // which leaves room for the task tabs.
    // Undo / Redo follow whichever menu action is in use (the live one while
    // a server layout is open).
    auto* undo = addTool(QStringLiteral("undo"), tr("Undo"), tr("Undo the last change"), [this] {
        if (liveUndoAct_ && liveUndoAct_->isVisible()) { liveUndoAct_->trigger(); return; }
        undoAct_->trigger();
    });
    auto* redo = addTool(QStringLiteral("redo"), tr("Redo"), tr("Redo what you undid"), [this] {
        if (liveRedoAct_ && liveRedoAct_->isVisible()) { liveRedoAct_->trigger(); return; }
        redoAct_->trigger();
    });
    const auto syncUndo = [this, undo, redo] {
        QAction* u = undoAct_;
        QAction* r = redoAct_;
        if (liveUndoAct_ && liveUndoAct_->isVisible()) { u = liveUndoAct_; r = liveRedoAct_; }
        undo->setEnabled(u && u->isEnabled());
        redo->setEnabled(r && r->isEnabled());
    };
    for (QAction* a : { undoAct_, redoAct_ })
        if (a) connect(a, &QAction::changed, this, syncUndo);
    for (QAction* a : { liveUndoAct_, liveRedoAct_ })
        if (a) connect(a, &QAction::changed, this, syncUndo);
    syncUndo();
    toolbar->addSeparator();

    // The drawing tools, one checked at a time.
    auto* tools = new QActionGroup(this);
    tools->setExclusive(true);
    struct ToolEntry { MapView::Tool tool; QString icon, text, tip; };
    const ToolEntry entries[] = {
        { MapView::Tool::Select, QStringLiteral("select"), tr("Select"), tr("Pick and move pieces") },
        { MapView::Tool::PaintArea, QStringLiteral("paint"), tr("Paint"), tr("Paint areas of colour on an area sheet") },
        { MapView::Tool::EraseArea, QStringLiteral("erase"), tr("Erase"), tr("Erase painted areas") },
        { MapView::Tool::DrawLinearRuler, QStringLiteral("measure"), tr("Measure"), tr("Draw a ruler to measure a distance") },
        { MapView::Tool::DrawCircularRuler, QStringLiteral("circle"), tr("Circle"), tr("Draw a circle to measure a radius") },
    };
    for (const ToolEntry& e : entries) {
        QAction* a = addTool(e.icon, e.text, e.tip, [this, t = e.tool] { mapView_->setTool(t); });
        a->setCheckable(true);
        a->setChecked(e.tool == MapView::Tool::Select);
        tools->addAction(a);
    }
    // Measure and Circle leave rulers on the map.
    help::HelpButton::addTo(toolbar, QStringLiteral("dialog.measure"),
                            toolbar->widgetForAction(findChild<QAction*>(QStringLiteral("tool.measure"))));

    // The paint colour, one click away.
    auto* colour = toolbar->addAction(tr("Colour"));
    colour->setObjectName(QStringLiteral("tool.paintColour"));
    colour->setToolTip(tr("The colour Paint uses"));
    refreshPaintSwatch_ = [this, colour] {
        QPixmap pm(QSize(20, 20) * 2);
        pm.setDevicePixelRatio(2);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(palette().color(QPalette::Mid), 1));
        p.setBrush(mapView_->paintColor());
        p.drawRoundedRect(QRectF(2.5, 2.5, 15, 15), 4, 4);
        colour->setIcon(QIcon(pm));
    };
    {
        QSettings s;
        s.beginGroup(QStringLiteral("editing"));
        QColor saved(s.value(QStringLiteral("paintColor"), QColor(0, 128, 0).name()).toString());
        // A 0-alpha paint colour silently does nothing on the canvas.
        if (saved.isValid() && saved.alpha() == 0) saved.setAlpha(255);
        if (saved.isValid()) mapView_->setPaintColor(saved);
    }
    connect(colour, &QAction::triggered, this, [this] {
        const QColor c = QColorDialog::getColor(mapView_->paintColor(), this, tr("Paint colour"),
                                                QColorDialog::ShowAlphaChannel);
        if (!c.isValid()) return;
        mapView_->setPaintColor(c);
        refreshPaintSwatch_();
        QSettings s;
        s.beginGroup(QStringLiteral("editing"));
        s.setValue(QStringLiteral("paintColor"), c.name(QColor::HexArgb));
    });
    help::HelpButton::addTo(toolbar, QStringLiteral("toolbar.paintColour"), toolbar->widgetForAction(colour));
    toolbar->addSeparator();

    addTool(QStringLiteral("turnLeft"), tr("Turn left"), tr("Turn the selected pieces anticlockwise"), [this] {
        mapView_->rotateSelected(static_cast<float>(-mapView_->rotationStepDegrees()));
    });
    addTool(QStringLiteral("turnRight"), tr("Turn right"), tr("Turn the selected pieces clockwise"), [this] {
        mapView_->rotateSelected(static_cast<float>(mapView_->rotationStepDegrees()));
    });
    addTool(QStringLiteral("delete"), tr("Delete"), tr("Remove the selected pieces"), [this] { mapView_->deleteSelected(); });
    toolbar->addSeparator();
    // Share picture: File > Share Picture…, one click away.
    auto* picture = addTool(QStringLiteral("picture"), tr("Picture"), tr("Share a picture of the layout"), [this] { openSharePicture(); });
    tours::tag(toolbar->widgetForAction(picture), QStringLiteral("share.picture"));
    // Folded away in a narrow window: the toolbar, where its » button finds it.
    tours::tag(toolbar, QStringLiteral("share.picture"), true);
    // What the editor tour points at for the map and for saving.
    tours::tag(mapView_, QStringLiteral("map"));
    tours::tag(statusBar(), QStringLiteral("topbar.saveStatus"));
    toolbar->addSeparator();

    // Snap: click turns it on or off, the arrow picks the step (vanilla's
    // off + 32/16/8/4/2/1/0.5 studs).
    auto* snapBtn = new QToolButton(this);
    snapBtn->setObjectName(QStringLiteral("tool.snap"));
    snapBtn->setPopupMode(QToolButton::MenuButtonPopup);
    snapBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    snapBtn->setCheckable(true);
    snapBtn->setToolTip(tr("Line pieces up on a grid (click to turn on or off, the arrow picks the step)"));
    auto* snapMenu = new QMenu(snapBtn);
    auto* snapGroup = new QActionGroup(snapBtn);
    QHash<double, QAction*> snapActByValue;
    for (double v : { 32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5 }) {
        auto* a = snapMenu->addAction(tr("%1 studs").arg(v));
        a->setCheckable(true);
        a->setData(v);
        snapGroup->addAction(a);
        snapActByValue.insert(v, a);
        connect(a, &QAction::triggered, this, [this, snapBtn, v] {
            mapView_->setSnapStepStuds(v);
            snapBtn->setChecked(true);
            snapBtn->setText(tr("Snap %1").arg(v));
            QSettings s;
            s.beginGroup(QStringLiteral("editing"));
            s.setValue(QStringLiteral("snapStepStuds"), v);
        });
    }
    snapBtn->setMenu(snapMenu);
    connect(snapBtn, &QToolButton::clicked, this, [this, snapBtn](bool on) {
        double v = 0.0;
        if (on) {
            v = 32.0;
            for (QAction* a : snapBtn->menu()->actions())
                if (a->isChecked()) { v = a->data().toDouble(); break; }
        }
        mapView_->setSnapStepStuds(v);
        snapBtn->setText(on ? tr("Snap %1").arg(v) : tr("Snap off"));
        QSettings s;
        s.beginGroup(QStringLiteral("editing"));
        s.setValue(QStringLiteral("snapStepStuds"), v);
    });
    shellIcons_.append({ toolbar->addWidget(snapBtn), QStringLiteral("snap") });
    help::HelpButton::addTo(toolbar, QStringLiteral("toolbar.snap"), snapBtn);

    // How far Turn left / right turns.
    auto* rotBtn = new QToolButton(this);
    rotBtn->setObjectName(QStringLiteral("tool.angle"));
    rotBtn->setPopupMode(QToolButton::InstantPopup);
    rotBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    rotBtn->setToolTip(tr("How far Turn left and Turn right turn"));
    auto* rotMenu = new QMenu(rotBtn);
    auto* rotGroup = new QActionGroup(rotBtn);
    QHash<double, QAction*> rotActByValue;
    for (double v : { 90.0, 45.0, 22.5, 11.25, 5.0, 1.0 }) {
        auto* a = rotMenu->addAction(QStringLiteral("%1°").arg(v));
        a->setCheckable(true);
        a->setData(v);
        rotGroup->addAction(a);
        rotActByValue.insert(v, a);
        connect(a, &QAction::triggered, this, [this, rotBtn, v] {
            mapView_->setRotationStepDegrees(v);
            rotBtn->setText(tr("Turn %1°").arg(v));
            QSettings s;
            s.beginGroup(QStringLiteral("editing"));
            s.setValue(QStringLiteral("rotationStepDegrees"), v);
        });
    }
    rotBtn->setMenu(rotMenu);
    shellIcons_.append({ toolbar->addWidget(rotBtn), QStringLiteral("angle") });
    help::HelpButton::addTo(toolbar, QStringLiteral("toolbar.rotateStep"), rotBtn);

    // Panels: the web's toolbar menu, the same one as View > Panels.
    auto* panelsBtn = new QToolButton(toolbar);
    panelsBtn->setObjectName(QStringLiteral("tool.panels"));
    panelsBtn->setText(tr("Panels"));
    panelsBtn->setToolTip(tr("Show or hide the side panels"));
    panelsBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    panelsBtn->setPopupMode(QToolButton::InstantPopup);
    panelsBtn->setMenu(panelsMenu_);
    shellIcons_.append({ toolbar->addWidget(panelsBtn), QStringLiteral("panels") });
    help::HelpButton::addTo(toolbar, QStringLiteral("toolbar.panels"), panelsBtn);

    // The steps saved last time.
    {
        QSettings s;
        s.beginGroup(QStringLiteral("editing"));
        const double snap = s.value(QStringLiteral("snapStepStuds"), 0.0).toDouble();
        const double rot = s.value(QStringLiteral("rotationStepDegrees"), 90.0).toDouble();
        mapView_->setSnapStepStuds(snap);
        mapView_->setRotationStepDegrees(rot);
        snapBtn->setChecked(snap > 0.0);
        snapBtn->setText(snap > 0.0 ? tr("Snap %1").arg(snap) : tr("Snap off"));
        if (auto* a = snapActByValue.value(snap > 0.0 ? snap : 32.0)) a->setChecked(true);
        if (auto* a = rotActByValue.value(rot)) a->setChecked(true);
        rotBtn->setText(tr("Turn %1°").arg(rot));
    }

    // Task tabs, centred between two stretches.
    const auto spacer = [toolbar] {
        auto* w = new QWidget(toolbar);
        w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        toolbar->addWidget(w);
    };
    spacer();
    auto* tabs = new QFrame(toolbar);
    tabs->setObjectName(QStringLiteral("TaskTabs"));
    auto* tabsRow = new QHBoxLayout(tabs);
    tabsRow->setContentsMargins(3, 3, 3, 3);
    tabsRow->setSpacing(2);
    auto* tabGroup = new QButtonGroup(tabs);
    tabGroup->setExclusive(true);
    const std::tuple<QString, QString, QString> tasks[] = {
        { QStringLiteral("build"), tr("Build"), tr("Parts, sheets and views, for building the layout") },
        { QStringLiteral("room"), tr("Venue"), tr("The venue the layout goes in") },
        { QStringLiteral("notes"), tr("Notes"), tr("Author, club, event and notes for this layout") },
        { QStringLiteral("parts"), tr("Parts list"), tr("Every part this layout uses") },
    };
    for (const auto& [id, text, tip] : tasks) {
        auto* b = new QToolButton(tabs);
        b->setObjectName(QStringLiteral("task.") + id);
        b->setText(text);
        b->setToolTip(tip);
        b->setCursor(Qt::PointingHandCursor);
        tabsRow->addWidget(b);
        // Notes opens a dialog, so it never stays selected.
        if (id != QLatin1String("notes")) {
            b->setCheckable(true);
            tabGroup->addButton(b);
        }
        connect(b, &QToolButton::clicked, this, [this, id] { showTask(id); });
    }
    if (auto* build = tabs->findChild<QToolButton*>(QStringLiteral("task.build"))) build->setChecked(true);
    toolbar->addWidget(tabs);
    help::HelpButton::addTo(toolbar, QStringLiteral("topbar.tasks"), tabs);
    spacer();

    auto* help = new QToolButton(toolbar);
    help->setObjectName(QStringLiteral("HelpButton"));
    help->setText(QStringLiteral("?"));
    help->setToolTip(tr("Help"));
    help->setAccessibleName(tr("Help"));
    help->setToolButtonStyle(Qt::ToolButtonTextOnly);
    // The Help menu: Getting started, Keyboard shortcuts, help buttons
    // on or off, and the rest.
    help->setPopupMode(QToolButton::InstantPopup);
    help->setMenu(helpMenu_);
    toolbar->addWidget(help);
    tours::tag(help, QStringLiteral("help.menu"));
    // When a narrow window folds the toolbar's "?" away, the menu bar's Help is still there.
    tours::tag(menuBar(), QStringLiteral("help.menu"), true);

    // ----- Status bar: pieces and the current sheet on the left of the
    // permanent readouts (size, selection, venue, budget, autosave and the
    // zoom follow).
    auto* pieces = new QLabel(this);
    pieces->setObjectName(QStringLiteral("PiecesLabel"));
    statusBar()->addPermanentWidget(pieces);
    // The current sheet, with its "?"; both hide while there is no sheet.
    auto* sheetItem = new QWidget(this);
    sheetItem->setObjectName(QStringLiteral("SheetItem"));
    auto* sheetRow = new QHBoxLayout(sheetItem);
    sheetRow->setContentsMargins(0, 0, 0, 0);
    sheetRow->setSpacing(0);
    auto* sheet = new QLabel(sheetItem);
    sheet->setObjectName(QStringLiteral("SheetLabel"));
    sheetRow->addWidget(sheet);
    sheetRow->addWidget(new help::HelpButton(QStringLiteral("status.sheet"), sheetItem, sheet));
    statusBar()->addPermanentWidget(sheetItem);
    const auto refresh = [this, pieces, sheet] {
        const core::Map* map = mapView_->currentMap();
        int count = 0;
        QString sheetName;
        if (map) {
            for (const auto& L : map->layers())
                if (L && L->kind() == core::LayerKind::Brick)
                    count += static_cast<int>(static_cast<const core::LayerBrick&>(*L).bricks.size());
            const int i = map->selectedLayerIndex;
            if (i >= 0 && i < static_cast<int>(map->layers().size()) && map->layers()[i])
                sheetName = map->layers()[i]->name;
        }
        pieces->setText(count == 1 ? tr("1 piece") : tr("%1 pieces").arg(count));
        sheet->setText(sheetName.isEmpty() ? QString() : tr("Sheet: %1").arg(sheetName));
        sheet->parentWidget()->setVisible(!sheetName.isEmpty());
    };
    connect(mapView_->undoStack(), &QUndoStack::indexChanged, this, refresh);
    connect(mapView_, &MapView::layersChanged, this, refresh);
    connect(layerPanel_, &LayerPanel::activeLayerChanged, this, refresh);
    QTimer::singleShot(0, this, refresh);

    refreshShellIcons();
}

void MainWindow::addZoomReadout() {
    auto* zoom = new QLabel(this);
    zoom->setObjectName(QStringLiteral("ZoomLabel"));
    zoom->setToolTip(tr("Zoom. Press F to fit the whole layout."));
    statusBar()->addPermanentWidget(zoom);
    auto* watcher = new ZoomWatcher(mapView_, zoom);
    watcher->refresh();
}

void MainWindow::refreshShellIcons() {
    for (const auto& [action, name] : std::as_const(shellIcons_)) {
        const QIcon icon = theme::lineIcon(name, palette());
        // Widgets in the toolbar (snap, turn step) carry the icon themselves.
        bool onWidget = false;
        if (mainToolbar_)
            if (auto* b = qobject_cast<QToolButton*>(mainToolbar_->widgetForAction(action));
                b && b->defaultAction() != action) {
                b->setIcon(icon);
                onWidget = true;
            }
        if (!onWidget) action->setIcon(icon);
    }
    if (refreshPaintSwatch_) refreshPaintSwatch_();
}

void MainWindow::changeEvent(QEvent* e) {
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::PaletteChange) refreshShellIcons();
}

void MainWindow::showTask(const QString& task) {
    // Like the web: a hidden panel comes back on the right side.
    const auto show = [](QDockWidget* d) {
        theme::PanelHeader::setShown(d, true);
        d->raise();
    };
    if (task == QLatin1String("build")) {
        show(partsBrowser_);
        show(layerPanel_);
        show(viewsPanel_);
    } else if (task == QLatin1String("room")) {
        show(venueLibraryPanel_);
    } else if (task == QLatin1String("parts")) {
        show(partUsagePanel_);
    } else if (task == QLatin1String("notes")) {
        if (generalInfoAct_) generalInfoAct_->trigger();
    }
}

QList<QDockWidget*> MainWindow::panelDocks() const {
    return { partsBrowser_, layerPanel_, viewsPanel_, partUsagePanel_,
             modulesPanel_, moduleLibraryPanel_, venueLibraryPanel_ };
}

void MainWindow::openSettings() {
    SettingsDialog dialog(theme::PrefsStore::instance(), syncedHost(),
                          [this] { if (preferencesAct_) preferencesAct_->trigger(); }, this,
                          // Once Settings has closed.
                          [this](const QString& id) {
                              QTimer::singleShot(0, this, [this, id] { startTourNamed(id); });
                          });
    dialog.exec();
}

void MainWindow::startTourNamed(const QString& id) {
    auto& store = theme::PrefsStore::instance();
    if (id == QLatin1String("rooms") && mapView_->currentMap()) {
        if (auto* designer = findChild<QAction*>(QStringLiteral("map.roomDesigner"))) {
            tours::startTourOnNext("bld::ui::VenueDesignerDialog", id, store);
            QTimer::singleShot(0, designer, &QAction::trigger);
            return;
        }
    }
    if (id == QLatin1String("clubs")) {
        tours::startTourOnNext("bld::sync::ConnectDialog", id, store);
        QTimer::singleShot(0, this, &MainWindow::onConnectToServer);
        return;
    }
    tours::startTour(this, id, store);
}

void MainWindow::showWelcomeIfNew() {
    auto& store = theme::PrefsStore::instance();
    const QString id = tours::catalogue().welcome.id;
    if (id.isEmpty() || tours::seen(store, id)) return;
    tours::WelcomeDialog welcome(this);
    welcome.exec();
    tours::markSeen(store, id);
    switch (welcome.choice()) {
    case tours::WelcomeDialog::Choice::NewLayout:
        QTimer::singleShot(0, this, &MainWindow::onNew);
        break;
    case tours::WelcomeDialog::Choice::OpenFile:
        if (auto* open = findChild<QAction*>(QStringLiteral("file.open"))) QTimer::singleShot(0, open, &QAction::trigger);
        break;
    case tours::WelcomeDialog::Choice::Server:
    case tours::WelcomeDialog::Choice::Club:
        QTimer::singleShot(0, this, &MainWindow::onConnectToServer);
        break;
    case tours::WelcomeDialog::Choice::Tour:
        QTimer::singleShot(0, this, [this] { startTourNamed(QStringLiteral("editor")); });
        break;
    case tours::WelcomeDialog::Choice::None:
        break;
    }
}

QString MainWindow::syncedHost() const {
    if (prefsSync_ && prefsSync_->active()) return prefsSync_->host();
    return {};
}

}  // namespace bld::ui
