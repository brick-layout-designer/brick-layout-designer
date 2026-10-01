// The loading card, like the web's: "Opening layout…", then "Loading part
// pictures… 132 of 480" with a real progress bar while a layout's pictures
// are read, then nothing, or "3 pictures couldn't load" with Retry. Also
// the parts browser reading its thumbnails a few at a time.

#include "ui/LoadingCard.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/PartsBrowser.h"
#include "ui/UpdateCheck.h"

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QListWidget>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <functional>

using namespace bld;

namespace {

const QString kCorpus = QString::fromUtf8(BLD_BBM_CORPUS_DIR);
const QString kParts = QString::fromUtf8(BLD_PARTS_LIBRARY_ROOT);

bool waitFor(const std::function<bool()>& done, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

// Writes down what the card shows each time it paints.
struct PaintSpy : QObject {
    explicit PaintSpy(ui::LoadingCard* card) : card_(card) { card->installEventFilter(this); }
    bool eventFilter(QObject*, QEvent* e) override {
        if (e->type() == QEvent::Paint) seen << (card_->title() + QLatin1Char('|') + card_->count());
        return false;
    }
    ui::LoadingCard* card_;
    QStringList seen;
};

std::unique_ptr<core::Map> fordyce() {
    auto loaded = saveload::readBbm(kCorpus + QStringLiteral("/fordyce-2026.bbm"));
    return loaded.ok() ? std::move(loaded.map) : nullptr;
}

// Distinct parts of `map` that have a picture in `lib`.
int picturesIn(const core::Map& map, parts::PartsLibrary& lib) {
    QSet<QString> keys;
    for (const auto& l : map.layers())
        if (l->kind() == core::LayerKind::Brick)
            for (const auto& b : static_cast<const core::LayerBrick&>(*l).bricks) {
                const auto meta = lib.metadata(b.partNumber);
                if (meta && !meta->gifFilePath.isEmpty()) keys.insert(b.partNumber.toLower());
            }
    return static_cast<int>(keys.size());
}

class StepGuard {
public:
    explicit StepGuard(int ms) { ui::MapView::setPictureProgressStepMs(ms); }
    ~StepGuard() { ui::MapView::setPictureProgressStepMs(30); }
};

}  // namespace

TEST(LoadingCard, ShowsACountAndARealProgressBar) {
    QWidget host;
    host.resize(800, 600);
    ui::LoadingCard card(&host, ui::LoadingCard::Place::Centre);
    host.show();
    card.showProgress(QStringLiteral("Loading part pictures…"), 132, 480);
    EXPECT_TRUE(card.isVisible());
    EXPECT_EQ(card.title(), QStringLiteral("Loading part pictures…"));
    EXPECT_EQ(card.count(), QStringLiteral("132 of 480"));
    EXPECT_EQ(card.bar()->maximum(), 480);
    EXPECT_EQ(card.bar()->value(), 132);
    EXPECT_FALSE(card.retryButton()->isVisible());
    // It doesn't take the map's clicks.
    EXPECT_TRUE(card.testAttribute(Qt::WA_TransparentForMouseEvents));
    // Centred over the map, inside it.
    EXPECT_NEAR(card.geometry().center().x(), 400, 2);
    EXPECT_GE(card.x(), 16);
}

TEST(LoadingCard, BusyHasNoNumber) {
    QWidget host;
    ui::LoadingCard card(&host, ui::LoadingCard::Place::Centre);
    host.show();
    card.showBusy(QStringLiteral("Opening layout…"), QStringLiteral("Reading the layout file."));
    EXPECT_EQ(card.bar()->maximum(), 0);  // Qt's busy bar
    EXPECT_TRUE(card.count().isEmpty());
}

TEST(LoadingCard, CountsInTheUsersNumberFormat) {
    const QLocale before;
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    EXPECT_EQ(ui::LoadingCard::countText(1204, 2853), QStringLiteral("1,204 of 2,853"));
    EXPECT_EQ(ui::LoadingCard::pictures(1), QStringLiteral("1 picture"));
    EXPECT_EQ(ui::LoadingCard::pictures(3), QStringLiteral("3 pictures"));
    QLocale::setDefault(before);
}

TEST(LoadingCard, FailuresHaveRetryAndHide) {
    QWidget host;
    ui::LoadingCard card(&host, ui::LoadingCard::Place::Top);
    host.show();
    QSignalSpy retry(&card, &ui::LoadingCard::retryRequested);
    card.showFailed(QStringLiteral("3 pictures couldn't load"));
    EXPECT_TRUE(card.failureShown());
    ASSERT_TRUE(card.retryButton()->isVisible());
    EXPECT_FALSE(card.testAttribute(Qt::WA_TransparentForMouseEvents));
    card.retryButton()->click();
    EXPECT_EQ(retry.count(), 1);
    auto* hide = card.findChild<QToolButton*>(QStringLiteral("LoadingHide"));
    ASSERT_NE(hide, nullptr);
    hide->click();
    EXPECT_FALSE(card.isVisible());
}

TEST(MapViewLoading, CountsTheLayoutsPicturesThenGoesAway) {
    if (!QDir(kParts).exists()) GTEST_SKIP() << "parts library missing";
    parts::PartsLibrary lib;
    lib.addSearchPath(kParts);
    lib.scan();
    auto map = fordyce();
    ASSERT_TRUE(map);
    const int wanted = picturesIn(*map, lib);
    ASSERT_GT(wanted, 0);

    StepGuard step(0);
    ui::MapView view(lib);
    view.resize(900, 700);
    view.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&view));
    PaintSpy spy(view.loadingCard());
    view.showOpening(QStringLiteral("Reading the layout file."));
    view.loadMap(std::move(map));

    ASSERT_FALSE(spy.seen.isEmpty());
    EXPECT_EQ(spy.seen.first(), QStringLiteral("Opening layout…|"));
    const QString last = QStringLiteral("Loading part pictures…|%1 of %1").arg(wanted);
    EXPECT_TRUE(spy.seen.contains(last)) << spy.seen.join(QLatin1Char('\n')).toStdString();
    EXPECT_TRUE(spy.seen.contains(QStringLiteral("Loading part pictures…|1 of %1").arg(wanted)));
    // Done: the card goes away, nothing failed.
    EXPECT_FALSE(view.loadingCard()->isVisible());
    EXPECT_TRUE(view.failedPictures().isEmpty());

    // Opening it again reads nothing from disk: no card at all.
    spy.seen.clear();
    view.loadMap(fordyce());
    EXPECT_TRUE(spy.seen.isEmpty()) << spy.seen.join(QLatin1Char('\n')).toStdString();
}

