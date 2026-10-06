// Modules and sheets, as the web does them (apps/web/src/editor/moduleSheets.ts):
// adding a module matches its sheets by name and asks "Where should these
// go?" for the rest (never a surprise sheet); saving can put everything on
// one sheet; a module with parts on a hidden sheet gets a sparser dashed
// frame, a note in the Modules panel and a Show button in Edit module.

#include "ui/LayerPanel.h"
#include "ui/MapView.h"
#include "ui/ModuleEditBar.h"
#include "ui/ModulesPanel.h"
#include "ui/SheetChoiceDialog.h"

#include "core/Ids.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "edit/ModuleCommands.h"
#include "edit/ModuleSheets.h"
#include "parts/PartsLibrary.h"
#include "rendering/ModuleLabels.h"
#include "rendering/SceneBuilder.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QListWidget>
#include <QPen>
#include <QPushButton>
#include <QTest>
#include <QTimer>
#include <QUndoStack>

using namespace bld;

namespace {

core::Brick brickAt(const QString& guid, double x) {
    core::Brick b;
    b.guid = guid;
    b.partNumber = QStringLiteral("3001.1");
    b.displayArea = QRectF(x, 0, 4, 2);
    return b;
}

std::unique_ptr<core::LayerBrick> sheet(const QString& guid, const QString& name, std::vector<core::Brick> bricks = {}) {
    auto L = std::make_unique<core::LayerBrick>();
    L->guid = guid;
    L->name = name;
    L->bricks = std::move(bricks);
    return L;
}

// A layout with parts sheets "Track" (bottom) and "Buildings", one part each.
std::unique_ptr<core::Map> layout() {
    auto m = std::make_unique<core::Map>();
    m->layers().push_back(sheet(QStringLiteral("T"), QStringLiteral("Track"), { brickAt(QStringLiteral("t1"), 0) }));
    m->layers().push_back(sheet(QStringLiteral("B"), QStringLiteral("Buildings"), { brickAt(QStringLiteral("b1"), 10) }));
    return m;
}

edit::ImportBbmAsModuleCommand::LayerBatch batch(const QString& name, int n) {
    edit::ImportBbmAsModuleCommand::LayerBatch b;
    b.layerName = name;
    for (int i = 0; i < n; ++i) b.bricks.push_back(brickAt(QString(), 40 + i * 4));
    return b;
}

QStringList partsSheets(const core::Map& m) {
    QStringList out;
    for (const auto& L : m.layers())
        if (L->kind() == core::LayerKind::Brick) out << L->name;
    return out;
}

QString sheetOf(const core::Map& m, const QString& guid) {
    for (const auto& L : m.layers())
        if (L->kind() == core::LayerKind::Brick)
            for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
                if (b.guid == guid) return L->name;
    return {};
}

}  // namespace

TEST(ModuleSheets, MatchesNamesIgnoringCaseAndSpaces) {
    auto m = layout();
    EXPECT_EQ(edit::findPartsSheet(*m, QStringLiteral(" track ")), 0);
    EXPECT_EQ(edit::findPartsSheet(*m, QStringLiteral("BUILDINGS")), 1);
    EXPECT_EQ(edit::findPartsSheet(*m, QStringLiteral("Trees")), -1);
    std::vector<edit::ImportBbmAsModuleCommand::LayerBatch> batches{ batch(QStringLiteral("TRACK"), 2), batch(QStringLiteral("Trees"), 3),
                                                                     batch(QStringLiteral("trees "), 1), batch(QStringLiteral("Empty"), 0) };
    const auto un = edit::unmatchedSheets(*m, batches);
    ASSERT_EQ(un.size(), 1u);
    EXPECT_EQ(un[0].name, QStringLiteral("Trees"));
    EXPECT_EQ(un[0].parts, 4);
    EXPECT_EQ(edit::moduleSheetCount(batches), 2);
}

TEST(ModuleSheets, ThePickedSheetIsWhereNewPartsGo) {
    auto m = layout();
    m->selectedLayerIndex = 0;
    EXPECT_EQ(edit::pickedPartsSheet(*m), 0);
    m->selectedLayerIndex = -1;
    EXPECT_EQ(edit::pickedPartsSheet(*m), 1);  // the top parts sheet
    core::Map none;
    EXPECT_EQ(edit::pickedPartsSheet(none), -1);
}

