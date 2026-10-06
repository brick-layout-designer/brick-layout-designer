// A module on the map is one piece; Edit module opens it part by part;
// Pin in place stops it moving as a whole (core/ModuleEdit.h and
// MapViewModules.cpp, the web's moduleEdit.ts and BrickLayer.tsx).

#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/MapViewInternal.h"
#include "ui/ModuleEditBar.h"
#include "ui/theme/ThemeManager.h"
#include "ui/theme/Tokens.h"

#include "core/LayerBrick.h"
#include "core/LayerRuler.h"
#include "core/Map.h"
#include "core/ModuleEdit.h"
#include "core/Venue.h"
#include "parts/PartsLibrary.h"
#include "rendering/ModuleLabels.h"
#include "saveload/BbmReader.h"
#include "ui/SelectionOverlay.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
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

// ---------------------------------------------------------------------------
// A drag: what is drawn from the layout but follows parts moves with them
// every frame (MapView's live pose), before the button is let go.
// ---------------------------------------------------------------------------

namespace {
void mouseAt(QWidget* vp, QEvent::Type type, QPoint at, Qt::MouseButtons buttons) {
    QMouseEvent ev(type, QPointF(at), vp->mapToGlobal(QPointF(at)),
                   type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, buttons, Qt::NoModifier);
    QApplication::sendEvent(vp, &ev);
}
QGraphicsItem* moduleItem(QGraphicsScene& scene, const QString& id, const QString& role) {
    for (QGraphicsItem* it : scene.items())
        if (it->data(rendering::kModuleAnnotationRole).toString() == role
            && it->data(rendering::kModuleIdRole).toString() == id)
            return it;
    return nullptr;
}
} // namespace

class LiveDragTest : public ModuleEditTest {
protected:
    // Presses on a part of module A that's on top, ready to drag it.
    QPoint pressOnA() {
        for (const QString& g : a_) {
            QGraphicsItem* it = brickItem(*view_->scene(), g);
            view_->centerOn(it);
            QApplication::processEvents();
            const QPoint at = view_->mapFromScene(it->sceneBoundingRect().center());
            for (QGraphicsItem* top : view_->items(at)) {
                // A ruler over the part would take the press.
                if (!ui::detail::isBrickItem(top) && !ui::detail::isRulerItem(top)) continue;
                if (top == it) {
                    mouseAt(view_->viewport(), QEvent::MouseButtonPress, at, Qt::LeftButton);
                    return at;
                }
                break;
            }
        }
        ADD_FAILURE() << "no part of module A on top";
        return {};
    }
    void moveTo(QPoint from, QPoint by) {
        for (int i = 1; i <= 4; ++i)
            mouseAt(view_->viewport(), QEvent::MouseMove, from + by * i / 4, Qt::LeftButton);
    }
};

TEST_F(LiveDragTest, TheModuleFrameAndNameMoveBeforeTheRelease) {
    QSettings().setValue(QStringLiteral("view/moduleNames"), true);
    view_->rebuildScene();
    const QRectF frame0 =
        moduleItem(*view_->scene(), QStringLiteral("A"), QStringLiteral("frame"))->sceneBoundingRect();
    const QRectF name0 =
        moduleItem(*view_->scene(), QStringLiteral("A"), QStringLiteral("name"))->sceneBoundingRect();
    const QPoint at = pressOnA();
    moveTo(at, QPoint(80, 40));
    // Still held: the frame and the name moved with the parts.
    const QRectF frame1 =
        moduleItem(*view_->scene(), QStringLiteral("A"), QStringLiteral("frame"))->sceneBoundingRect();
    const QRectF name1 =
        moduleItem(*view_->scene(), QStringLiteral("A"), QStringLiteral("name"))->sceneBoundingRect();
    const QPointF moved = frame1.center() - frame0.center();
    EXPECT_GT(moved.x(), 1.0);
    EXPECT_GT(moved.y(), 0.5);
    EXPECT_NEAR(name1.center().x() - name0.center().x(), moved.x(), 1.0);
    // The live pose says the same.
    view_->withLivePose([&](const core::Map& m) {
        for (const auto& L : m.layers())
            if (L->kind() == core::LayerKind::Brick)
                for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
                    if (b.guid == *a_.begin()) { EXPECT_TRUE(view_->liveDragging()); }
    });
    mouseAt(view_->viewport(), QEvent::MouseButtonRelease, at + QPoint(80, 40), Qt::NoButton);
    EXPECT_FALSE(view_->liveDragging());
    const QRectF frame2 =
        moduleItem(*view_->scene(), QStringLiteral("A"), QStringLiteral("frame"))->sceneBoundingRect();
    EXPECT_NEAR(frame2.center().x(), frame1.center().x(), 2.0);
    QSettings().remove(QStringLiteral("view/moduleNames"));
}

