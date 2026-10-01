// The Views panel, the Share Picture dialog and how the main window shows
// a saved view: only this screen changes, never the layout.

#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/SavedViews.h"
#include "ui/SharePictureDialog.h"
#include "ui/UpdateCheck.h"
#include "ui/ViewsPanel.h"

#include "core/Layer.h"
#include "core/Map.h"
#include "import/LayoutFile.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>
#include <QUndoStack>

using namespace bld;

namespace {

// Bricks the map draws now.
int shownBricks(ui::MapView& view) {
    int n = 0;
    for (QGraphicsItem* it : view.scene()->items())
        n += it->isVisible() && it->data(2).toString() == QLatin1String("brick");
    return n;
}

const QString kFixture = QStringLiteral(BLD_SOURCE_DIR "/fixtures/layouts/views.bld-layout");

std::unique_ptr<core::Map> fixture(QTemporaryDir& dir) {
    auto read = import::readLayoutFile(kFixture, dir.path());
    EXPECT_TRUE(read.ok()) << read.error.toStdString();
    return std::move(read.map);
}

// A panel on a map, applying its edits as the main window does.
struct PanelOnMap {
    QTemporaryDir dir;
    std::unique_ptr<core::Map> map = fixture(dir);
    ui::ViewsPanel panel;
    std::vector<QString> edits;
    std::vector<QString> wentTo;
    int showAll = 0;
    PanelOnMap() {
        QObject::connect(&panel, &ui::ViewsPanel::viewsEdited, [this](const std::vector<core::SavedView>& v, const QString& what) {
            map->sidecar.views = v;
            edits.push_back(what);
            panel.setMap(map.get());
        });
        QObject::connect(&panel, &ui::ViewsPanel::goToViewRequested,
                         [this](const core::SavedView& v) { wentTo.push_back(v.id); });
        QObject::connect(&panel, &ui::ViewsPanel::showEverythingRequested, [this] { ++showAll; });
        panel.setMap(map.get());
    }
    const core::SavedView& view(int i) const { return map->sidecar.views.at(static_cast<size_t>(i)); }
    // The sheets' ids (BlueBrick numbers: the file's "sheet-track" and
    // "sheet-town" are renumbered on reading).
    QString track() const { return map->layers()[0]->guid; }
    QString town() const { return map->layers()[1]->guid; }
    template <typename T> T* child(const QString& name) { return panel.findChild<T*>(name); }
};

}  // namespace

TEST(ViewsPanel, ListsTheViewsAndShowsOneWhenClicked) {
    PanelOnMap t;
    auto* list = t.panel.findChild<QListWidget*>(QStringLiteral("ViewList"));
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->count(), 2);
    EXPECT_EQ(list->item(0)->text(), QStringLiteral("Whole layout"));
    EXPECT_EQ(list->item(1)->data(Qt::UserRole + 1).toString(), QStringLiteral("One area · 1 sheet"));
    EXPECT_FALSE(t.child<QLabel>(QStringLiteral("ViewsEmpty"))->isVisibleTo(&t.panel));
    EXPECT_EQ(t.child<QPushButton>(QStringLiteral("viewExportAll"))->text(), QStringLiteral("Export all views"));

    emit list->itemClicked(list->item(1));
    EXPECT_EQ(t.wentTo, std::vector<QString>{ QStringLiteral("view-station") });
    EXPECT_EQ(t.panel.selectedViewId(), QStringLiteral("view-station"));
    // Its options show what it holds.
    EXPECT_TRUE(t.child<QPushButton>(QStringLiteral("viewArea"))->isChecked());
    EXPECT_FALSE(t.child<QCheckBox>(QStringLiteral("viewGrid"))->isChecked());
    EXPECT_FALSE(t.child<QCheckBox>(QStringLiteral("viewAllSheets"))->isChecked());
    EXPECT_TRUE(t.child<QCheckBox>(QStringLiteral("viewSheet:") + t.town())->isChecked());
    EXPECT_FALSE(t.child<QCheckBox>(QStringLiteral("viewSheet:") + t.track())->isChecked());
    // Looking at a view changes nothing in the layout.
    EXPECT_TRUE(t.edits.empty());

    // No views: a friendly line and one picture of the whole layout.
    t.map->sidecar.views.clear();
    t.panel.setMap(t.map.get());
    EXPECT_TRUE(t.child<QLabel>(QStringLiteral("ViewsEmpty"))->isVisibleTo(&t.panel));
    EXPECT_EQ(t.child<QPushButton>(QStringLiteral("viewExportAll"))->text(), QStringLiteral("Export a picture"));
}