TEST(ModuleSheets, PutsEverythingOnTheSheetWithTheMostParts) {
    core::Map module;
    module.layers().push_back(sheet(QStringLiteral("a"), QStringLiteral("Track"), { brickAt(QStringLiteral("1"), 0) }));
    module.layers().push_back(sheet(QStringLiteral("b"), QStringLiteral("Buildings"), { brickAt(QStringLiteral("2"), 0), brickAt(QStringLiteral("3"), 4) }));
    module.layers()[1]->transparency = 40;
    EXPECT_EQ(edit::moduleSheetNames(module), (QStringList{ QStringLiteral("Track"), QStringLiteral("Buildings") }));
    EXPECT_EQ(edit::oneSheetName(module), QStringLiteral("Buildings"));
    edit::putOnOneSheet(module);
    ASSERT_EQ(module.layers().size(), 1u);
    EXPECT_EQ(module.layers()[0]->name, QStringLiteral("Buildings"));
    EXPECT_EQ(module.layers()[0]->transparency, 100);
    EXPECT_EQ(static_cast<core::LayerBrick&>(*module.layers()[0]).bricks.size(), 3u);
}

TEST(ModuleSheets, MendsSheetsAnOlderWebSaveMadeSeeThrough) {
    core::Map module;
    module.layers().push_back(sheet(QStringLiteral("module-layer-0"), QStringLiteral("Track")));
    module.layers().push_back(sheet(QStringLiteral("module-layer-1"), QStringLiteral("Faded")));
    module.layers().push_back(sheet(QStringLiteral("mine"), QStringLiteral("Mine")));
    module.layers()[0]->transparency = 0;
    module.layers()[1]->transparency = 40;
    module.layers()[2]->transparency = 0;
    EXPECT_EQ(edit::repairModuleSheets(module), 1);
    EXPECT_EQ(module.layers()[0]->transparency, 100);
    EXPECT_EQ(module.layers()[1]->transparency, 40);
    EXPECT_EQ(module.layers()[2]->transparency, 0);
    EXPECT_EQ(edit::repairModuleSheets(module), 0);
}

TEST(ModuleSheets, ImportGoesWhereTheAnswerSaysAndUndoRedoKeepsIt) {
    auto m = layout();
    std::vector<edit::ImportBbmAsModuleCommand::LayerBatch> batches{ batch(QStringLiteral("track"), 1), batch(QStringLiteral("Trees"), 1),
                                                                     batch(QStringLiteral("Water"), 1), batch(QStringLiteral("water"), 1) };
    edit::sendSheetTo(batches, QStringLiteral("Trees"), QStringLiteral("B"));  // onto Buildings
    edit::sendSheetTo(batches, QStringLiteral("WATER"), QString());           // a new sheet "Water"
    QUndoStack stack;
    auto* cmd = new edit::ImportBbmAsModuleCommand(*m, QString(), QStringLiteral("Park"), std::move(batches));
    stack.push(cmd);
    const auto placed = cmd->placedBricks();
    ASSERT_EQ(placed.size(), 4);
    const auto check = [&] {
        EXPECT_EQ(partsSheets(*m), (QStringList{ QStringLiteral("Track"), QStringLiteral("Buildings"), QStringLiteral("Water") }));
        EXPECT_EQ(sheetOf(*m, placed[0].guid), QStringLiteral("Track"));
        EXPECT_EQ(sheetOf(*m, placed[1].guid), QStringLiteral("Buildings"));
        EXPECT_EQ(sheetOf(*m, placed[2].guid), QStringLiteral("Water"));
        EXPECT_EQ(sheetOf(*m, placed[3].guid), QStringLiteral("Water"));
    };
    check();
    const QString water = m->layers()[2]->guid;
    stack.undo();
    EXPECT_EQ(partsSheets(*m), (QStringList{ QStringLiteral("Track"), QStringLiteral("Buildings") }));
    EXPECT_TRUE(m->sidecar.modules.empty());
    stack.redo();
    check();
    EXPECT_EQ(m->layers()[2]->guid, water);
}

