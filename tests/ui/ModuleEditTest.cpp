// A module on the map is one piece; Edit module opens it part by part;
// Pin in place stops it moving as a whole (core/ModuleEdit.h and
// MapViewModules.cpp, the web's moduleEdit.ts and BrickLayer.tsx).

#include "ui/MapView.h"
#include "ui/MapViewInternal.h"
#include "ui/ModuleEditBar.h"
#include "ui/theme/ThemeManager.h"
#include "ui/theme/Tokens.h"

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "core/ModuleEdit.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QPushButton>
#include <QTest>
#include <QUndoStack>

#include <algorithm>

using namespace bld;

namespace {

QString fixture() { return QString::fromUtf8(BLD_BBM_CORPUS_DIR) + QStringLiteral("/tight-corner.bbm"); }

QGraphicsItem* brickItem(QGraphicsScene& scene, const QString& guid) {
    for (QGraphicsItem* it : scene.items())
        if (ui::detail::isBrickItem(it) && it->data(ui::detail::kBrickDataGuid).toString() == guid) return it;
    return nullptr;
}

QSet<QString> selected(QGraphicsScene& scene) {
    QSet<QString> out;
    for (QGraphicsItem* it : scene.selectedItems())
        if (ui::detail::isBrickItem(it)) out.insert(it->data(ui::detail::kBrickDataGuid).toString());
    return out;
}

class ModuleEditTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!QFile::exists(fixture())) GTEST_SKIP() << "tight-corner.bbm missing";
        const QString partsRoot = QString::fromUtf8(BLD_PARTS_LIBRARY_ROOT);
        if (QDir(partsRoot).exists()) {
            parts_.addSearchPath(partsRoot);
            parts_.scan();
        }
        auto loaded = saveload::readBbm(fixture());
        ASSERT_TRUE(loaded.ok());
        // Two modules of three parts each; the second pinned.
        // BlueBrick groups would pick more parts: not here.
        QStringList guids;
        for (const auto& L : loaded.map->layers())
            if (L->kind() == core::LayerKind::Brick)
                for (auto& b : static_cast<core::LayerBrick&>(*L).bricks) {
                    b.myGroupId.clear();
                    guids << b.guid;
                }
        ASSERT_GE(guids.size(), 7);
        // The last parts drawn are on top.
        std::reverse(guids.begin(), guids.end());
        core::Module a;
        a.id = QStringLiteral("A");
        a.name = QStringLiteral("Harbour");
        a.memberIds = { guids[0], guids[1], guids[2] };
        core::Module b;
        b.id = QStringLiteral("B");
        b.name = QStringLiteral("Yard");
        b.memberIds = { guids[3], guids[4], guids[5] };
        b.pinned = true;
        loaded.map->sidecar.modules = { a, b };
        a_ = a.memberIds;
        b_ = b.memberIds;
        loose_ = guids[6];
        view_ = std::make_unique<ui::MapView>(parts_);
        view_->resize(800, 600);
        view_->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(view_.get()));
        view_->loadMap(std::move(loaded.map));
    }

    parts::PartsLibrary parts_;
    std::unique_ptr<ui::MapView> view_;
    QSet<QString> a_, b_;
    QString loose_;
};

}  // namespace