TEST(ViewsPanel, AddsANamedViewThatFitsTheWholeLayout) {
    PanelOnMap t;
    t.panel.setGridShown([] { return true; });
    t.panel.setAskName([](const QString&, const QString& suggestion) -> std::optional<QString> {
        EXPECT_EQ(suggestion, QStringLiteral("View 3"));
        return QStringLiteral("Yard");
    });
    t.child<QPushButton>(QStringLiteral("viewAdd"))->click();
    ASSERT_EQ(t.map->sidecar.views.size(), 3u);
    const core::SavedView& v = t.view(2);
    EXPECT_EQ(v.name, QStringLiteral("Yard"));
    EXPECT_FALSE(v.id.isEmpty());
    EXPECT_TRUE(v.fit);
    EXPECT_FALSE(v.rect);
    EXPECT_FALSE(v.sheets);
    EXPECT_TRUE(v.grid);  // like the screen now
    EXPECT_TRUE(v.labels);
    EXPECT_EQ(t.wentTo.back(), v.id);  // and shows it

    // Cancelled: nothing added.
    t.panel.setAskName([](const QString&, const QString&) -> std::optional<QString> { return std::nullopt; });
    t.child<QPushButton>(QStringLiteral("viewAdd"))->click();
    EXPECT_EQ(t.map->sidecar.views.size(), 3u);
}

TEST(ViewsPanel, ChangesAreaSheetsGridAndLabels) {
    PanelOnMap t;
    t.panel.setScreenRect([] { return std::optional(QRectF(10.123, 20, 30.456, 40)); });
    t.panel.selectView(QStringLiteral("view-whole"));

    t.child<QPushButton>(QStringLiteral("viewArea"))->click();
    EXPECT_FALSE(t.view(0).fit);
    EXPECT_EQ(t.view(0).rect, QRectF(10.12, 20, 30.46, 40));
    t.child<QPushButton>(QStringLiteral("viewFit"))->click();
    EXPECT_TRUE(t.view(0).fit);
    EXPECT_FALSE(t.view(0).rect);
    const size_t before = t.edits.size();
    t.child<QPushButton>(QStringLiteral("viewFit"))->click();  // already: no change, no undo step
    EXPECT_EQ(t.edits.size(), before);

    t.child<QCheckBox>(QStringLiteral("viewGrid"))->click();
    EXPECT_FALSE(t.view(0).grid);
    t.child<QCheckBox>(QStringLiteral("viewLabels"))->click();
    EXPECT_FALSE(t.view(0).labels);

    // Sheets: from all, to a list of every sheet, then one off, then all again.
    t.child<QCheckBox>(QStringLiteral("viewAllSheets"))->click();
    EXPECT_EQ(t.view(0).sheets, (QStringList{ t.track(), t.town() }));
    t.child<QCheckBox>(QStringLiteral("viewSheet:") + t.track())->click();
    EXPECT_EQ(t.view(0).sheets, QStringList{ t.town() });
    t.child<QCheckBox>(QStringLiteral("viewAllSheets"))->click();
    EXPECT_FALSE(t.view(0).sheets);

    t.panel.setAskName([](const QString&, const QString& name) -> std::optional<QString> {
        EXPECT_EQ(name, QStringLiteral("Whole layout"));
        return QStringLiteral("  Everything ");
    });
    t.child<QPushButton>(QStringLiteral("viewRename"))->click();
    EXPECT_EQ(t.view(0).name, QStringLiteral("Everything"));
    // The other view is untouched.
    EXPECT_EQ(t.view(1).rect, QRectF(90, 40, 40, 30));
}

