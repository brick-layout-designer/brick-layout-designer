// The Views panel, the Share Picture dialog and how the main window shows
// a saved view: only this screen changes, never the layout.

#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/SavedViews.h"
#include "ui/SharePictureDialog.h"
#include "ui/UpdateCheck.h"
#include "ui/ViewsPanel.h"

#include "core/Layer.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "core/Module.h"
#include "rendering/SceneBuilder.h"
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
#include <QAbstractButton>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>
#include <QUndoStack>

#include <algorithm>
#include <cmath>

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
    QAbstractButton* row(const QString& id) { return child<QAbstractButton>(QStringLiteral("viewRow:") + id); }
    QToolButton* editBtn(const QString& id) { return child<QToolButton>(QStringLiteral("viewEdit:") + id); }
    bool optionsShown() { return child<QFrame>(QStringLiteral("ViewOptions"))->isVisibleTo(&panel); }
};

// The view rows in the panel, top to bottom.
QStringList rowNames(QWidget& panel) {
    QList<QAbstractButton*> rows;
    for (auto* b : panel.findChildren<QAbstractButton*>())
        if (b->objectName().startsWith(QLatin1String("viewRow:"))) rows << b;
    std::sort(rows.begin(), rows.end(), [](QWidget* a, QWidget* b) {
        return a->mapTo(a->window(), QPoint()).y() < b->mapTo(b->window(), QPoint()).y();
    });
    QStringList out;
    for (auto* b : rows) out << b->text();
    return out;
}

QAbstractButton* rowOf(QWidget& panel, const QString& id) {
    return panel.findChild<QAbstractButton*>(QStringLiteral("viewRow:") + id);
}

}  // namespace

TEST(ViewsPanel, ListsTheViewsAndShowsOneWhenClicked) {
    PanelOnMap t;
    t.panel.resize(320, 600);
    t.panel.show();
    EXPECT_EQ(rowNames(t.panel), (QStringList{ QStringLiteral("Whole layout"), QStringLiteral("Station") }));
    ASSERT_NE(t.row(QStringLiteral("view-station")), nullptr);
    EXPECT_EQ(t.row(QStringLiteral("view-station"))->accessibleDescription(), QStringLiteral("One area · 1 sheet"));
    EXPECT_FALSE(t.child<QLabel>(QStringLiteral("ViewsEmpty"))->isVisibleTo(&t.panel));
    EXPECT_EQ(t.child<QPushButton>(QStringLiteral("viewExportAll"))->text(), QStringLiteral("Export all views"));
    // Settings stay shut until "Edit".
    EXPECT_FALSE(t.optionsShown());

    t.row(QStringLiteral("view-station"))->click();
    EXPECT_EQ(t.wentTo, std::vector<QString>{ QStringLiteral("view-station") });
    EXPECT_FALSE(t.optionsShown());  // showing a view doesn't open its settings
    // Each row shares a picture of its view.
    QString shared;
    QObject::connect(&t.panel, &ui::ViewsPanel::sharePictureRequested, [&](const QString& id) { shared = id; });
    t.child<QToolButton>(QStringLiteral("viewShare:view-station"))->click();
    EXPECT_EQ(shared, QStringLiteral("view-station"));
    // Its options show what it holds.
    t.editBtn(QStringLiteral("view-station"))->click();
    EXPECT_EQ(t.panel.openViewId(), QStringLiteral("view-station"));
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
    EXPECT_TRUE(rowNames(t.panel).isEmpty());
    EXPECT_TRUE(t.panel.openViewId().isEmpty());
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
    t.panel.openView(QStringLiteral("view-whole"));

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
    t.panel.openView(QStringLiteral("view-station"));
    t.child<QPushButton>(QStringLiteral("viewDelete"))->click();
    EXPECT_EQ(asked, QStringLiteral("Delete the view \"Station\"? The layout itself doesn't change."));
    EXPECT_EQ(t.map->sidecar.views.size(), 2u);
    answer = true;
    t.child<QPushButton>(QStringLiteral("viewDelete"))->click();
    ASSERT_EQ(t.map->sidecar.views.size(), 1u);
    EXPECT_EQ(t.view(0).id, QStringLiteral("view-whole"));
    EXPECT_EQ(t.showAll, 1);  // it was being looked at
    EXPECT_TRUE(t.panel.openViewId().isEmpty());
    EXPECT_FALSE(t.optionsShown());
}