TEST(ModuleEdit, ShapesTheSelectionByModule) {
    std::vector<core::Module> mods(2);
    mods[0].id = QStringLiteral("a");
    mods[0].memberIds = { QStringLiteral("a1"), QStringLiteral("a2") };
    mods[1].id = QStringLiteral("b");
    mods[1].memberIds = { QStringLiteral("b1") };
    mods[1].pinned = true;
    EXPECT_EQ(core::shapeSelection({ QStringLiteral("a2"), QStringLiteral("x") }, mods, {}),
              (QSet<QString>{ QStringLiteral("a1"), QStringLiteral("a2"), QStringLiteral("x") }));
    // Editing: only the edited module's parts.
    EXPECT_EQ(core::shapeSelection({ QStringLiteral("a2"), QStringLiteral("b1"), QStringLiteral("x") }, mods, QStringLiteral("a")),
              (QSet<QString>{ QStringLiteral("a2") }));
    EXPECT_TRUE(core::outsideEdit(QStringLiteral("x"), mods, QStringLiteral("a")));
    EXPECT_FALSE(core::outsideEdit(QStringLiteral("a1"), mods, QStringLiteral("a")));
    EXPECT_FALSE(core::outsideEdit(QStringLiteral("x"), mods, {}));
    // Pinned: not as a whole, but its parts while it's edited.
    EXPECT_EQ(core::pinnedAmong({ QStringLiteral("b1") }, mods, {}), &mods[1]);
    EXPECT_EQ(core::pinnedAmong({ QStringLiteral("b1") }, mods, QStringLiteral("b")), nullptr);
    EXPECT_EQ(core::pinnedAmong({ QStringLiteral("a1") }, mods, {}), nullptr);
    EXPECT_FALSE(core::canDragPart(QStringLiteral("b1"), mods, {}));
    EXPECT_TRUE(core::canDragPart(QStringLiteral("b1"), mods, QStringLiteral("b")));
    EXPECT_FALSE(core::canDragPart(QStringLiteral("x"), mods, QStringLiteral("a")));
}

TEST(ModuleEdit, TellsWhenAPartLeftTheOutlineAndWhoElseIsThere) {
    const QRectF outline(0, 0, 10, 10);
    EXPECT_TRUE(core::outsideOutline(QRectF(10, 0, 2, 2), outline));
    EXPECT_FALSE(core::outsideOutline(QRectF(9, 9, 2, 2), outline));
    EXPECT_TRUE(core::outsideOutline(QRectF(-3, 4, 3, 2), outline));
    EXPECT_TRUE(core::outsideOutline(QRectF(4, -2, 2, 2), outline));
    EXPECT_TRUE(core::outsideOutline(QRectF(4, 10, 2, 2), outline));
    EXPECT_EQ(core::hereTooText({}), QString());
    EXPECT_EQ(core::hereTooText({ QStringLiteral("Sam"), QStringLiteral("Sam") }), QStringLiteral("Sam is here too"));
    EXPECT_EQ(core::hereTooText({ QStringLiteral("Sam"), QStringLiteral("Alex") }), QStringLiteral("Sam and Alex are here too"));
    EXPECT_EQ(core::hereTooText({ QStringLiteral("Sam"), QStringLiteral("Alex"), QStringLiteral("Jo") }),
              QStringLiteral("Sam, Alex and one other are here too"));
    EXPECT_EQ(core::hereTooText({ QStringLiteral("Sam"), QStringLiteral("Alex"), QStringLiteral("Jo"), QStringLiteral("Kim") }),
              QStringLiteral("Sam, Alex and 2 others are here too"));
}

TEST_F(ModuleEditTest, APartPicksTheWholeModuleAndTakingOneOffDropsIt) {
    brickItem(*view_->scene(), *a_.begin())->setSelected(true);
    EXPECT_EQ(selected(*view_->scene()), a_);
    // Taking one part off a whole module takes the module off.
    brickItem(*view_->scene(), *a_.begin())->setSelected(false);
    EXPECT_TRUE(selected(*view_->scene()).isEmpty());
    // A loose part stays on its own.
    brickItem(*view_->scene(), loose_)->setSelected(true);
    EXPECT_EQ(selected(*view_->scene()), QSet<QString>{ loose_ });
}