TEST(ModuleSheets, TheHiddenNoteSaysWhatIsHidden) {
    auto m = layout();
    const QSet<QString> both{ QStringLiteral("t1"), QStringLiteral("b1") };
    EXPECT_TRUE(edit::hiddenSheetsNote(edit::moduleSheetsUsed(*m, both)).isEmpty());
    m->layers()[1]->visible = false;
    EXPECT_EQ(edit::hiddenSheetsNote(edit::moduleSheetsUsed(*m, both)), QStringLiteral("1 part is on a hidden sheet"));
    m->layers()[0]->visible = false;
    EXPECT_EQ(edit::hiddenSheetsNote(edit::moduleSheetsUsed(*m, both)), QStringLiteral("hidden: its sheets are hidden"));
    EXPECT_EQ(edit::hiddenSheetsNote(edit::moduleSheetsUsed(*m, { QStringLiteral("t1") })), QStringLiteral("hidden: its sheet is hidden"));
}

namespace {

class ModuleSheetsView : public ::testing::Test {
protected:
    void SetUp() override {
        view_ = std::make_unique<ui::MapView>(parts_);
        view_->resize(800, 500);
        view_->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(view_.get()));
        auto m = layout();
        m->selectedLayerIndex = 0;  // Track is picked
        view_->loadMap(std::move(m));
    }
    core::Map& map() { return *view_->currentMap(); }
    static core::Map module(const QStringList& names) {
        core::Map out;
        for (const QString& n : names) out.layers().push_back(sheet(core::newBbmId(), n, { brickAt(QString(), 0), brickAt(QString(), 4) }));
        return out;
    }
    // Answers the next "Where should these go?" (if one opens): `pick` a choice by its text, then Place or Cancel.
    void answer(bool place, const QString& pick = {}) {
        asked_ = false;
        QTimer::singleShot(0, [this, place, pick] {
            auto* d = qobject_cast<ui::SheetChoiceDialog*>(QApplication::activeModalWidget());
            if (!d) return;
            asked_ = true;
            body_ = d->bodyText();
            auto* box = d->findChild<QComboBox*>(QStringLiteral("sheetChoice"));
            if (box) {
                shownChoice_ = box->currentText();
                for (int i = 0; i < box->count(); ++i) options_ << box->itemText(i);
                if (!pick.isEmpty()) box->setCurrentIndex(box->findText(pick));
            }
            (place ? d->placeButton() : d->cancelButton())->click();
        });
    }
    parts::PartsLibrary parts_;
    std::unique_ptr<ui::MapView> view_;
    bool asked_ = false;
    QString body_, shownChoice_;
    QStringList options_;
};

}  // namespace

TEST_F(ModuleSheetsView, AMissingSheetAsksAndGoesOnThePickedSheet) {
    auto mod = module({ QStringLiteral("Trees") });
    answer(true);
    ASSERT_TRUE(view_->placeModule(mod, QStringLiteral("Park"), QString(), QPointF(400, 400)));
    EXPECT_TRUE(asked_);
    EXPECT_EQ(body_, QStringLiteral("“Park” uses a sheet. Your layout doesn’t have “Trees”."));
    EXPECT_EQ(shownChoice_, QStringLiteral("Track (picked sheet)"));
    EXPECT_EQ(options_, (QStringList{ QStringLiteral("Buildings"), QStringLiteral("Track (picked sheet)"), QStringLiteral("A new sheet named “Trees”") }));
    EXPECT_EQ(partsSheets(map()), (QStringList{ QStringLiteral("Track"), QStringLiteral("Buildings") }));
    EXPECT_EQ(static_cast<core::LayerBrick&>(*map().layers()[0]).bricks.size(), 3u);
    ASSERT_EQ(map().sidecar.modules.size(), 1u);
}

TEST_F(ModuleSheetsView, ANewSheetByItsNameWhenPicked) {
    auto mod = module({ QStringLiteral("track"), QStringLiteral("Trees"), QStringLiteral("Water") });
    answer(true, QStringLiteral("A new sheet named “Trees”"));
    ASSERT_TRUE(view_->placeModule(mod, QStringLiteral("Park"), QString(), QPointF(400, 400)));
    EXPECT_EQ(body_, QStringLiteral("“Park” uses 3 sheets. Your layout doesn’t have “Trees” or “Water”."));
    // Trees: a new sheet; Water: left at the picked sheet; track: matched.
    EXPECT_EQ(partsSheets(map()), (QStringList{ QStringLiteral("Track"), QStringLiteral("Buildings"), QStringLiteral("Trees") }));
    EXPECT_EQ(static_cast<core::LayerBrick&>(*map().layers()[0]).bricks.size(), 5u);
}

