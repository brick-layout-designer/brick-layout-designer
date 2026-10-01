// Touch on the desktop: pinch and two-finger pan on the map, tap to select,
// drag to move (the mouse's own snapping), long press for the menu, the
// touch action bar, trackpad gestures, touch mode and the parts panel.

#include "ui/MapView.h"
#include "ui/MapViewInternal.h"
#include "ui/PartsBrowser.h"
#include "ui/TouchActionBar.h"
#include "ui/TouchMode.h"
#include "ui/theme/AppPrefs.h"
#include "ui/theme/PanelHeader.h"
#include "ui/theme/ThemeManager.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "rendering/SceneBuilder.h"
#include "saveload/BbmReader.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QNativeGestureEvent>
#include <QPointingDevice>
#include <QScroller>
#include <QSignalSpy>
#include <QStyle>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>

#include <cmath>
#include <memory>

using namespace bld;

namespace {

QPointingDevice* touchScreen() {
    static QPointingDevice* dev = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
    return dev;
}

QString fixture() {
    return QString::fromUtf8(BLD_BBM_CORPUS_DIR) + QStringLiteral("/tight-corner.bbm");
}

std::vector<core::Brick> allBricks(core::Map& map) {
    std::vector<core::Brick> out;
    for (const auto& l : map.layers())
        if (l->kind() == core::LayerKind::Brick)
            for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks) out.push_back(b);
    return out;
}

const core::Brick* brickByGuid(core::Map& map, const QString& guid) {
    for (const auto& l : map.layers())
        if (l->kind() == core::LayerKind::Brick)
            for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks)
                if (b.guid == guid) return &b;
    return nullptr;
}

QStringList selectedGuids(QGraphicsScene& scene) {
    QStringList out;
    for (QGraphicsItem* it : scene.selectedItems())
        if (ui::detail::isBrickItem(it)) out.append(it->data(ui::detail::kBrickDataGuid).toString());
    out.sort();
    return out;
}

class TouchTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!QFile::exists(fixture())) GTEST_SKIP() << "tight-corner.bbm missing";
        const QString partsRoot = QString::fromUtf8(BLD_PARTS_LIBRARY_ROOT);
        if (QDir(partsRoot).exists()) {
            parts_.addSearchPath(partsRoot);
            parts_.scan();
        }
        ui::TouchMode::instance().setActive(false);
        view_ = makeView();
    }
    void TearDown() override { ui::TouchMode::instance().setActive(false); }

    std::unique_ptr<ui::MapView> makeView() {
        auto loaded = saveload::readBbm(fixture());
        EXPECT_TRUE(loaded.ok());
        auto v = std::make_unique<ui::MapView>(parts_);
        v->resize(800, 600);
        v->show();
        EXPECT_TRUE(QTest::qWaitForWindowExposed(v.get()));
        v->loadMap(std::move(loaded.map));
        return v;
    }

    // A brick whose centre on screen is that brick (nothing on top).
    QGraphicsItem* reachableBrick(ui::MapView& v, QPoint* at) {
        for (QGraphicsItem* it : v.scene()->items()) {
            if (!ui::detail::isBrickItem(it)) continue;
            const QPoint p = v.mapFromScene(it->sceneBoundingRect().center());
            QGraphicsItem* top = v.itemAt(p);
            while (top && top->parentItem()) top = top->parentItem();
            if (top == it && v.viewport()->rect().contains(p)) {
                *at = p;
                return it;
            }
        }
        return nullptr;
    }

    QPoint emptySpot(ui::MapView& v) {
        for (int y = 10; y < 590; y += 20)
            for (int x = 10; x < 790; x += 20)
                if (!v.itemAt(QPoint(x, y))) return { x, y };
        return {};
    }

    void tap(QWidget* w, QPoint p) {
        QTest::touchEvent(w, touchScreen()).press(0, p);
        QTest::touchEvent(w, touchScreen()).release(0, p);
    }

    // One finger from `from` to `to` in a few steps, then up.
    void touchDrag(QWidget* w, QPoint from, QPoint to) {
        QTest::touchEvent(w, touchScreen()).press(0, from);
        for (int i = 1; i <= 5; ++i)
            QTest::touchEvent(w, touchScreen()).move(0, from + (to - from) * i / 5);
        QTest::touchEvent(w, touchScreen()).release(0, to);
    }

    QPointF sceneAt(ui::MapView& v, QPointF viewportPos) {
        return v.viewportTransform().inverted().map(viewportPos);
    }

    parts::PartsLibrary parts_;
    std::unique_ptr<ui::MapView> view_;
};

