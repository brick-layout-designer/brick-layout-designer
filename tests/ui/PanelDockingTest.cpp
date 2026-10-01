// Panels move like the web's: the "⋯" menu on each panel's header (same
// words as the web's PanelHost / FloatingPanel), the Panels menu, stacked
// panels' dividers, and the layout kept across restarts.

#include "ui/MainWindow.h"
#include "ui/UpdateCheck.h"
#include "ui/theme/AppPrefs.h"
#include "ui/theme/PanelHeader.h"

#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QToolButton>

#include <array>
#include <memory>

using namespace bld;
using ui::theme::PanelHeader;
using ui::theme::PanelPlace;

namespace {

QStringList labels(const QList<PanelHeader::Target>& targets) {
    QStringList out;
    for (const auto& t : targets) out << t.label;
    return out;
}

// Picks `label` in the panel's "⋯" menu, as a click would.
bool pick(QDockWidget* dock, const QString& label) {
    auto* header = qobject_cast<PanelHeader*>(dock->titleBarWidget());
    if (!header) return false;
    emit header->menu()->aboutToShow();
    for (QAction* a : header->menu()->actions())
        if (a->text() == label) {
            a->trigger();
            QCoreApplication::processEvents();  // the move runs once the menu has closed
            return true;
        }
    return false;
}

// A window with a map in the middle and two panels on the right; the left
// side is empty.
class PanelMenu : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        window_.setCentralWidget(new QLabel(QStringLiteral("map")));
        for (int i = 0; i < 2; ++i) {
            auto* d = new QDockWidget(QStringLiteral("Panel %1").arg(i), &window_);
            d->setObjectName(QStringLiteral("dock.%1").arg(i));
            d->setWidget(new QLabel(QStringLiteral("body")));
            PanelHeader::install(d, {}, ui::theme::PrefsStore::instance());
            window_.addDockWidget(Qt::RightDockWidgetArea, d);
            docks_[i] = d;
        }
        window_.resize(1200, 800);
        window_.show();
        QCoreApplication::processEvents();
    }
    void TearDown() override { QStandardPaths::setTestModeEnabled(false); }

    QMainWindow window_;
    QDockWidget* docks_[2] = {};
};

}  // namespace

TEST(PanelTargets, SameWordsAsTheWeb) {
    // PanelHost.tsx: the places a docked panel is not in.
    EXPECT_EQ(labels(PanelHeader::targets(PanelPlace::Left)),
              (QStringList{ "Move to right", "Float panel", "Hide panel" }));
    EXPECT_EQ(labels(PanelHeader::targets(PanelPlace::Right)),
              (QStringList{ "Move to left", "Float panel", "Hide panel" }));
    // FloatingPanel.tsx.
    EXPECT_EQ(labels(PanelHeader::targets(PanelPlace::Float)),
              (QStringList{ "Dock to left", "Dock to right", "Hide panel" }));
    // A top or bottom dock from an old saved layout can go anywhere.
    EXPECT_EQ(labels(PanelHeader::targets(PanelPlace::Elsewhere)),
              (QStringList{ "Move to left", "Move to right", "Float panel", "Hide panel" }));
}