TEST(ViewsPanel, DeleteAsksFirst) {
    PanelOnMap t;
    QString asked;
    bool answer = false;
    t.panel.setConfirm([&](const QString& q) {
        asked = q;
        return answer;
    });
    t.panel.setActiveView(QStringLiteral("view-station"));
    t.child<QPushButton>(QStringLiteral("viewDelete"))->click();
    EXPECT_EQ(asked, QStringLiteral("Delete the view \"Station\"? The layout itself doesn't change."));
    EXPECT_EQ(t.map->sidecar.views.size(), 2u);
    answer = true;
    t.child<QPushButton>(QStringLiteral("viewDelete"))->click();
    ASSERT_EQ(t.map->sidecar.views.size(), 1u);
    EXPECT_EQ(t.view(0).id, QStringLiteral("view-whole"));
    EXPECT_EQ(t.showAll, 1);  // it was being looked at
}

// ---------------------------------------------------------------------------

class SharePicture : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        QSettings().remove(QStringLiteral("views"));
        map_ = fixture(dir_);
        ASSERT_TRUE(map_);
    }
    void TearDown() override {
        QSettings().remove(QStringLiteral("views"));
        QStandardPaths::setTestModeEnabled(false);
    }
    ui::SharePictureDialog::Input input(const QString& choice = {}) {
        ui::SharePictureDialog::Input in;
        in.map = map_.get();
        in.parts = &parts_;
        in.layoutTitle = QStringLiteral("Show");
        in.screen = ui::views::PictureSpec{ QRectF(0, 0, 20, 10), std::nullopt, false, true };
        in.initialChoice = choice;
        return in;
    }
    QTemporaryDir dir_;
    std::unique_ptr<core::Map> map_;
    parts::PartsLibrary parts_;
};

TEST_F(SharePicture, OffersTheWholeLayoutTheScreenAndEachView) {
    ui::SharePictureDialog dlg(input());
    auto* box = dlg.findChild<QComboBox*>(QStringLiteral("pictureOf"));
    ASSERT_NE(box, nullptr);
    QStringList items;
    for (int i = 0; i < box->count(); ++i) items << box->itemText(i);
    EXPECT_EQ(items, (QStringList{ QStringLiteral("Whole layout"), QStringLiteral("What’s on screen"),
                                   QStringLiteral("Whole layout"), QStringLiteral("Station") }));
    // Whole layout at the sharing size: 2x, 16 px per stud.
    EXPECT_EQ(dlg.choice(), QStringLiteral("whole-layout"));
    EXPECT_EQ(dlg.picture().size(), QSize(128 * 16, 88 * 16));
    dlg.choose(ui::SharePictureDialog::screenChoice());
    EXPECT_EQ(dlg.picture().size(), QSize(320, 160));
    dlg.choose(QStringLiteral("view-station"));
    EXPECT_EQ(dlg.picture().size(), QSize(640, 480));
    EXPECT_EQ(dlg.fileName(), QStringLiteral("Show - Station.png"));

    // Opened from a view's row, it starts on that view.
    ui::SharePictureDialog fromRow(input(QStringLiteral("view-station")));
    EXPECT_EQ(fromRow.choice(), QStringLiteral("view-station"));
}

TEST_F(SharePicture, SavesAndCopiesThePicture) {
    ui::SharePictureDialog dlg(input(QStringLiteral("view-station")));
    const QString path = dir_.filePath(QStringLiteral("station.png"));
    ASSERT_TRUE(dlg.savePictureTo(path));
    EXPECT_EQ(QImage(path).size(), QSize(640, 480));
    EXPECT_EQ(dlg.status(), QStringLiteral("Saved as “station.png”."));

    dlg.copyPicture();
    EXPECT_EQ(QGuiApplication::clipboard()->image().size(), QSize(640, 480));
    EXPECT_EQ(dlg.status(), QStringLiteral("Copied. Paste it into a message or a document."));

    EXPECT_FALSE(dlg.savePictureTo(dir_.filePath(QStringLiteral("no/such/folder/x.png"))));
}