TEST_F(TouchTest, PinchZoomsAroundTheFingers) {
    QWidget* vp = view_->viewport();
    const double before = view_->transform().m11();
    const QPoint a(300, 300), b(400, 300);
    const QPointF anchor = sceneAt(*view_, QPointF(350, 300));
    QTest::touchEvent(vp, touchScreen()).press(0, a).press(1, b);
    QTest::touchEvent(vp, touchScreen()).move(0, QPoint(275, 300)).move(1, QPoint(425, 300));
    QTest::touchEvent(vp, touchScreen()).move(0, QPoint(250, 300)).move(1, QPoint(450, 300));
    QTest::touchEvent(vp, touchScreen()).release(0, QPoint(250, 300)).release(1, QPoint(450, 300));

    EXPECT_NEAR(view_->transform().m11() / before, 2.0, 0.01);
    const QPointF now = view_->viewportTransform().map(anchor);
    EXPECT_NEAR(now.x(), 350, 1.5);
    EXPECT_NEAR(now.y(), 300, 1.5);
}

TEST_F(TouchTest, TwoFingersPan) {
    QWidget* vp = view_->viewport();
    const double before = view_->transform().m11();
    const QPointF anchor = sceneAt(*view_, QPointF(350, 300));
    QTest::touchEvent(vp, touchScreen()).press(0, QPoint(300, 300)).press(1, QPoint(400, 300));
    QTest::touchEvent(vp, touchScreen()).move(0, QPoint(330, 320)).move(1, QPoint(430, 320));
    QTest::touchEvent(vp, touchScreen()).move(0, QPoint(360, 340)).move(1, QPoint(460, 340));
    QTest::touchEvent(vp, touchScreen()).release(0, QPoint(360, 340)).release(1, QPoint(460, 340));

    EXPECT_NEAR(view_->transform().m11(), before, 1e-9);
    const QPointF now = view_->viewportTransform().map(anchor);
    EXPECT_NEAR(now.x(), 410, 1.5);
    EXPECT_NEAR(now.y(), 340, 1.5);
}

TEST_F(TouchTest, OneFingerOnEmptyMapPans) {
    const QPoint start = emptySpot(*view_);
    ASSERT_FALSE(start.isNull());
    const QPointF anchor = sceneAt(*view_, start);
    const QPoint end = start + QPoint(start.x() < 400 ? 120 : -120, 0);
    touchDrag(view_->viewport(), start, end);
    const QPointF now = view_->viewportTransform().map(anchor);
    EXPECT_NEAR(now.x(), end.x(), 1.5);
    EXPECT_NEAR(now.y(), end.y(), 1.5);
}

TEST_F(TouchTest, TapSelectsAndTapOnEmptyClears) {
    QPoint at;
    QGraphicsItem* it = reachableBrick(*view_, &at);
    ASSERT_NE(it, nullptr);
    tap(view_->viewport(), at);
    EXPECT_EQ(selectedGuids(*view_->scene()), QStringList{ it->data(ui::detail::kBrickDataGuid).toString() });
    EXPECT_TRUE(ui::TouchMode::instance().active());

    tap(view_->viewport(), emptySpot(*view_));
    EXPECT_TRUE(selectedGuids(*view_->scene()).isEmpty());
}