TEST_F(PanelMenu, HeaderMenuFollowsWhereThePanelIs) {
    auto* header = qobject_cast<PanelHeader*>(docks_[0]->titleBarWidget());
    ASSERT_NE(header, nullptr);
    EXPECT_EQ(header->menuButton()->menu(), header->menu());
    emit header->menu()->aboutToShow();
    QStringList shown;
    for (QAction* a : header->menu()->actions()) shown << a->text();
    EXPECT_EQ(shown, (QStringList{ "Move to left", "Float panel", "Hide panel" }));
    // Only the two sides the web has.
    EXPECT_EQ(docks_[0]->allowedAreas(), Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
}

TEST_F(PanelMenu, MoveOntoAnEmptySideWorksEveryTime) {
    QDockWidget* d = docks_[0];
    for (int round = 0; round < 3; ++round) {
        ASSERT_TRUE(pick(d, QStringLiteral("Move to left"))) << round;
        EXPECT_EQ(window_.dockWidgetArea(d), Qt::LeftDockWidgetArea) << round;
        EXPECT_EQ(PanelHeader::placeOf(d), PanelPlace::Left) << round;
        EXPECT_TRUE(d->isVisible()) << round;
        // Wide enough to use, not squeezed to nothing.
        QCoreApplication::processEvents();
        EXPECT_GE(d->width(), 180) << round;
        ASSERT_TRUE(pick(d, QStringLiteral("Move to right"))) << round;
        EXPECT_EQ(PanelHeader::placeOf(d), PanelPlace::Right) << round;
    }
    // Both panels to the left leave the right side empty; back again.
    ASSERT_TRUE(pick(docks_[0], QStringLiteral("Move to left")));
    ASSERT_TRUE(pick(docks_[1], QStringLiteral("Move to left")));
    EXPECT_EQ(window_.dockWidgetArea(docks_[1]), Qt::LeftDockWidgetArea);
    ASSERT_TRUE(pick(docks_[1], QStringLiteral("Move to right")));
    EXPECT_EQ(window_.dockWidgetArea(docks_[1]), Qt::RightDockWidgetArea);
    EXPECT_TRUE(docks_[1]->isVisible());
}

TEST_F(PanelMenu, FloatDockAndHide) {
    QDockWidget* d = docks_[0];
    ASSERT_TRUE(pick(d, QStringLiteral("Float panel")));
    EXPECT_TRUE(d->isFloating());
    EXPECT_EQ(PanelHeader::placeOf(d), PanelPlace::Float);
    // A floating panel docks onto the empty left side.
    ASSERT_TRUE(pick(d, QStringLiteral("Dock to left")));
    EXPECT_FALSE(d->isFloating());
    EXPECT_EQ(window_.dockWidgetArea(d), Qt::LeftDockWidgetArea);
    EXPECT_TRUE(d->isVisible());

    ASSERT_TRUE(pick(d, QStringLiteral("Hide panel")));
    EXPECT_TRUE(d->isHidden());
    EXPECT_EQ(PanelHeader::placeOf(d), PanelPlace::Hidden);
    // The Panels menu brings it back on the right, like the web.
    PanelHeader::setShown(d, true);
    EXPECT_FALSE(d->isHidden());
    EXPECT_EQ(window_.dockWidgetArea(d), Qt::RightDockWidgetArea);
    PanelHeader::setShown(d, false);
    EXPECT_TRUE(d->isHidden());
}

// Dragging the divider between two stacked panels changes only those two
// (the web's splitterHeights, #137). QMainWindow's own separator does
// this; the test keeps it that way.
TEST(PanelDivider, ResizesOnlyItsTwoNeighbours) {
    QMainWindow window;
    window.setCentralWidget(new QLabel(QStringLiteral("map")));
    QDockWidget* docks[3] = {};
    for (int i = 0; i < 3; ++i) {
        docks[i] = new QDockWidget(QStringLiteral("Panel %1").arg(i), &window);
        docks[i]->setObjectName(QStringLiteral("dock.%1").arg(i));
        docks[i]->setWidget(new QLabel(QStringLiteral("body")));
        window.addDockWidget(Qt::RightDockWidgetArea, docks[i]);
    }
    window.resize(1200, 900);
    window.show();
    QCoreApplication::processEvents();

    const auto heights = [&] {
        return std::array<int, 3>{ docks[0]->height(), docks[1]->height(), docks[2]->height() };
    };
    for (int below = 0; below < 2; ++below) {
        const auto before = heights();
        const QRect upper = docks[below]->geometry();
        const QRect lower = docks[below + 1]->geometry();
        ASSERT_LT(upper.bottom(), lower.top());
        const QPoint grip(upper.center().x(), (upper.bottom() + lower.top() + 1) / 2);
        QTest::mousePress(&window, Qt::LeftButton, {}, grip);
        // QMainWindow moves the divider on a zero timer after each move.
        QTest::mouseMove(&window, grip + QPoint(0, 10));
        QCoreApplication::processEvents();
        QTest::mouseMove(&window, grip + QPoint(0, 40));
        QCoreApplication::processEvents();
        QTest::mouseRelease(&window, Qt::LeftButton, {}, grip + QPoint(0, 40));
        QCoreApplication::processEvents();
        const auto after = heights();
        EXPECT_EQ(after[below], before[below] + 40) << "divider " << below;
        EXPECT_EQ(after[below + 1], before[below + 1] - 40) << "divider " << below;
        const int other = below == 0 ? 2 : 0;
        EXPECT_EQ(after[other], before[other]) << "divider " << below;
    }
}

namespace {

// The app's own window: its Panels menu, first layout and saved layout.
class PanelsInMainWindow : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ui::UpdateCheck::setCheckAtStartupEnabled(false);
        QSettings().remove(QStringLiteral("ui"));
        window_ = std::make_unique<ui::MainWindow>(parts_);
    }
    void TearDown() override {
        window_.reset();
        QSettings().remove(QStringLiteral("ui"));
        QStandardPaths::setTestModeEnabled(false);
    }
    QDockWidget* dock(const char* name) const {
        return window_->findChild<QDockWidget*>(QString::fromLatin1(name));
    }

    parts::PartsLibrary parts_;
    std::unique_ptr<ui::MainWindow> window_;
};

}  // namespace