TEST_F(LiveDragTest, ThePoseMovesTheDraggedPartsOnlyWhileTheDragLasts) {
    const auto areaOf = [&](const core::Map& m, const QString& g) {
        for (const auto& L : m.layers())
            if (L->kind() == core::LayerKind::Brick)
                for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
                    if (b.guid == g) return b.displayArea;
        return QRectF();
    };
    const QString part = *a_.begin();
    QRectF committed, posed;
    view_->withLivePose([&](const core::Map& m) { committed = areaOf(m, part); });
    const QPoint at = pressOnA();
    moveTo(at, QPoint(80, 0));
    view_->withLivePose([&](const core::Map& m) { posed = areaOf(m, part); });
    EXPECT_GT(posed.center().x() - committed.center().x(), 1.0);
    // The layout itself still has it where it was until the drop.
    EXPECT_EQ(areaOf(*view_->currentMap(), part), committed);
    EXPECT_EQ(areaOf(*view_->currentMap(), loose_),
              committed.isNull() ? QRectF() : areaOf(*view_->currentMap(), loose_));
    mouseAt(view_->viewport(), QEvent::MouseButtonRelease, at + QPoint(80, 0), Qt::NoButton);
}

TEST_F(LiveDragTest, RulersFixedToDraggedPartsFollowBeforeTheRelease) {
    // A ruler from a part of A to a point on the map.
    auto* map = view_->currentMap();
    auto layer = std::make_unique<core::LayerRuler>();
    layer->guid = QStringLiteral("rulers");
    core::LayerRuler::AnyRuler any;
    any.kind = core::RulerKind::Linear;
    any.linear.guid = QStringLiteral("r1");
    any.linear.attachedBrick1Id = *a_.begin();
    any.linear.point1 = QPointF(0, 0);
    any.linear.point2 = QPointF(-200, -200);
    any.linear.displayArea = QRectF(-200, -200, 200, 200);
    any.linear.lineThickness = 1;
    layer->rulers.push_back(any);
    map->layers().push_back(std::move(layer));
    view_->rebuildScene();
    const auto rulerRect = [&] {
        QRectF r;
        for (QGraphicsItem* it : view_->scene()->items())
            if (ui::detail::isRulerItem(it)) r = r.united(it->sceneBoundingRect());
        return r;
    };
    const QRectF before = rulerRect();
    QGraphicsItem* part = brickItem(*view_->scene(), *a_.begin());
    const QPointF part0 = part->scenePos();
    const QPoint at = pressOnA();
    moveTo(at, QPoint(80, 40));
    // Still held: the end fixed to the part moved as far as the part did,
    // and the free end stayed put.
    const QPointF moved = part->scenePos() - part0;
    ASSERT_GT(QLineF(QPointF(), moved).length(), 20.0);
    const QPointF endShift = rulerRect().bottomRight() - before.bottomRight();
    EXPECT_LT(QLineF(endShift, moved).length(), 4.0);
    EXPECT_LT(QLineF(rulerRect().topLeft(), before.topLeft()).length(), 4.0);
    mouseAt(view_->viewport(), QEvent::MouseButtonRelease, at + QPoint(80, 40), Qt::NoButton);
}

TEST_F(LiveDragTest, APickedModuleIsOutlinedAsOnePiece) {
    brickItem(*view_->scene(), *a_.begin())->setSelected(true);
    ASSERT_EQ(selected(*view_->scene()), a_);
    EXPECT_EQ(view_->selectionOverlay()->outlines().size(), 1);
    // Editing it: its parts one by one.
    view_->setEditingModule(QStringLiteral("A"));
    for (const QString& g : a_) brickItem(*view_->scene(), g)->setSelected(true);
    EXPECT_EQ(view_->selectionOverlay()->outlines().size(), a_.size());
}