TEST_F(SharePicture, NothingToShowDisablesSaveAndCopy) {
    map_->layers().clear();
    map_->sidecar.anchoredLabels.clear();
    ui::SharePictureDialog dlg(input());
    EXPECT_TRUE(dlg.picture().isNull());
    EXPECT_FALSE(dlg.findChild<QPushButton*>(QStringLiteral("pictureSave"))->isEnabled());
    EXPECT_FALSE(dlg.findChild<QPushButton*>(QStringLiteral("pictureCopy"))->isEnabled());
    EXPECT_EQ(dlg.findChild<QLabel*>(QStringLiteral("picturePreview"))->text(), QStringLiteral("There is nothing to show yet."));
}

TEST_F(SharePicture, ExportAllRemembersTheFolderAndSize) {
    const QString out = dir_.filePath(QStringLiteral("pictures"));
    ASSERT_TRUE(QDir().mkpath(out));
    {
        ui::SharePictureDialog dlg(input());
        EXPECT_EQ(dlg.exportScale(), 2.0);  // Medium to start
        dlg.findChild<QPushButton*>(QStringLiteral("size:Small"))->click();
        const auto r = dlg.exportAllTo(out);
        EXPECT_EQ(r.files.size(), 2);
        EXPECT_EQ(QImage(QDir(out).filePath(QStringLiteral("Show - Station.png"))).size(), QSize(320, 240));
        EXPECT_TRUE(dlg.status().startsWith(QStringLiteral("Saved 2 pictures in")));
    }
    const auto prefs = ui::loadExportViewsPrefs();
    ASSERT_EQ(prefs.folder, out);  // else the one-click export below would ask for a folder
    EXPECT_EQ(prefs.scale, 1.0);
    // The next time: the same size, and one click writes to the same folder.
    ui::SharePictureDialog again(input());
    EXPECT_EQ(again.exportScale(), 1.0);
    EXPECT_TRUE(again.findChild<QLabel*>(QStringLiteral("exportFolder"))->text().contains(QDir::toNativeSeparators(out)));
    map_->sidecar.views[1].rect = QRectF(90, 40, 10, 10);
    const QString msg = ui::runExportAllViews(nullptr, *map_, parts_, QStringLiteral("Show"), false);
    EXPECT_FALSE(msg.isEmpty());
    EXPECT_EQ(QImage(QDir(out).filePath(QStringLiteral("Show - Station.png"))).size(), QSize(80, 80));
    EXPECT_EQ(QDir(out).entryList(QDir::Files).size(), 2);
}

// ---------------------------------------------------------------------------

class ViewsInTheWindow : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ui::UpdateCheck::setCheckAtStartupEnabled(false);
        path_ = dir_.filePath(QStringLiteral("show.bld-layout"));
        ASSERT_TRUE(QFile::copy(kFixture, path_));
        window_ = std::make_unique<ui::MainWindow>(parts_);
        window_->resize(1400, 900);
        window_->show();
        ASSERT_TRUE(window_->openFile(path_));
        view_ = window_->findChild<ui::MapView*>();
        panel_ = window_->findChild<ui::ViewsPanel*>();
        ASSERT_TRUE(view_ && panel_);
    }
    void TearDown() override {
        window_.reset();
        QStandardPaths::setTestModeEnabled(false);
    }
    QTemporaryDir dir_;
    QString path_;
    parts::PartsLibrary parts_;
    std::unique_ptr<ui::MainWindow> window_;
    ui::MapView* view_ = nullptr;
    ui::ViewsPanel* panel_ = nullptr;
};