// The settings open from a labelled "Edit" with a chevron, and "Done"
// (or "Edit" again) closes them, as on the web.
TEST(ViewsPanel, EditOpensTheSettingsAndDoneClosesThem) {
    PanelOnMap t;
    t.panel.resize(320, 600);
    t.panel.show();
    QToolButton* edit = t.editBtn(QStringLiteral("view-whole"));
    ASSERT_NE(edit, nullptr);
    EXPECT_EQ(edit->text(), QStringLiteral("Edit"));
    EXPECT_FALSE(edit->icon().isNull());  // the chevron
    EXPECT_EQ(edit->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
    EXPECT_EQ(edit->accessibleName(), QStringLiteral("Change Whole layout"));
    EXPECT_FALSE(edit->isChecked());

    edit->click();
    EXPECT_EQ(t.panel.openViewId(), QStringLiteral("view-whole"));
    EXPECT_TRUE(t.optionsShown());
    // In the open view's card, under its row.
    auto* card = t.child<QFrame>(QStringLiteral("viewCard:view-whole"));
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(t.child<QFrame>(QStringLiteral("ViewOptions"))->parentWidget(), card);
    EXPECT_TRUE(t.editBtn(QStringLiteral("view-whole"))->isChecked());
    EXPECT_EQ(t.editBtn(QStringLiteral("view-whole"))->toolTip(), QStringLiteral("Close the settings for this view"));
    EXPECT_TRUE(t.edits.empty());  // opening them changes nothing
    EXPECT_TRUE(t.wentTo.empty());  // nor shows the view

    // Another view's Edit moves them there.
    t.editBtn(QStringLiteral("view-station"))->click();
    EXPECT_EQ(t.panel.openViewId(), QStringLiteral("view-station"));
    EXPECT_EQ(t.child<QFrame>(QStringLiteral("ViewOptions"))->parentWidget(),
              t.child<QFrame>(QStringLiteral("viewCard:view-station")));

    // Done closes them.
    auto* done = t.child<QPushButton>(QStringLiteral("viewDone"));
    ASSERT_NE(done, nullptr);
    EXPECT_EQ(done->text(), QStringLiteral("Done"));
    done->click();
    EXPECT_TRUE(t.panel.openViewId().isEmpty());
    EXPECT_FALSE(t.optionsShown());

    // Edit again closes them too, and they stay open across an edit.
    t.editBtn(QStringLiteral("view-whole"))->click();
    t.child<QCheckBox>(QStringLiteral("viewGrid"))->click();
    EXPECT_EQ(t.edits.size(), 1u);
    EXPECT_EQ(t.panel.openViewId(), QStringLiteral("view-whole"));
    EXPECT_TRUE(t.optionsShown());
    t.editBtn(QStringLiteral("view-whole"))->click();
    EXPECT_TRUE(t.panel.openViewId().isEmpty());
    EXPECT_FALSE(t.optionsShown());
}

// The view on show says so, and clicking it again stops showing it.
TEST(ViewsPanel, TheViewOnShowSaysSoAndClickingItAgainStops) {
    PanelOnMap t;
    int left = 0;
    QObject::connect(&t.panel, &ui::ViewsPanel::leaveViewRequested, [&] { ++left; });
    const QString onScreen = QStringLiteral("On screen now · click to stop");
    EXPECT_FALSE(t.row(QStringLiteral("view-station"))->accessibleDescription().contains(onScreen));
    t.panel.setActiveView(QStringLiteral("view-station"));
    EXPECT_TRUE(t.row(QStringLiteral("view-station"))->accessibleDescription().contains(onScreen));
    EXPECT_FALSE(t.row(QStringLiteral("view-whole"))->accessibleDescription().contains(onScreen));
    EXPECT_TRUE(t.child<QFrame>(QStringLiteral("viewCard:view-station"))->property("active").toBool());
    // The row is taller for the extra line.
    EXPECT_GT(t.row(QStringLiteral("view-station"))->sizeHint().height(),
              t.row(QStringLiteral("view-whole"))->sizeHint().height());

    t.row(QStringLiteral("view-station"))->click();
    EXPECT_EQ(left, 1);
    EXPECT_TRUE(t.wentTo.empty());
    // Another row shows that view instead.
    t.row(QStringLiteral("view-whole"))->click();
    EXPECT_EQ(left, 1);
    EXPECT_EQ(t.wentTo, std::vector<QString>{ QStringLiteral("view-whole") });
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
        EXPECT_EQ(dlg.exportMaxSide(), 2560);  // Medium to start
        EXPECT_TRUE(dlg.findChild<QPushButton*>(QStringLiteral("size:Medium"))->isChecked());
        dlg.findChild<QPushButton*>(QStringLiteral("size:Small"))->click();
        const auto r = dlg.exportAllTo(out);
        EXPECT_EQ(r.files.size(), 2);
        // The longest side 1280 px.
        EXPECT_EQ(QImage(QDir(out).filePath(QStringLiteral("Show - Whole layout.png"))).size(), QSize(1280, 880));
        EXPECT_EQ(QImage(QDir(out).filePath(QStringLiteral("Show - Station.png"))).size(), QSize(1280, 960));
        EXPECT_TRUE(dlg.status().startsWith(QStringLiteral("Saved 2 pictures in")));
    }
    const auto prefs = ui::loadExportViewsPrefs();
    ASSERT_EQ(prefs.folder, out);  // else the one-click export below would ask for a folder
    EXPECT_EQ(prefs.maxSide, 1280);
    // The next time: the same size, and one click writes to the same folder.
    ui::SharePictureDialog again(input());
    EXPECT_EQ(again.exportMaxSide(), 1280);
    EXPECT_TRUE(again.findChild<QPushButton*>(QStringLiteral("size:Small"))->isChecked());
    EXPECT_TRUE(again.findChild<QLabel*>(QStringLiteral("exportFolder"))->text().contains(QDir::toNativeSeparators(out)));
    map_->sidecar.views[1].rect = QRectF(90, 40, 10, 10);
    const QString msg = ui::runExportAllViews(nullptr, *map_, parts_, QStringLiteral("Show"), false);
    EXPECT_FALSE(msg.isEmpty());
    // 10 x 10 studs: 32 px per stud at most, not 1280 px.
    EXPECT_EQ(QImage(QDir(out).filePath(QStringLiteral("Show - Station.png"))).size(), QSize(320, 320));
    EXPECT_EQ(QDir(out).entryList(QDir::Files).size(), 2);
}