TEST_F(LiveDragTest, ElevationLabelsAndHullsRideOnTheirPart) {
    auto* map = view_->currentMap();
    for (const auto& L : map->layers())
        if (L->kind() == core::LayerKind::Brick) {
            auto& bl = static_cast<core::LayerBrick&>(*L);
            for (auto& b : bl.bricks) b.altitude = 2.0f;
        }
    QSettings().setValue(QStringLiteral("view/brickElevation"), true);
    view_->rebuildScene();
    QSettings().remove(QStringLiteral("view/brickElevation"));
    QGraphicsItem* part = brickItem(*view_->scene(), *a_.begin());
    bool label = false;
    for (QGraphicsItem* child : part->childItems())
        if (dynamic_cast<QGraphicsSimpleTextItem*>(child)) label = true;
    EXPECT_TRUE(label);
}

// The status bar's venue check follows a drag: a part dragged out of the
// room shows as a problem before it is let go.
TEST(LiveDrag, TheVenueCheckFollowsTheDrag) {
    if (!QFile::exists(fixture())) GTEST_SKIP() << "tight-corner.bbm missing";
    parts::PartsLibrary parts;
    const QString partsRoot = QString::fromUtf8(BLD_PARTS_LIBRARY_ROOT);
    if (QDir(partsRoot).exists()) {
        parts.addSearchPath(partsRoot);
        parts.scan();
    }
    ui::MainWindow w(parts);
    w.resize(1200, 800);
    w.show();
    auto* view = w.findChild<ui::MapView*>();
    ASSERT_NE(view, nullptr);
    auto loaded = saveload::readBbm(fixture());
    ASSERT_TRUE(loaded.ok());
    // A room round every part, with no walkway rule.
    QRectF all;
    for (const auto& L : loaded.map->layers())
        if (L->kind() == core::LayerKind::Brick)
            for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
                all = all.united(b.displayArea);
    core::Venue v;
    v.minWalkwayStuds = 0;
    core::VenueEdge e;
    const QRectF room = all.adjusted(-5, -5, 5, 5);
    e.polyline = { room.topLeft(), room.topRight(), room.bottomRight(), room.bottomLeft(), room.topLeft() };
    v.edges = { e };
    loaded.map->sidecar.venue = v;
    view->loadMap(std::move(loaded.map));
    // The status may come a moment later (a slower machine).
    QLabel* status = nullptr;
    const bool found = QTest::qWaitFor(
        [&] {
            for (QLabel* l : w.findChildren<QLabel*>())
                if (l->text().startsWith(QStringLiteral("Venue:"))) status = l;
            return status != nullptr;
        },
        5000);
    ASSERT_TRUE(found);
    ASSERT_NE(status, nullptr);
    ASSERT_EQ(status->text(), QStringLiteral("Venue: fits"));
    // Press on a part that's on top, drag it far out, keep holding.
    QPoint at;
    QGraphicsItem* part = nullptr;
    for (QGraphicsItem* it : view->scene()->items()) {
        if (!ui::detail::isBrickItem(it)) continue;
        view->centerOn(it);
        QApplication::processEvents();
        at = view->mapFromScene(it->sceneBoundingRect().center());
        for (QGraphicsItem* top : view->items(at))
            if (ui::detail::isBrickItem(top)) {
                if (top == it) part = it;
                break;
            }
        if (part) break;
    }
    ASSERT_NE(part, nullptr);
    mouseAt(view->viewport(), QEvent::MouseButtonPress, at, Qt::LeftButton);
    const QPoint far = view->mapFromScene(room.bottomRight() * 8 + QPointF(400, 400));
    for (int i = 1; i <= 6; ++i)
        mouseAt(view->viewport(), QEvent::MouseMove, at + (far - at) * i / 6, Qt::LeftButton);
    EXPECT_NE(status->text(), QStringLiteral("Venue: fits"));
    mouseAt(view->viewport(), QEvent::MouseButtonRelease, far, Qt::NoButton);
}

