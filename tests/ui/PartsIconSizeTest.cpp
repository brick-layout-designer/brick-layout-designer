// The Parts list's picture size: the slider under the list, Ctrl+wheel and
// a pinch over it, the grid following, and the synced setting partsIconSize
// (saved once you pause, and taken in when it changes elsewhere).

#include "ui/PartsBrowser.h"
#include "ui/TouchMode.h"
#include "ui/theme/AppPrefs.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QListWidget>
#include <QNativeGestureEvent>
#include <QPointingDevice>
#include <QSettings>
#include <QSlider>
#include <QTest>
#include <QWheelEvent>

#include <functional>

using namespace bld;
using ui::theme::AppPrefs;
using ui::theme::PrefsStore;

namespace {

constexpr const char* kGroup = "test-parts-icon-size";

bool waitFor(const std::function<bool()>& done, int ms = 20000) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

QPointingDevice* touchScreen() {
    static QPointingDevice* dev = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
    return dev;
}

void ctrlWheel(QWidget* w, int notches) {
    const QPointF at(w->rect().center());
    QWheelEvent ev(at, w->mapToGlobal(at), QPoint(), QPoint(0, 120 * notches), Qt::NoButton, Qt::ControlModifier,
                   Qt::NoScrollPhase, false);
    QApplication::sendEvent(w, &ev);
}

// The biggest side of the first loaded picture.
int firstIconSide(QListWidget* grid) {
    for (int i = 0; i < grid->count(); ++i) {
        const QIcon icon = grid->item(i)->icon();
        if (icon.isNull()) continue;
        const QSize s = icon.availableSizes().value(0);
        return std::max(s.width(), s.height());
    }
    return 0;
}

class PartsIconSize : public ::testing::Test {
protected:
    void SetUp() override {
        QSettings().remove(QString::fromLatin1(kGroup));
        const QString root = QString::fromUtf8(BLD_PARTS_LIBRARY_ROOT);
        if (!QDir(root).exists()) GTEST_SKIP() << "parts library missing";
        lib_.addSearchPath(root);
        lib_.scan();
        store_ = std::make_unique<PrefsStore>(QString::fromLatin1(kGroup));
        browser_ = std::make_unique<ui::PartsBrowser>(lib_);
        browser_->setPrefsStore(store_.get());
        browser_->resize(420, 600);
        browser_->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(browser_.get()));
        grid_ = browser_->grid();
        ASSERT_TRUE(waitFor([&] { return browser_->iconsLoaded() == browser_->iconsWanted(); }, 60000));
    }
    void TearDown() override {
        browser_.reset();
        store_.reset();
        ui::TouchMode::instance().setActive(false);
        QSettings().remove(QString::fromLatin1(kGroup));
    }
    bool picturesAt(int px) {
        return waitFor([&] { return browser_->iconsLoaded() == browser_->iconsWanted() && firstIconSide(grid_) == px; });
    }

    parts::PartsLibrary lib_;
    std::unique_ptr<PrefsStore> store_;
    std::unique_ptr<ui::PartsBrowser> browser_;
    QListWidget* grid_ = nullptr;
};

}  // namespace

TEST_F(PartsIconSize, TheSliderResizesThePicturesAndTheGridAndSavesAfterAPause) {
    EXPECT_EQ(browser_->iconSize(), ui::theme::kPartsIconDefault);
    QSlider* slider = browser_->findChild<QSlider*>(QStringLiteral("partsIconSize"));
    ASSERT_NE(slider, nullptr);
    EXPECT_TRUE(slider->isVisible());
    EXPECT_EQ(slider->minimum(), 32);
    EXPECT_EQ(slider->maximum(), 160);
    EXPECT_EQ(slider->value(), ui::theme::kPartsIconDefault);
    EXPECT_EQ(firstIconSide(grid_), ui::theme::kPartsIconDefault);

    slider->setValue(144);
    EXPECT_EQ(browser_->iconSize(), 144);
    EXPECT_EQ(grid_->iconSize(), QSize(144, 144));
    EXPECT_EQ(grid_->gridSize(), QSize(144 + 32, 144 + 52));
    // Pictures read again at the new size, sharp rather than stretched.
    EXPECT_TRUE(picturesAt(144));
    // Saved once the slider rests, not on every step.
    EXPECT_EQ(store_->prefs().partsIconSize, ui::theme::kPartsIconDefault);
    ASSERT_TRUE(waitFor([&] { return store_->prefs().partsIconSize == 144; }, 3000));
    EXPECT_TRUE(store_->prefs().updatedAt.isValid());
    // And kept on this computer for next time.
    EXPECT_EQ(PrefsStore(QString::fromLatin1(kGroup)).prefs().partsIconSize, 144);

    slider->setValue(32);
    EXPECT_EQ(grid_->gridSize(), QSize(64, 84));
    EXPECT_TRUE(picturesAt(32));
}