// Sizes were a scale once (1, 2 or 4): a remembered scale, or a size not
// offered, reads as Medium, and saving drops the old key.
TEST_F(SharePicture, AnOldRememberedScaleReadsAsMedium) {
    QSettings s;
    s.setValue(QStringLiteral("views/exportFolder"), dir_.path());
    s.setValue(QStringLiteral("views/exportScale"), 4.0);
    auto prefs = ui::loadExportViewsPrefs();
    EXPECT_EQ(prefs.maxSide, 2560);
    EXPECT_EQ(prefs.folder, dir_.path());
    ui::SharePictureDialog dlg(input());
    EXPECT_EQ(dlg.exportMaxSide(), 2560);
    ui::saveExportViewsPrefs(prefs);
    EXPECT_FALSE(QSettings().contains(QStringLiteral("views/exportScale")));
    EXPECT_EQ(QSettings().value(QStringLiteral("views/exportMaxSide")).toInt(), 2560);

    QSettings().setValue(QStringLiteral("views/exportMaxSide"), 999);
    EXPECT_EQ(ui::loadExportViewsPrefs().maxSide, 2560);
    QSettings().setValue(QStringLiteral("views/exportMaxSide"), 5120);
    EXPECT_EQ(ui::loadExportViewsPrefs().maxSide, 5120);
    ui::SharePictureDialog large(input());
    EXPECT_EQ(large.exportMaxSide(), 5120);
}

// The picture and Export all views fit the whole layout with its module
// frames and names, which sit outside the modules.
TEST_F(SharePicture, TheWholeLayoutTakesInModuleNames) {
    QSettings().setValue(QStringLiteral("view/moduleNames"), true);
    QString member;
    for (const auto& l : map_->layers())
        if (l->kind() == core::LayerKind::Brick && member.isEmpty())
            for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks) {
                member = b.guid;
                break;
            }
    ASSERT_FALSE(member.isEmpty());
    core::Module mod;
    mod.id = QStringLiteral("m1");
    mod.name = QStringLiteral("A rather long module name");
    mod.memberIds.insert(member);
    map_->sidecar.modules.push_back(mod);
    map_->sidecar.views.resize(1);  // the "Whole layout" view
    const auto plain = ui::views::fitRegionStuds(*map_, std::nullopt, true);
    ui::views::PictureRenderer renderer(*map_, parts_);
    const auto region = ui::views::viewRegionStuds(map_->sidecar.views[0], *map_, &renderer.builder());
    ASSERT_TRUE(plain && region);
    ASSERT_NE(*plain, *region);
    ASSERT_TRUE(region->contains(*plain));

    ui::SharePictureDialog dlg(input());
    EXPECT_EQ(dlg.picture().size(), ui::views::pictureSize(*region, ui::views::shareScale(*region)));
    const QString out = dir_.filePath(QStringLiteral("pictures"));
    ASSERT_TRUE(QDir().mkpath(out));
    const auto r = dlg.exportAllTo(out);
    ASSERT_EQ(r.files, QStringList{ QStringLiteral("Show - Whole layout.png") });
    EXPECT_EQ(QImage(QDir(out).filePath(r.files[0])).size(),
              ui::views::pictureSize(*region, ui::views::scaleForSide(*region, 2560)));
    QSettings().remove(QStringLiteral("view/moduleNames"));
}