TEST_F(ViewsInTheWindow, ShowingAViewChangesOnlyThisScreen) {
    auto* list = panel_->findChild<QListWidget*>(QStringLiteral("ViewList"));
    ASSERT_EQ(list->count(), 2);
    auto* indicator = window_->findChild<ui::ViewIndicator*>();
    ASSERT_NE(indicator, nullptr);
    EXPECT_FALSE(indicator->isVisible());
    EXPECT_EQ(shownBricks(*view_), 3);

    emit list->itemClicked(list->item(1));  // Station: one sheet, one area
    EXPECT_EQ(shownBricks(*view_), 1);  // the town's only
    ASSERT_TRUE(view_->viewFilter().has_value());
    EXPECT_EQ(view_->viewFilter()->sheets, QStringList{ view_->currentMap()->layers()[1]->guid });
    EXPECT_FALSE(view_->viewFilter()->labels);
    EXPECT_TRUE(indicator->isVisible());
    EXPECT_EQ(indicator->text(), QStringLiteral("Showing <b>Station</b>"));
    // The map shows the saved area.
    const auto screen = view_->screenRectStuds();
    ASSERT_TRUE(screen);
    EXPECT_TRUE(screen->contains(QRectF(90, 40, 40, 30).adjusted(1, 1, -1, -1)));
    EXPECT_LT(screen->width(), 200);
    // The track sheet's bricks are hidden on screen, but the layout is unchanged.
    for (const auto& l : view_->currentMap()->layers()) EXPECT_TRUE(l->visible);
    EXPECT_TRUE(view_->undoStack()->isClean());

    // Changing the view being looked at follows on screen, as one undo step.
    panel_->setLabels(QStringLiteral("view-station"), true);
    EXPECT_TRUE(view_->viewFilter()->labels);
    EXPECT_EQ(view_->undoStack()->count(), 1);
    view_->undoStack()->undo();
    EXPECT_FALSE(view_->currentMap()->sidecar.views[1].labels);
    EXPECT_FALSE(view_->viewFilter()->labels);

    // Show everything: back to the layout as it is.
    indicator->findChild<QPushButton*>(QStringLiteral("indicatorShowAll"))->click();
    EXPECT_FALSE(view_->viewFilter().has_value());
    EXPECT_FALSE(indicator->isVisible());
    EXPECT_TRUE(panel_->activeViewId().isEmpty());
    EXPECT_EQ(shownBricks(*view_), 3);
}

TEST_F(ViewsInTheWindow, DeletingTheViewBeingLookedAtEndsIt) {
    auto* list = panel_->findChild<QListWidget*>(QStringLiteral("ViewList"));
    emit list->itemClicked(list->item(1));
    ASSERT_TRUE(view_->viewFilter());
    panel_->setConfirm([](const QString&) { return true; });
    ASSERT_TRUE(panel_->deleteView(QStringLiteral("view-station")));
    EXPECT_FALSE(view_->viewFilter());
    EXPECT_EQ(view_->currentMap()->sidecar.views.size(), 1u);
    // Undo brings it back (it isn't shown again by itself).
    view_->undoStack()->undo();
    EXPECT_EQ(view_->currentMap()->sidecar.views.size(), 2u);
    EXPECT_EQ(list->count(), 2);

    // Gone some other way (redo here, or someone else on a live layout): it ends too.
    emit list->itemClicked(list->item(1));
    ASSERT_TRUE(view_->viewFilter());
    view_->undoStack()->redo();
    EXPECT_FALSE(view_->viewFilter());
    EXPECT_TRUE(panel_->activeViewId().isEmpty());
}

TEST_F(ViewsInTheWindow, TheBuildTabShowsTheViewsPanelAndFileHasSharePicture) {
    panel_->hide();
    auto* build = window_->findChild<QToolButton*>(QStringLiteral("task.build"));
    ASSERT_NE(build, nullptr);
    build->click();
    EXPECT_TRUE(panel_->isVisible());
    EXPECT_NE(window_->findChild<QAction*>(QStringLiteral("action.sharePicture")), nullptr);
    EXPECT_NE(window_->findChild<QAction*>(QStringLiteral("action.exportAllViews")), nullptr);
    EXPECT_NE(window_->findChild<QAction*>(QStringLiteral("tool.picture")), nullptr);
    // Its header has the "?" for panel.views.
    EXPECT_EQ(panel_->windowTitle(), QStringLiteral("Views"));
}

// Opening another layout ends the view being looked at.
TEST_F(ViewsInTheWindow, OpeningAnotherLayoutEndsTheView) {
    auto* list = panel_->findChild<QListWidget*>(QStringLiteral("ViewList"));
    emit list->itemClicked(list->item(0));
    ASSERT_TRUE(view_->viewFilter());
    ASSERT_TRUE(window_->openFile(path_));
    EXPECT_FALSE(view_->viewFilter());
    EXPECT_TRUE(panel_->activeViewId().isEmpty());
}