TEST(MapViewLoading, APictureThatCantBeReadSaysSoAndRetryReadsItAgain) {
    if (!QFile::exists(kParts + QStringLiteral("/Baseplate/3811.2.xml"))) GTEST_SKIP() << "parts library missing";
    // A library with one part whose picture is broken.
    QTemporaryDir dir;
    ASSERT_TRUE(QFile::copy(kParts + QStringLiteral("/Baseplate/3811.2.xml"), dir.filePath(QStringLiteral("3811.2.xml"))));
    const QString gif = dir.filePath(QStringLiteral("3811.2.gif"));
    {
        QFile f(gif);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("not a picture");
    }
    parts::PartsLibrary lib;
    lib.addSearchPath(dir.path());
    lib.scan();
    ASSERT_TRUE(lib.metadata(QStringLiteral("3811.2")));

    ui::MapView view(lib);
    view.resize(900, 700);
    view.show();
    view.loadMap(fordyce());
    EXPECT_EQ(view.failedPictures(), QStringList{ QStringLiteral("3811.2") });
    ui::LoadingCard* card = view.loadingCard();
    ASSERT_TRUE(card->failureShown());
    EXPECT_EQ(card->title(), QStringLiteral("1 picture couldn't load"));

    // Fixed on disk: Retry reads it again and the card goes away.
    QFile::remove(gif);
    ASSERT_TRUE(QFile::copy(kParts + QStringLiteral("/Baseplate/3811.2.gif"), gif));
    card->retryButton()->click();
    ASSERT_TRUE(waitFor([&] { return view.failedPictures().isEmpty(); }));
    EXPECT_FALSE(card->isVisible());
    EXPECT_FALSE(lib.pixmap(QStringLiteral("3811.2")).isNull());

    // Broken again (the library reloaded): it says so again.
    QFile::remove(gif);
    {
        QFile f(gif);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("not a picture");
    }
    lib.forgetPixmap(QStringLiteral("3811.2"));
    view.reloadPictures();
    ASSERT_TRUE(card->failureShown());
    // Hidden, the same failure doesn't come back on the next (live) reload.
    card->findChild<QToolButton*>(QStringLiteral("LoadingHide"))->click();
    view.loadMap(fordyce());
    EXPECT_FALSE(card->isVisible());
}

TEST(PartsBrowserLoading, ReadsThumbnailsAFewAtATimeWithACount) {
    if (!QDir(kParts).exists()) GTEST_SKIP() << "parts library missing";
    parts::PartsLibrary lib;
    lib.addSearchPath(kParts);
    lib.scan();
    ui::PartsBrowser browser(lib);
    browser.show();
    // rebuild() returns before reading the thousands of thumbnails.
    ASSERT_GT(browser.iconsWanted(), 500);
    EXPECT_EQ(browser.iconsLoaded(), 0);
    auto* grid = browser.findChild<QListWidget*>();
    ASSERT_NE(grid, nullptr);
    EXPECT_TRUE(grid->item(0)->icon().isNull());

    bool counted = false;
    ASSERT_TRUE(waitFor([&] {
        ui::LoadingCard* card = browser.loadingCard();
        if (card->isVisible() && card->title() == QStringLiteral("Loading part pictures…")
            && card->count().endsWith(QStringLiteral(" of %1").arg(QLocale().toString(browser.iconsWanted()))))
            counted = true;
        return browser.iconsLoaded() == browser.iconsWanted();
    }, 60000));
    EXPECT_TRUE(counted);
    EXPECT_FALSE(browser.loadingCard()->isVisible());
    EXPECT_FALSE(grid->item(0)->icon().isNull());
}

TEST(MainWindowLoading, OpeningAFileSaysSoThenCountsThePictures) {
    QStandardPaths::setTestModeEnabled(true);
    ui::UpdateCheck::setCheckAtStartupEnabled(false);
    {
        parts::PartsLibrary lib;
        StepGuard step(0);
        ui::MainWindow window(lib);
        window.resize(1200, 800);
        window.show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
        auto* view = window.findChild<ui::MapView*>();
        ASSERT_NE(view, nullptr);
        PaintSpy spy(view->loadingCard());
        ASSERT_TRUE(window.openFile(kCorpus + QStringLiteral("/fordyce-2026.bbm")));
        ASSERT_FALSE(spy.seen.isEmpty());
        EXPECT_EQ(spy.seen.first(), QStringLiteral("Opening layout…|"));
        if (lib.partCount() > 0) {
            bool counted = false;
            for (const QString& s : spy.seen) counted |= s.startsWith(QStringLiteral("Loading part pictures…|"));
            EXPECT_TRUE(counted) << spy.seen.join(QLatin1Char('\n')).toStdString();
        }
        EXPECT_FALSE(view->loadingCard()->isVisible());
    }
    QStandardPaths::setTestModeEnabled(false);
}