TEST_F(ModuleEditTest, EditModuleOpensOneModuleAndDoneEscOrAClickOutsideLeave) {
    QString seen = QStringLiteral("unset");
    QObject::connect(view_.get(), &ui::MapView::editingModuleChanged, [&](const QString& id) { seen = id; });
    brickItem(*view_->scene(), loose_)->setSelected(true);
    view_->setEditingModule(QStringLiteral("A"));
    EXPECT_EQ(seen, QStringLiteral("A"));
    EXPECT_TRUE(selected(*view_->scene()).isEmpty());
    // One part at a time inside it; nothing outside it can be picked.
    const QString one = *a_.begin();
    brickItem(*view_->scene(), one)->setSelected(true);
    EXPECT_EQ(selected(*view_->scene()), QSet<QString>{ one });
    QGraphicsItem* outside = brickItem(*view_->scene(), loose_);
    EXPECT_FALSE(outside->flags() & QGraphicsItem::ItemIsSelectable);
    EXPECT_FALSE(outside->flags() & QGraphicsItem::ItemIsMovable);
    EXPECT_LT(outside->opacity(), 0.75);
    // The bar says so, with who else is in it.
    ASSERT_NE(view_->moduleEditBar(), nullptr);
    EXPECT_TRUE(view_->moduleEditBar()->isVisibleTo(view_.get()));
    EXPECT_TRUE(view_->moduleEditBar()->text().contains(QStringLiteral("Harbour")));
    view_->setPeersEditing({ { QStringLiteral("A"), { QStringLiteral("Sam") } } });
    EXPECT_EQ(view_->moduleEditBar()->others(), QStringLiteral("Sam is here too"));
    // Done.
    view_->moduleEditBar()->doneButton()->click();
    EXPECT_EQ(seen, QString());
    EXPECT_TRUE(view_->editingModule().isEmpty());
    EXPECT_TRUE(brickItem(*view_->scene(), loose_)->flags() & QGraphicsItem::ItemIsSelectable);
    // Esc.
    view_->setEditingModule(QStringLiteral("A"));
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(view_.get(), &esc);
    EXPECT_TRUE(view_->editingModule().isEmpty());
    // A click outside the module.
    view_->setEditingModule(QStringLiteral("A"));
    const auto frame = view_->editedModuleFrameStuds();
    ASSERT_TRUE(frame);
    const QPoint far = view_->mapFromScene((frame->bottomRight() + QPointF(200, 200)) * 8);
    QTest::mouseClick(view_->viewport(), Qt::LeftButton, Qt::NoModifier, far);
    EXPECT_TRUE(view_->editingModule().isEmpty());
}

TEST_F(ModuleEditTest, DoubleClickingAPartOpensItsModule) {
    // A part of the module that is on top where it's clicked.
    QPoint at;
    bool found = false;
    for (const QString& g : a_) {
        QGraphicsItem* it = brickItem(*view_->scene(), g);
        view_->centerOn(it);
        QApplication::processEvents();
        at = view_->mapFromScene(it->sceneBoundingRect().center());
        for (QGraphicsItem* top : view_->items(at)) {
            if (!ui::detail::isBrickItem(top)) continue;
            found = top == it;
            break;
        }
        if (found) break;
    }
    ASSERT_TRUE(found);
    QWidget* vp = view_->viewport();
    const auto send = [vp, at](QEvent::Type type, Qt::MouseButtons buttons) {
        QMouseEvent ev(type, QPointF(at), vp->mapToGlobal(QPointF(at)), Qt::LeftButton, buttons, Qt::NoModifier);
        QApplication::sendEvent(vp, &ev);
    };
    send(QEvent::MouseButtonPress, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, Qt::NoButton);
    send(QEvent::MouseButtonDblClick, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, Qt::NoButton);
    EXPECT_EQ(view_->editingModule(), QStringLiteral("A"));
    // The part double-clicked is the one picked.
    EXPECT_EQ(selected(*view_->scene()).size(), 1);
}