TEST_F(TouchTest, DragMovesWithTheMousesSnapping) {
    // The same gesture with a finger and with the mouse, on two copies of
    // the layout, lands the part in exactly the same place.
    auto mouseView = makeView();
    for (auto* v : { view_.get(), mouseView.get() }) v->setSnapStepStuds(8.0);
    QPoint at;
    QGraphicsItem* it = reachableBrick(*view_, &at);
    ASSERT_NE(it, nullptr);
    const QString guid = it->data(ui::detail::kBrickDataGuid).toString();
    const QPointF before = brickByGuid(*view_->currentMap(), guid)->displayArea.topLeft();
    // About 13 by 5 studs on screen: off the grid, so the snap shows.
    const double studPx = rendering::SceneBuilder::kPixelsPerStud * view_->transform().m11();
    const QPoint to = at + QPoint(qRound(13.3 * studPx), qRound(5.4 * studPx));

    QTest::mousePress(mouseView->viewport(), Qt::LeftButton, {}, at);
    for (int i = 1; i <= 5; ++i) QTest::mouseMove(mouseView->viewport(), at + (to - at) * i / 5);
    QTest::mouseRelease(mouseView->viewport(), Qt::LeftButton, {}, to);
    // Touches go to the window on top.
    mouseView->hide();

    touchDrag(view_->viewport(), at, to);

    const QPointF touched = brickByGuid(*view_->currentMap(), guid)->displayArea.topLeft();
    const QPointF moused = brickByGuid(*mouseView->currentMap(), guid)->displayArea.topLeft();
    EXPECT_NE(touched, before);
    EXPECT_NEAR(touched.x(), moused.x(), 1e-6);
    EXPECT_NEAR(touched.y(), moused.y(), 1e-6);
    // One undo step, as with the mouse.
    view_->undoStack()->undo();
    EXPECT_EQ(brickByGuid(*view_->currentMap(), guid)->displayArea.topLeft(), before);
}

TEST_F(TouchTest, SecondFingerPutsAMovedPartBack) {
    QPoint at;
    QGraphicsItem* it = reachableBrick(*view_, &at);
    ASSERT_NE(it, nullptr);
    const QString guid = it->data(ui::detail::kBrickDataGuid).toString();
    const QPointF before = brickByGuid(*view_->currentMap(), guid)->displayArea.topLeft();
    QWidget* vp = view_->viewport();
    QTest::touchEvent(vp, touchScreen()).press(0, at);
    QTest::touchEvent(vp, touchScreen()).move(0, at + QPoint(60, 0));
    QTest::touchEvent(vp, touchScreen()).move(0, at + QPoint(60, 0)).press(1, at + QPoint(160, 0));
    QTest::touchEvent(vp, touchScreen()).move(0, at + QPoint(50, 0)).move(1, at + QPoint(170, 0));
    QTest::touchEvent(vp, touchScreen()).release(0, at + QPoint(50, 0)).release(1, at + QPoint(170, 0));
    EXPECT_EQ(brickByGuid(*view_->currentMap(), guid)->displayArea.topLeft(), before);
    EXPECT_EQ(view_->undoStack()->count(), 0);
    QPointF scenePos;
    for (QGraphicsItem* i : view_->scene()->items())
        if (ui::detail::isBrickItem(i) && i->data(ui::detail::kBrickDataGuid).toString() == guid)
            scenePos = i->sceneBoundingRect().center();
    const double px = rendering::SceneBuilder::kPixelsPerStud;
    const QPointF centre = brickByGuid(*view_->currentMap(), guid)->displayArea.center() * px;
    EXPECT_NEAR(scenePos.x(), centre.x(), 1.0);
}