TEST_F(PanelsInMainWindow, PanelsMenuHasTheWebsListAndFirstLayout) {
    auto* menu = window_->findChild<QMenu*>(QStringLiteral("PanelsMenu"));
    ASSERT_NE(menu, nullptr);
    QStringList names;
    for (QAction* a : menu->actions()) names << a->text();
    // The web's PANEL_TITLES, in its order.
    EXPECT_EQ(names, (QStringList{ "Parts", "Sheets", "Views", "Parts list", "Modules", "Module library",
                                   "Room library" }));
    // The toolbar's Panels button opens the same menu.
    auto* button = window_->findChild<QToolButton*>(QStringLiteral("tool.panels"));
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->menu(), menu);

    // The web's first layout: Parts and Sheets on the right, the rest hidden.
    EXPECT_EQ(window_->dockWidgetArea(dock("dock.parts")), Qt::RightDockWidgetArea);
    EXPECT_EQ(window_->dockWidgetArea(dock("dock.layers")), Qt::RightDockWidgetArea);
    EXPECT_FALSE(dock("dock.parts")->isHidden());
    EXPECT_FALSE(dock("dock.layers")->isHidden());
    for (const char* hidden : { "dock.views", "dock.partUsage", "dock.modules", "dock.moduleLibrary",
                                "dock.venueLibrary" })
        EXPECT_TRUE(dock(hidden)->isHidden()) << hidden;

    // Ticking a hidden panel brings it back on the right; unticking hides it.
    window_->show();
    QCoreApplication::processEvents();
    QDockWidget* modules = dock("dock.modules");
    emit menu->aboutToShow();
    QAction* tick = menu->actions().at(4);
    EXPECT_FALSE(tick->isChecked());
    tick->trigger();
    QCoreApplication::processEvents();
    EXPECT_FALSE(modules->isHidden());
    EXPECT_EQ(window_->dockWidgetArea(modules), Qt::RightDockWidgetArea);
    emit menu->aboutToShow();
    EXPECT_TRUE(tick->isChecked());
    tick->trigger();
    QCoreApplication::processEvents();
    EXPECT_TRUE(modules->isHidden());
}

TEST_F(PanelsInMainWindow, MovedPanelsStayMovedAfterARestart) {
    window_->show();
    QCoreApplication::processEvents();
    ASSERT_TRUE(pick(dock("dock.parts"), QStringLiteral("Move to left")));
    ASSERT_TRUE(pick(dock("dock.layers"), QStringLiteral("Float panel")));
    ASSERT_TRUE(window_->close());

    window_ = std::make_unique<ui::MainWindow>(parts_);
    EXPECT_EQ(window_->dockWidgetArea(dock("dock.parts")), Qt::LeftDockWidgetArea);
    EXPECT_FALSE(dock("dock.parts")->isHidden());
    EXPECT_TRUE(dock("dock.layers")->isFloating());
}