TEST_F(ModuleSheetsView, CancelPutsNothingDown) {
    auto mod = module({ QStringLiteral("Trees") });
    answer(false);
    EXPECT_FALSE(view_->placeModule(mod, QStringLiteral("Park"), QString(), QPointF(400, 400)));
    EXPECT_TRUE(asked_);
    EXPECT_EQ(partsSheets(map()), (QStringList{ QStringLiteral("Track"), QStringLiteral("Buildings") }));
    EXPECT_TRUE(map().sidecar.modules.empty());
    EXPECT_EQ(view_->undoStack()->count(), 0);
}

TEST_F(ModuleSheetsView, MatchingSheetsDontAsk) {
    auto mod = module({ QStringLiteral("TRACK"), QStringLiteral("Buildings ") });
    answer(false);  // would cancel, if it asked
    ASSERT_TRUE(view_->placeModule(mod, QStringLiteral("Town"), QString(), QPointF(400, 400)));
    QTest::qWait(10);
    EXPECT_FALSE(asked_);
    EXPECT_EQ(static_cast<core::LayerBrick&>(*map().layers()[1]).bricks.size(), 3u);
}

TEST_F(ModuleSheetsView, AHiddenSheetDashesTheFrameAndEditModuleCanShowIt) {
    auto mod = module({ QStringLiteral("Track"), QStringLiteral("Buildings") });
    ASSERT_TRUE(view_->placeModule(mod, QStringLiteral("Town"), QString(), QPointF(400, 400)));
    const QString id = map().sidecar.modules.front().id;
    const auto frame = [&]() -> QGraphicsRectItem* {
        for (QGraphicsItem* it : view_->scene()->items())
            if (it->data(rendering::kModuleAnnotationRole).toString() == QLatin1String("frame")) return qgraphicsitem_cast<QGraphicsRectItem*>(it);
        return nullptr;
    };
    ASSERT_NE(frame(), nullptr);
    EXPECT_FALSE(frame()->data(rendering::kModulePartlyHiddenRole).toBool());
    const QRectF whole = frame()->rect();

    // Hide Buildings from the Sheets panel.
    ui::LayerPanel sheets;
    ui::ModulesPanel modules;
    QObject::connect(&sheets, &ui::LayerPanel::layerVisibilityChanged, [&] {
        modules.setMap(view_->currentMap());
        view_->refreshModuleEditBar();
    });
    sheets.setMap(view_->currentMap(), view_->builder());
    modules.setMap(view_->currentMap());
    auto* list = sheets.findChild<QListWidget*>();
    ASSERT_NE(list, nullptr);
    view_->setEditingModule(id);
    ASSERT_NE(view_->moduleEditBar(), nullptr);
    EXPECT_EQ(view_->moduleEditBar()->sheetsHint(),
              QStringLiteral("This module uses 2 sheets. New parts go on <b>Track</b> (the picked sheet)."));
    EXPECT_FALSE(view_->moduleEditBar()->showSheetsButton()->isVisibleTo(view_->moduleEditBar()));
    list->item(1)->setCheckState(Qt::Unchecked);

    ASSERT_NE(frame(), nullptr);
    EXPECT_TRUE(frame()->data(rendering::kModulePartlyHiddenRole).toBool());
    const auto dash = frame()->pen().dashPattern();
    ASSERT_EQ(dash.size(), 2);
    EXPECT_NEAR(dash[0] / dash[1], rendering::kModuleFramePartlyHiddenDash[0] / rendering::kModuleFramePartlyHiddenDash[1], 1e-9);
    EXPECT_LT(frame()->rect().width(), whole.width() + 1e-9);
    auto* modList = modules.findChild<QListWidget*>();
    ASSERT_NE(modList, nullptr);
    EXPECT_TRUE(modList->item(0)->text().endsWith(QStringLiteral(" — 2 parts are on a hidden sheet"))) << modList->item(0)->text().toStdString();
    EXPECT_TRUE(view_->moduleEditBar()->sheetsHint().endsWith(QStringLiteral("1 of its sheets is hidden.")));
    QPushButton* show = view_->moduleEditBar()->showSheetsButton();
    EXPECT_EQ(show->text(), QStringLiteral("Show it"));
    show->click();
    EXPECT_TRUE(map().layers()[1]->visible);
    ASSERT_NE(frame(), nullptr);
    EXPECT_FALSE(frame()->data(rendering::kModulePartlyHiddenRole).toBool());
    EXPECT_FALSE(view_->moduleEditBar()->sheetsHint().contains(QStringLiteral("hidden")));
}