TEST_F(TouchTest, LongPressOpensTheContextMenuWithARing) {
    QPoint at;
    QGraphicsItem* it = reachableBrick(*view_, &at);
    ASSERT_NE(it, nullptr);
    QStringList actions;
    bool ringShown = false;
    QTimer::singleShot(ui::MapView::kLongPressMs / 2, [&] {
        ringShown = view_->longPressRingShown();
    });
    // Closes the menu once it is up (it runs its own loop).
    QTimer closer;
    closer.setInterval(20);
    QObject::connect(&closer, &QTimer::timeout, [&] {
        if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            for (QAction* a : menu->actions()) actions << a->text();
            menu->close();
            closer.stop();
        }
    });
    closer.start();
    QTest::touchEvent(view_->viewport(), touchScreen()).press(0, at);
    QTest::qWait(ui::MapView::kLongPressMs + 300);
    QTest::touchEvent(view_->viewport(), touchScreen()).release(0, at);
    EXPECT_TRUE(ringShown);
    EXPECT_TRUE(actions.contains(QStringLiteral("Rotate CW"))) << actions.join(u',').toStdString();
    EXPECT_EQ(selectedGuids(*view_->scene()), QStringList{ it->data(ui::detail::kBrickDataGuid).toString() });
}

TEST_F(TouchTest, ActionBarActsThroughUndoableCommands) {
    QPoint at;
    QGraphicsItem* it = reachableBrick(*view_, &at);
    ASSERT_NE(it, nullptr);
    const QString guid = it->data(ui::detail::kBrickDataGuid).toString();
    tap(view_->viewport(), at);
    ui::TouchActionBar* bar = view_->touchActionBar();
    ASSERT_NE(bar, nullptr);
    EXPECT_TRUE(bar->isVisible());
    EXPECT_EQ(bar->mode(), ui::TouchActionBar::Mode::Selection);
    auto* map = view_->currentMap();
    const float angle = brickByGuid(*map, guid)->orientation;
    const size_t count = allBricks(*map).size();

    bar->button(QStringLiteral("touchRotateRight"))->click();
    EXPECT_NEAR(std::fmod(brickByGuid(*map, guid)->orientation - angle + 360.0, 360.0), 90.0, 1e-3);
    view_->undoStack()->undo();
    EXPECT_FLOAT_EQ(brickByGuid(*map, guid)->orientation, angle);

    bar->button(QStringLiteral("touchRotateLeft"))->click();
    EXPECT_NEAR(std::fmod(brickByGuid(*map, guid)->orientation - angle + 360.0, 360.0), 270.0, 1e-3);
    view_->undoStack()->undo();

    bar->button(QStringLiteral("touchDuplicate"))->click();
    EXPECT_EQ(allBricks(*map).size(), count + 1);
    view_->undoStack()->undo();
    EXPECT_EQ(allBricks(*map).size(), count);

    // The copy was selected, and went with the undo.
    tap(view_->viewport(), at);
    ASSERT_EQ(selectedGuids(*view_->scene()), QStringList{ guid });
    bar->button(QStringLiteral("touchDelete"))->click();
    EXPECT_EQ(allBricks(*map).size(), count - 1);
    EXPECT_EQ(brickByGuid(*map, guid), nullptr);
    // Delete hands the selection to a connected neighbour, so the bar stays.
    EXPECT_EQ(bar->mode(), selectedGuids(*view_->scene()).isEmpty() ? ui::TouchActionBar::Mode::Idle
                                                                    : ui::TouchActionBar::Mode::Selection);
    bar->button(QStringLiteral("touchUndo"))->click();
    EXPECT_NE(brickByGuid(*map, guid), nullptr);
    bar->button(QStringLiteral("touchRedo"))->click();
    EXPECT_EQ(brickByGuid(*map, guid), nullptr);
    bar->button(QStringLiteral("touchUndo"))->click();

    view_->scene()->clearSelection();
    tap(view_->viewport(), at);
    ASSERT_FALSE(selectedGuids(*view_->scene()).isEmpty());
    bar->button(QStringLiteral("touchDone"))->click();
    EXPECT_TRUE(selectedGuids(*view_->scene()).isEmpty());
    EXPECT_EQ(bar->mode(), ui::TouchActionBar::Mode::Idle);

    // A mouse again: the bar goes.
    ui::TouchMode::instance().setActive(false);
    EXPECT_FALSE(bar->isVisible());
}