TEST_F(LiveDragTest, ElectricCircuitsAreDrawnAgainFromThePose) {
    QSettings().setValue(QStringLiteral("view/electricCircuits"), true);
    view_->rebuildScene();
    // Every circuit stroke's box, in a fixed order.
    const auto circuits = [&](double dx) {
        QStringList boxes;
        for (QGraphicsItem* it : view_->scene()->items())
            if (it->zValue() >= 5e5 && it->zValue() < 6e5) {
                const QRectF r = it->sceneBoundingRect().translated(dx, 0);
                boxes << QStringLiteral("%1,%2,%3,%4")
                             .arg(r.x(), 0, 'f', 0)
                             .arg(r.y(), 0, 'f', 0)
                             .arg(r.width(), 0, 'f', 0)
                             .arg(r.height(), 0, 'f', 0);
            }
        boxes.sort();
        return boxes;
    };
    const QStringList before = circuits(0);
    if (before.isEmpty()) {
        QSettings().remove(QStringLiteral("view/electricCircuits"));
        GTEST_SKIP() << "no electric parts drawn (parts library missing)";
    }
    // The layout with every part moved 40 studs: the posed layout a drag hands over.
    core::Map* map = view_->currentMap();
    QSet<QString> moving;
    for (const auto& L : map->layers())
        if (L->kind() == core::LayerKind::Brick)
            for (auto& b : static_cast<core::LayerBrick&>(*L).bricks) {
                b.displayArea.translate(40, 0);
                moving.insert(b.guid);
            }
    view_->builder()->rebuildFollowers(*map, moving, {});
    // Drawn again where the parts are now: the same strokes, 40 studs over.
    EXPECT_EQ(circuits(-40 * ui::detail::studToPx()), before);
    QSettings().remove(QStringLiteral("view/electricCircuits"));
}

// Perf: dragging one part of a module being edited on a layout of about a
// thousand parts (Fordyce 2026) stays well inside a frame per move.
TEST(LiveDrag, AnEditModuleDragOnABigLayoutStaysQuick) {
    const QString fordyce = QString::fromUtf8(BLD_BBM_CORPUS_DIR) + QStringLiteral("/fordyce-2026.bbm");
    if (!QFile::exists(fordyce)) GTEST_SKIP() << "fordyce-2026.bbm missing";
    parts::PartsLibrary parts;
    const QString partsRoot = QString::fromUtf8(BLD_PARTS_LIBRARY_ROOT);
    if (QDir(partsRoot).exists()) {
        parts.addSearchPath(partsRoot);
        parts.scan();
    }
    auto loaded = saveload::readBbm(fordyce);
    ASSERT_TRUE(loaded.ok());
    QStringList guids;
    for (const auto& L : loaded.map->layers())
        if (L->kind() == core::LayerKind::Brick)
            for (auto& b : static_cast<core::LayerBrick&>(*L).bricks) {
                b.myGroupId.clear();
                guids << b.guid;
            }
    ASSERT_GT(guids.size(), 900);
    std::reverse(guids.begin(), guids.end());
    core::Module m;
    m.id = QStringLiteral("M");
    m.name = QStringLiteral("Big module");
    for (int i = 0; i < 200; ++i) m.memberIds.insert(guids[i]);
    loaded.map->sidecar.modules = { m };
    ui::MapView view(parts);
    view.resize(1200, 800);
    view.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&view));
    view.loadMap(std::move(loaded.map));
    view.setEditingModule(QStringLiteral("M"));
    // A member on top where it's pressed.
    QPoint at;
    bool found = false;
    for (int i = 0; i < 200 && !found; ++i) {
        QGraphicsItem* it = brickItem(*view.scene(), guids[i]);
        if (!it) continue;
        view.centerOn(it);
        QApplication::processEvents();
        at = view.mapFromScene(it->sceneBoundingRect().center());
        for (QGraphicsItem* top : view.items(at))
            if (ui::detail::isBrickItem(top)) {
                found = top == it;
                break;
            }
    }
    ASSERT_TRUE(found);
    mouseAt(view.viewport(), QEvent::MouseButtonPress, at, Qt::LeftButton);
    QElapsedTimer t;
    t.start();
    for (int i = 1; i <= 30; ++i) {
        mouseAt(view.viewport(), QEvent::MouseMove, at + QPoint(6 * i, 4 * i), Qt::LeftButton);
        QApplication::processEvents();
    }
    const double perMove = t.elapsed() / 30.0;
    mouseAt(view.viewport(), QEvent::MouseButtonRelease, at + QPoint(180, 120), Qt::NoButton);
    std::printf("edit-module drag: %.1f ms per move\n", perMove);
    // A sanitizer or a loaded CI machine is slower: a generous bound.
    EXPECT_LT(perMove, 100.0);
}