// Share picture: the longest side 2560 px at most.
TEST_F(SharePicture, KeepsTheLongestSideTo2560) {
    map_->sidecar.views[1].rect = QRectF(0, 0, 400, 100);  // 6400 px at 16 px per stud
    ui::SharePictureDialog dlg(input(QStringLiteral("view-station")));
    EXPECT_EQ(dlg.picture().size(), QSize(2560, 640));
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
    EXPECT_EQ(rowNames(*panel_), (QStringList{ QStringLiteral("Whole layout"), QStringLiteral("Station") }));
    auto* indicator = window_->findChild<ui::ViewIndicator*>();
    ASSERT_NE(indicator, nullptr);
    EXPECT_FALSE(indicator->isVisible());
    EXPECT_EQ(shownBricks(*view_), 3);

    rowOf(*panel_, QStringLiteral("view-station"))->click();  // Station: one sheet, one area
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
    EXPECT_EQ(panel_->activeViewId(), QStringLiteral("view-station"));

    // Show everything: back to the layout as it is.
    indicator->findChild<QPushButton*>(QStringLiteral("indicatorShowAll"))->click();
    EXPECT_FALSE(view_->viewFilter().has_value());
    EXPECT_FALSE(indicator->isVisible());
    EXPECT_TRUE(panel_->activeViewId().isEmpty());
    EXPECT_EQ(shownBricks(*view_), 3);
}

TEST_F(ViewsInTheWindow, DeletingTheViewBeingLookedAtEndsIt) {
    rowOf(*panel_, QStringLiteral("view-station"))->click();
    ASSERT_TRUE(view_->viewFilter());
    panel_->setConfirm([](const QString&) { return true; });
    ASSERT_TRUE(panel_->deleteView(QStringLiteral("view-station")));
    EXPECT_FALSE(view_->viewFilter());
    EXPECT_EQ(view_->currentMap()->sidecar.views.size(), 1u);
    // Undo brings it back (it isn't shown again by itself).
    view_->undoStack()->undo();
    EXPECT_EQ(view_->currentMap()->sidecar.views.size(), 2u);
    EXPECT_EQ(rowNames(*panel_).size(), 2);

    // Gone some other way (redo here, or someone else on a live layout): it ends too.
    rowOf(*panel_, QStringLiteral("view-station"))->click();
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
    rowOf(*panel_, QStringLiteral("view-whole"))->click();
    ASSERT_TRUE(view_->viewFilter());
    ASSERT_TRUE(window_->openFile(path_));
    EXPECT_FALSE(view_->viewFilter());
    EXPECT_TRUE(panel_->activeViewId().isEmpty());
}

// Changing the view on show (sheets, grid, labels, area) changes what
// shows, but never moves or re-fits the map: the user may be on the next
// area already, as on the web.
TEST_F(ViewsInTheWindow, ChangingTheViewOnShowNeverMovesTheMap) {
    rowOf(*panel_, QStringLiteral("view-station"))->click();
    ASSERT_TRUE(view_->viewFilter());
    // The user moves on.
    view_->centerOn(QPointF(-400, -400));
    const auto before = view_->screenRectStuds();
    ASSERT_TRUE(before);
    const auto same = [&] {
        const auto now = view_->screenRectStuds();
        return now && std::abs(now->x() - before->x()) < 0.01 && std::abs(now->y() - before->y()) < 0.01
               && std::abs(now->width() - before->width()) < 0.01;
    };
    const QString id = QStringLiteral("view-station");
    panel_->setLabels(id, true);
    EXPECT_TRUE(view_->viewFilter()->labels);
    EXPECT_TRUE(same());
    panel_->setGrid(id, true);
    EXPECT_TRUE(view_->viewFilter()->grid);
    EXPECT_TRUE(same());
    panel_->setSheets(id, std::nullopt);
    EXPECT_FALSE(view_->viewFilter()->sheets);
    EXPECT_EQ(shownBricks(*view_), 3);
    EXPECT_TRUE(same());
    panel_->setFit(id, true);  // the area becomes the whole layout
    EXPECT_TRUE(view_->currentMap()->sidecar.views[1].fit);
    EXPECT_TRUE(same());
    // Undo neither.
    view_->undoStack()->undo();
    EXPECT_TRUE(same());
    EXPECT_EQ(panel_->activeViewId(), id);
}