TEST_F(ModuleEditTest, NewPartsJoinTheEditedModuleAndASetMeltsIn) {
    view_->setEditingModule(QStringLiteral("A"));
    const int steps = view_->undoStack()->count();
    view_->absorbIntoEditedModule({ loose_ });
    const auto* mod = core::findModule(view_->currentMap()->sidecar.modules, QStringLiteral("A"));
    ASSERT_NE(mod, nullptr);
    EXPECT_TRUE(mod->memberIds.contains(loose_));
    // A module placed meanwhile (all of whose parts are new) melts in.
    core::Module set;
    set.id = QStringLiteral("S");
    set.memberIds = { QStringLiteral("s1"), QStringLiteral("s2") };
    view_->currentMap()->sidecar.modules.push_back(set);
    view_->absorbIntoEditedModule({ QStringLiteral("s1"), QStringLiteral("s2") });
    EXPECT_EQ(core::findModule(view_->currentMap()->sidecar.modules, QStringLiteral("S")), nullptr);
    mod = core::findModule(view_->currentMap()->sidecar.modules, QStringLiteral("A"));
    EXPECT_TRUE(mod->memberIds.contains(QStringLiteral("s2")));
    EXPECT_GT(view_->undoStack()->count(), steps);
    // Not editing: nothing joins, and a placed set stays a module.
    view_->setEditingModule({});
    core::Module set2;
    set2.id = QStringLiteral("S2");
    set2.memberIds = { QStringLiteral("t1") };
    view_->currentMap()->sidecar.modules.push_back(set2);
    view_->absorbIntoEditedModule({ QStringLiteral("t1") });
    EXPECT_NE(core::findModule(view_->currentMap()->sidecar.modules, QStringLiteral("S2")), nullptr);
    view_->absorbIntoEditedModule({ QStringLiteral("zz") });
    EXPECT_FALSE(core::findModule(view_->currentMap()->sidecar.modules, QStringLiteral("A"))->memberIds.contains(QStringLiteral("zz")));
}

TEST_F(ModuleEditTest, APinnedModuleDoesntMoveAsAWholeButItsPartsDoWhileEdited) {
    QGraphicsItem* pinned = brickItem(*view_->scene(), *b_.begin());
    EXPECT_FALSE(pinned->flags() & QGraphicsItem::ItemIsMovable);
    pinned->setSelected(true);
    EXPECT_EQ(selected(*view_->scene()), b_);
    const int steps = view_->undoStack()->count();
    EXPECT_FALSE(view_->selectionMayMove());
    view_->nudgeSelected(1, 0);
    view_->rotateSelected(90);
    EXPECT_EQ(view_->undoStack()->count(), steps);
    // While it's edited, its parts move.
    view_->setEditingModule(QStringLiteral("B"));
    QGraphicsItem* part = brickItem(*view_->scene(), *b_.begin());
    EXPECT_TRUE(part->flags() & QGraphicsItem::ItemIsMovable);
    part->setSelected(true);
    EXPECT_TRUE(view_->selectionMayMove());
    view_->nudgeSelected(1, 0);
    EXPECT_GT(view_->undoStack()->count(), steps);
}

// BLD_MODULE_SHOTS=<dir>: pictures of Edit module in the light and dark
// themes, to look at (not compared).
TEST_F(ModuleEditTest, PicturesForReview) {
    const QString out = qEnvironmentVariable("BLD_MODULE_SHOTS");
    if (out.isEmpty()) GTEST_SKIP() << "BLD_MODULE_SHOTS not set";
    QDir().mkpath(out);
    view_->setEditingModule(QStringLiteral("A"));
    view_->setPeersEditing({ { QStringLiteral("A"), { QStringLiteral("Sam") } } });
    if (const auto f = view_->editedModuleFrameStuds()) view_->centerOn(f->center() * 8);
    for (const auto mode : { ui::theme::Mode::Light, ui::theme::Mode::Dark }) {
        const QPalette pal = ui::theme::buildPalette(mode, ui::theme::accent(QString::fromLatin1(ui::theme::kDefaultAccent)));
        QApplication::setPalette(pal);
        view_->moduleEditBar()->setPalette(pal);
        QApplication::processEvents();
        view_->grab().save(out + (mode == ui::theme::Mode::Dark ? QStringLiteral("/desk-edit-dark.png")
                                                                  : QStringLiteral("/desk-edit-light.png")));
    }
}