TEST_F(TouchTest, TrackpadPinchAndPanGestures) {
    auto* pad = QTest::createTouchDevice(QInputDevice::DeviceType::TouchPad);
    QWidget* vp = view_->viewport();
    const double before = view_->transform().m11();
    const QPointF at(200, 150);
    const QPointF anchor = sceneAt(*view_, at);
    auto send = [&](Qt::NativeGestureType type, double value, QPointF delta) {
        QNativeGestureEvent ev(type, pad, 2, at, at, vp->mapToGlobal(at), value, delta);
        QApplication::sendEvent(vp, &ev);
        return ev.isAccepted();
    };
    EXPECT_TRUE(send(Qt::BeginNativeGesture, 0, {}));
    EXPECT_TRUE(send(Qt::ZoomNativeGesture, 0.25, {}));
    EXPECT_TRUE(send(Qt::ZoomNativeGesture, 0.2, {}));
    EXPECT_TRUE(send(Qt::EndNativeGesture, 0, {}));
    EXPECT_NEAR(view_->transform().m11() / before, 1.5, 1e-6);
    QPointF now = view_->viewportTransform().map(anchor);
    EXPECT_NEAR(now.x(), at.x(), 1.5);
    EXPECT_NEAR(now.y(), at.y(), 1.5);

    EXPECT_TRUE(send(Qt::PanNativeGesture, 0, QPointF(40, -25)));
    now = view_->viewportTransform().map(anchor);
    EXPECT_NEAR(now.x(), at.x() + 40, 1.5);
    EXPECT_NEAR(now.y(), at.y() - 25, 1.5);
    // Not touch mode: a trackpad is not a touchscreen.
    EXPECT_FALSE(ui::TouchMode::instance().active());
}

TEST_F(TouchTest, TapAPartThenTheMapPlacesIt) {
    const auto all = parts_.keys();
    if (all.isEmpty()) GTEST_SKIP() << "no parts library";
    const QString key = all.first();
    ui::TouchMode::instance().setActive(true);
    view_->armPartPlacement(key);
    ASSERT_NE(view_->touchActionBar(), nullptr);
    EXPECT_EQ(view_->touchActionBar()->mode(), ui::TouchActionBar::Mode::Placing);
    const size_t count = allBricks(*view_->currentMap()).size();
    tap(view_->viewport(), emptySpot(*view_));
    const size_t placed = allBricks(*view_->currentMap()).size();
    EXPECT_GT(placed, count);
    EXPECT_TRUE(view_->armedPart().isEmpty());
    EXPECT_NE(view_->touchActionBar()->mode(), ui::TouchActionBar::Mode::Placing);

    // Dragged out of the panel by a finger: dropped on the map, it lands.
    const QPoint inside = view_->viewport()->mapToGlobal(emptySpot(*view_));
    view_->touchPartDragTo(key, inside);
    EXPECT_TRUE(view_->touchPartDropAt(key, inside));
    const size_t dropped = allBricks(*view_->currentMap()).size();
    EXPECT_EQ(dropped - placed, placed - count);
    EXPECT_FALSE(view_->touchPartDropAt(key, view_->viewport()->mapToGlobal(QPoint(-50, -50))));
    EXPECT_EQ(allBricks(*view_->currentMap()).size(), dropped);
}

TEST_F(TouchTest, PartsPanelTapAndSidewaysDrag) {
    if (parts_.keys().isEmpty()) GTEST_SKIP() << "no parts library";
    ui::PartsBrowser browser(parts_);
    browser.resize(400, 600);
    browser.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&browser));
    browser.rebuild();
    QListWidget* grid = browser.grid();
    QListWidgetItem* first = nullptr;
    for (int i = 0; i < grid->count() && !first; ++i)
        if (!grid->item(i)->isHidden()) first = grid->item(i);
    ASSERT_NE(first, nullptr);
    const QPoint at = grid->visualItemRect(first).center();
    QSignalSpy tapped(&browser, &ui::PartsBrowser::partTapped);
    QSignalSpy moved(&browser, &ui::PartsBrowser::touchDragMoved);
    QSignalSpy dropped(&browser, &ui::PartsBrowser::touchDragDropped);
    tap(grid->viewport(), at);
    ASSERT_EQ(tapped.count(), 1);
    touchDrag(grid->viewport(), at, at + QPoint(150, 10));
    EXPECT_EQ(tapped.count(), 1);
    EXPECT_GE(moved.count(), 1);
    ASSERT_EQ(dropped.count(), 1);
    EXPECT_EQ(dropped.first().at(0).toString(), tapped.first().at(0).toString());
    // Up and down scrolls instead.
    touchDrag(grid->viewport(), at, at + QPoint(0, -120));
    EXPECT_EQ(dropped.count(), 1);
    EXPECT_EQ(tapped.count(), 1);
}