// Clicking the view on show again stops showing it, the map where it is.
TEST_F(ViewsInTheWindow, ClickingTheViewOnShowAgainStopsShowingIt) {
    auto* indicator = window_->findChild<ui::ViewIndicator*>();
    rowOf(*panel_, QStringLiteral("view-station"))->click();
    ASSERT_TRUE(view_->viewFilter());
    EXPECT_TRUE(rowOf(*panel_, QStringLiteral("view-station"))
                    ->accessibleDescription()
                    .contains(QStringLiteral("On screen now · click to stop")));
    const auto before = view_->screenRectStuds();
    ASSERT_TRUE(before);

    rowOf(*panel_, QStringLiteral("view-station"))->click();
    EXPECT_FALSE(view_->viewFilter());
    EXPECT_TRUE(panel_->activeViewId().isEmpty());
    EXPECT_FALSE(indicator->isVisible());
    EXPECT_EQ(shownBricks(*view_), 3);
    const auto after = view_->screenRectStuds();
    ASSERT_TRUE(after);
    EXPECT_NEAR(after->x(), before->x(), 0.01);
    EXPECT_NEAR(after->y(), before->y(), 0.01);
    EXPECT_NEAR(after->width(), before->width(), 0.01);
    EXPECT_FALSE(rowOf(*panel_, QStringLiteral("view-station"))
                     ->accessibleDescription()
                     .contains(QStringLiteral("On screen now")));
}

// "Fit whole layout" and Show everything take in module names and frames,
// which sit outside the modules.
TEST_F(ViewsInTheWindow, FittingTakesInModuleNamesAndFrames) {
    QSettings().setValue(QStringLiteral("view/moduleNames"), true);
    core::Map* map = view_->currentMap();
    QString member;
    QRectF memberArea;
    for (const auto& l : map->layers())
        if (l->kind() == core::LayerKind::Brick && member.isEmpty())
            for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks) {
                member = b.guid;
                memberArea = b.displayArea;
                break;
            }
    ASSERT_FALSE(member.isEmpty());
    core::Module mod;
    mod.id = QStringLiteral("m1");
    mod.name = QStringLiteral("A rather long module name");
    mod.memberIds.insert(member);
    map->sidecar.modules.push_back(mod);
    view_->rebuildScene();
    const auto& rects = view_->builder()->moduleAnnotationRects();
    ASSERT_EQ(rects.size(), 1);
    const QRectF px = rects.first().second;
    const QRectF drawn(px.x() / 8, px.y() / 8, px.width() / 8, px.height() / 8);
    // The frame and name (above the module, never wider than it) reach past its parts.
    EXPECT_GT(memberArea.top() - drawn.top(), 2.0);
    EXPECT_LE(drawn.width(), memberArea.width() + 1.0 + 1e-9);

    rowOf(*panel_, QStringLiteral("view-whole"))->click();  // fits the whole layout
    auto screen = view_->screenRectStuds();
    ASSERT_TRUE(screen);
    EXPECT_TRUE(screen->contains(drawn)) << "screen " << screen->x() << "," << screen->y() << " " << screen->width()
                                         << "x" << screen->height() << " drawn " << drawn.x() << "," << drawn.y()
                                         << " " << drawn.width() << "x" << drawn.height();

    view_->centerOn(QPointF(-40000, -40000));
    window_->findChild<ui::ViewIndicator*>()->findChild<QPushButton*>(QStringLiteral("indicatorShowAll"))->click();
    screen = view_->screenRectStuds();
    ASSERT_TRUE(screen);
    EXPECT_TRUE(screen->contains(drawn));
    QSettings().remove(QStringLiteral("view/moduleNames"));
}