TEST_F(PartsIconSize, CtrlWheelAndPinchResize) {
    QWidget* vp = grid_->viewport();
    ctrlWheel(vp, 2);
    EXPECT_EQ(browser_->iconSize(), ui::theme::kPartsIconDefault + 16);
    ctrlWheel(vp, -1);
    EXPECT_EQ(browser_->iconSize(), ui::theme::kPartsIconDefault + 8);
    ctrlWheel(vp, 100);
    EXPECT_EQ(browser_->iconSize(), ui::theme::kPartsIconMax);  // no further than the slider goes
    EXPECT_EQ(browser_->sizeSlider()->value(), ui::theme::kPartsIconMax);

    // A trackpad pinch.
    auto* pad = QTest::createTouchDevice(QInputDevice::DeviceType::TouchPad);
    const QPointF at(vp->rect().center());
    QNativeGestureEvent pinch(Qt::ZoomNativeGesture, pad, 2, at, at, vp->mapToGlobal(at), -0.5, {});
    QApplication::sendEvent(vp, &pinch);
    EXPECT_EQ(browser_->iconSize(), 80);

    // Two fingers on a touchscreen, twice as far apart.
    ui::TouchMode::instance().setActive(true);
    QTest::touchEvent(vp, touchScreen()).press(0, QPoint(150, 300)).press(1, QPoint(250, 300));
    QTest::touchEvent(vp, touchScreen()).move(0, QPoint(125, 300)).move(1, QPoint(275, 300));
    QTest::touchEvent(vp, touchScreen()).move(0, QPoint(100, 300)).move(1, QPoint(300, 300));
    QTest::touchEvent(vp, touchScreen()).release(0, QPoint(100, 300)).release(1, QPoint(300, 300));
    EXPECT_EQ(browser_->iconSize(), 160);
    ASSERT_TRUE(waitFor([&] { return store_->prefs().partsIconSize == 160; }, 3000));

    // A plain wheel still scrolls instead.
    const int before = browser_->iconSize();
    QWheelEvent plain(at, vp->mapToGlobal(at), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(vp, &plain);
    EXPECT_EQ(browser_->iconSize(), before);
}

TEST_F(PartsIconSize, ASizeFromTheAccountShowsHere) {
    AppPrefs theirs = store_->prefs();
    theirs.partsIconSize = 48;
    theirs.updatedAt = QDateTime::currentDateTimeUtc();
    store_->adopt(theirs);  // as PrefsSync does with a newer server copy
    EXPECT_EQ(browser_->iconSize(), 48);
    EXPECT_EQ(browser_->sizeSlider()->value(), 48);
    EXPECT_EQ(grid_->gridSize(), QSize(80, 100));
    EXPECT_TRUE(picturesAt(48));
}

TEST(PartsIconSizePrefs, TheServersNumberIsKeptInRange) {
    AppPrefs p = AppPrefs::fromJson(QJsonObject{ { QStringLiteral("partsIconSize"), 120 } });
    EXPECT_EQ(p.partsIconSize, 120);
    EXPECT_EQ(p.toJson().value(QStringLiteral("partsIconSize")).toInt(), 120);
    EXPECT_EQ(AppPrefs::fromJson(QJsonObject{ { QStringLiteral("partsIconSize"), 4000 } }).partsIconSize, 160);
    EXPECT_EQ(AppPrefs::fromJson(QJsonObject{ { QStringLiteral("partsIconSize"), 2 } }).partsIconSize, 32);
    EXPECT_EQ(AppPrefs::fromJson(QJsonObject{ { QStringLiteral("partsIconSize"), QStringLiteral("big") } }).partsIconSize,
              ui::theme::kPartsIconDefault);
    AppPrefs other = p;
    other.partsIconSize = 64;
    EXPECT_FALSE(other.sameSettings(p));
}