TEST(TouchModeTest, FollowsTheLastInput) {
    ui::TouchMode& mode = ui::TouchMode::instance();
    mode.install();
    mode.setActive(false);
    QWidget w;
    w.setAttribute(Qt::WA_AcceptTouchEvents);
    w.resize(200, 200);
    w.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&w));
    QSignalSpy changed(&mode, &ui::TouchMode::changed);
    QTest::touchEvent(&w, touchScreen()).press(0, QPoint(50, 50));
    QTest::touchEvent(&w, touchScreen()).release(0, QPoint(50, 50));
    EXPECT_TRUE(mode.active());
    QTest::mouseClick(&w, Qt::LeftButton, {}, QPoint(60, 60));
    EXPECT_FALSE(mode.active());
    EXPECT_EQ(changed.count(), 2);
}

TEST(TouchModeTest, BiggerDividersHeadersAndMenus) {
    ui::TouchMode& mode = ui::TouchMode::instance();
    mode.setActive(false);
    QMainWindow mw;
    auto* dock = new QDockWidget(QStringLiteral("Parts"), &mw);
    dock->setWidget(new QWidget);
    mw.addDockWidget(Qt::LeftDockWidgetArea, dock);
    auto* header = ui::theme::PanelHeader::install(dock, {}, ui::theme::PrefsStore::instance());
    mw.setCentralWidget(new QWidget);
    mw.resize(800, 600);
    mw.show();
    const QString saved = qApp->styleSheet();
    const auto& accent = ui::theme::accent(QString());

    qApp->setStyleSheet(ui::theme::buildStyleSheet(ui::theme::Mode::Light, accent, false));
    EXPECT_LT(mw.style()->pixelMetric(QStyle::PM_DockWidgetSeparatorExtent, nullptr, &mw), ui::TouchMode::kMinTarget);
    EXPECT_LT(header->menuButton()->minimumSizeHint().height(), ui::theme::PanelHeader::kTouchButton);

    mode.setActive(true);
    qApp->setStyleSheet(ui::theme::buildStyleSheet(ui::theme::Mode::Light, accent, true));
    EXPECT_GE(mw.style()->pixelMetric(QStyle::PM_DockWidgetSeparatorExtent, nullptr, &mw), ui::TouchMode::kMinTarget);
    EXPECT_GE(header->menuButton()->minimumSize().height(), ui::TouchMode::kMinTarget);
    EXPECT_GE(header->menuButton()->minimumSize().width(), ui::TouchMode::kMinTarget);
    EXPECT_GE(header->sizeHint().height(), ui::TouchMode::kMinTarget);

    mode.setActive(false);
    EXPECT_EQ(header->menuButton()->minimumSize(), QSize(0, 0));
    qApp->setStyleSheet(saved);
}

TEST(TouchModeTest, ScrollAreasFlickByTouch) {
    ui::TouchMode::instance().install();
    QListWidget list;
    for (int i = 0; i < 200; ++i) list.addItem(QStringLiteral("Row %1").arg(i));
    list.resize(200, 300);
    list.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&list));
    EXPECT_TRUE(QScroller::hasScroller(list.viewport()));
    EXPECT_EQ(list.verticalScrollMode(), QAbstractItemView::ScrollPerPixel);
}

}  // namespace
