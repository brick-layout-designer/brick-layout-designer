// The welcome and the guided tours: the catalogue is the web app's
// tours.json (same ids, same words; with BLD_WEB_REPO set the copy is
// compared byte for byte), every editor and room step finds its control,
// and the tour card itself: Back / Next / Skip, Esc, Tab staying in the
// card, the outline round the control, and toursSeen remembering it.

#include "ui/MainWindow.h"
#include "ui/SettingsDialog.h"
#include "ui/UpdateCheck.h"
#include "ui/theme/AppPrefs.h"
#include "ui/tours/Tours.h"
#include "ui/venue/VenueDesignerDialog.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>

using namespace bld;
using namespace bld::ui;
namespace ev = bld::edit::venue;

namespace {

constexpr const char* kGroup = "test-tours";

class Tours : public ::testing::Test {
protected:
    void SetUp() override { QSettings().remove(QString::fromLatin1(kGroup)); }
    void TearDown() override { QSettings().remove(QString::fromLatin1(kGroup)); }
};

QByteArray readAll(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QPushButton* cardButton(tours::TourOverlay* o, const char* name) {
    return o->card()->findChild<QPushButton*>(QString::fromLatin1(name));
}

QString cardTitle(tours::TourOverlay* o) { return o->card()->findChild<QLabel*>(QStringLiteral("TourTitle"))->text(); }

// A window with a couple of tagged controls.
struct Host {
    QWidget window;
    QPushButton* parts = new QPushButton(QStringLiteral("Parts"), &window);
    QPushButton* sheets = new QPushButton(QStringLiteral("Sheets"), &window);
    Host() {
        window.resize(900, 600);
        parts->setGeometry(20, 20, 120, 40);
        sheets->setGeometry(600, 400, 100, 40);
        tours::tag(parts, QStringLiteral("panel.parts"));
        tours::tag(sheets, QStringLiteral("panel.sheets"));
        window.show();
        QApplication::processEvents();
    }
};

}  // namespace

TEST_F(Tours, TheCatalogueIsTheWebAppsFile) {
    const auto& c = tours::catalogue();
    EXPECT_EQ(c.welcome.id, QStringLiteral("welcome"));
    QStringList ids;
    for (const auto& t : c.tours) ids << t.id;
    EXPECT_EQ(ids, (QStringList{ "editor", "rooms", "clubs", "view" }));
    for (const auto& t : c.tours) {
        EXPECT_GE(t.steps.size(), 4) << t.id.toStdString();
        EXPECT_LE(t.steps.size(), 7) << t.id.toStdString();
        for (const auto& s : t.steps) {
            EXPECT_FALSE(s.title.isEmpty());
            EXPECT_LE(s.text.size(), 120) << s.text.toStdString();
        }
    }
    // The phone tour is the web's; the desktop offers the others.
    QStringList here;
    for (const auto* t : tours::desktopTours()) here << t->id;
    EXPECT_EQ(here, (QStringList{ "editor", "rooms", "clubs" }));
    EXPECT_EQ(tours::stepCount(2, 5), QStringLiteral("2 of 5"));

    // The embedded copy is the file in the tree, and it says where its twin lives.
    const QByteArray embedded = readAll(QStringLiteral(":/bld/tours/tours.json"));
    EXPECT_EQ(embedded, readAll(QStringLiteral(BLD_SOURCE_DIR "/src/ui/tours/tours.json")));
    const QString about = QJsonDocument::fromJson(embedded).object().value(QStringLiteral("_about")).toString();
    EXPECT_TRUE(about.contains(QStringLiteral("apps/web/src/tours/tours.json")));
    // With the web repo beside it, the two files are identical.
    const QByteArray web = qgetenv("BLD_WEB_REPO");
    if (!web.isEmpty())
        EXPECT_EQ(embedded, readAll(QString::fromLocal8Bit(web) + QStringLiteral("/apps/web/src/tours/tours.json")));
}

TEST_F(Tours, ParsingKeepsEveryField) {
    const auto c = tours::parseCatalogue(R"({"welcome":{"id":"w","title":"T","text":"X","dismiss":"No",
        "actions":{"layout":{"label":"L","text":"LT"},"club":{"label":"C","text":"CT"},"tour":{"label":"G","text":"GT"}}},
        "buttons":{"next":"N","back":"B","skip":"S","done":"D","of":"{n}/{total}"},
        "tours":[{"id":"a","title":"A","devices":"all","steps":[{"target":"t","title":"st","text":"sx"}]}]})");
    EXPECT_EQ(c.welcome.clubText, QStringLiteral("CT"));
    EXPECT_EQ(c.welcome.tourLabel, QStringLiteral("G"));
    EXPECT_EQ(c.welcome.dismiss, QStringLiteral("No"));
    EXPECT_EQ(c.buttons.done, QStringLiteral("D"));
    ASSERT_EQ(c.tours.size(), 1);
    EXPECT_EQ(c.tours[0].steps[0].target, QStringLiteral("t"));
    EXPECT_TRUE(tours::parseCatalogue("not json").tours.isEmpty());
}

TEST_F(Tours, EveryEditorStepFindsItsControl) {
    QStandardPaths::setTestModeEnabled(true);
    UpdateCheck::setCheckAtStartupEnabled(false);
    parts::PartsLibrary lib;
    {
        MainWindow w(lib);
        w.resize(1280, 800);
        w.show();
        QApplication::processEvents();
        for (const auto& s : tours::findTour(QStringLiteral("editor"))->steps)
            EXPECT_NE(tours::findTarget(&w, s.target), nullptr) << s.target.toStdString();
        // Help › Tour: … for each desktop tour.
        for (const auto* t : tours::desktopTours())
            EXPECT_NE(w.findChild<QAction*>(QStringLiteral("help.tour.") + t->id), nullptr) << t->id.toStdString();
    }
    QStandardPaths::setTestModeEnabled(false);
}

TEST_F(Tours, EveryRoomStepFindsItsControl) {
    VenueDesignerDialog dlg(ev::emptyVenue(QStringLiteral("Hall")), QStringLiteral("test"), QStringLiteral("Save"),
                            [](const core::Venue&) { return QString(); });
    dlg.resize(1200, 800);
    dlg.show();
    QApplication::processEvents();
    for (const auto& s : tours::findTour(QStringLiteral("rooms"))->steps)
        EXPECT_NE(tours::findTarget(&dlg, s.target), nullptr) << s.target.toStdString();
}

TEST_F(Tours, NextBackAndDoneRememberTheTour) {
    theme::PrefsStore store(QString::fromLatin1(kGroup));
    Host h;
    auto* o = tours::startTour(&h.window, QStringLiteral("editor"), store);
    ASSERT_NE(o, nullptr);
    const auto& steps = tours::findTour(QStringLiteral("editor"))->steps;
    EXPECT_EQ(cardTitle(o), steps[0].title);
    EXPECT_EQ(o->card()->findChild<QLabel*>(QStringLiteral("TourCount"))->text(),
              QStringLiteral("The editor · 1 of %1").arg(steps.size()));
    EXPECT_FALSE(cardButton(o, "TourBack")->isVisible());
    // The outline sits round the parts button, the card beside it.
    EXPECT_EQ(o->target(), h.parts);
    EXPECT_EQ(o->hole(), QRect(14, 14, 132, 52));
    EXPECT_FALSE(o->card()->geometry().intersects(o->hole()));

    cardButton(o, "TourNext")->click();
    EXPECT_EQ(o->step(), 1);
    EXPECT_EQ(o->target(), h.sheets);
    EXPECT_TRUE(cardButton(o, "TourBack")->isVisible());
    cardButton(o, "TourBack")->click();
    EXPECT_EQ(o->step(), 0);

    // A step with nothing to point at: no outline, the card in the middle.
    for (int i = 0; i < 2; ++i) cardButton(o, "TourNext")->click();
    EXPECT_EQ(o->target(), nullptr);
    EXPECT_TRUE(o->hole().isEmpty());
    EXPECT_NEAR(o->card()->geometry().center().x(), o->rect().center().x(), 2);

    while (o->step() < steps.size() - 1) cardButton(o, "TourNext")->click();
    EXPECT_FALSE(cardButton(o, "TourSkip")->isVisible());
    EXPECT_EQ(cardButton(o, "TourNext")->text(), QStringLiteral("Done"));
    bool ended = false;
    QObject::connect(o, &tours::TourOverlay::finished, [&ended] { ended = true; });
    cardButton(o, "TourNext")->click();
    QApplication::processEvents();
    EXPECT_TRUE(ended);
    EXPECT_TRUE(store.prefs().toursSeen.contains(QStringLiteral("editor")));
    QApplication::processEvents();
    EXPECT_TRUE(h.window.findChildren<tours::TourOverlay*>().isEmpty());
}

TEST_F(Tours, FollowsTheControlWhenItMoves) {
    theme::PrefsStore store(QString::fromLatin1(kGroup));
    // The control sits inside a panel, so moving it tells the window nothing:
    // the overlay keeps looking on its own.
    QWidget window;
    window.resize(900, 600);
    auto* panel = new QWidget(&window);
    panel->setGeometry(0, 0, 900, 600);
    auto* parts = new QPushButton(QStringLiteral("Parts"), panel);
    parts->setGeometry(20, 20, 120, 40);
    tours::tag(parts, QStringLiteral("panel.parts"));
    window.show();
    QApplication::processEvents();
    auto* o = tours::startTour(&window, QStringLiteral("editor"), store);
    EXPECT_EQ(o->hole(), QRect(14, 14, 132, 52));
    parts->setGeometry(200, 100, 120, 40);
    QTest::qWait(400);
    EXPECT_EQ(o->hole(), QRect(194, 94, 132, 52));

    // A control that shows up a moment after the step starts (a panel still opening) is found.
    parts->hide();
    o->next();
    o->back();
    EXPECT_TRUE(o->hole().isEmpty());
    QTimer::singleShot(100, parts, [parts] { parts->show(); });
    QTest::qWait(500);
    EXPECT_EQ(o->target(), parts);
    EXPECT_EQ(o->hole(), QRect(194, 94, 132, 52));
}

TEST_F(Tours, TheKeyboardStaysInTheCardAndEscCloses) {
    theme::PrefsStore store(QString::fromLatin1(kGroup));
    Host h;
    h.window.activateWindow();
    h.sheets->setFocus();
    auto* o = tours::startTour(&h.window, QStringLiteral("editor"), store);
    cardButton(o, "TourNext")->click();
    QApplication::processEvents();
    auto* next = cardButton(o, "TourNext");
    EXPECT_EQ(QApplication::focusWidget(), next);
    // Next → (wraps) Skip → Back → Next.
    QTest::keyClick(next, Qt::Key_Tab);
    EXPECT_EQ(QApplication::focusWidget(), cardButton(o, "TourSkip"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
    EXPECT_EQ(QApplication::focusWidget(), cardButton(o, "TourBack"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Backtab);
    EXPECT_EQ(QApplication::focusWidget(), cardButton(o, "TourSkip"));

    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    QApplication::processEvents();
    QApplication::processEvents();
    EXPECT_TRUE(h.window.findChildren<tours::TourOverlay*>().isEmpty());
    EXPECT_TRUE(store.prefs().toursSeen.contains(QStringLiteral("editor")));
    // The focus goes back where it was.
    EXPECT_EQ(QApplication::focusWidget(), h.sheets);
}

TEST_F(Tours, SkipRemembersItAndClicksDontReachTheWindow) {
    theme::PrefsStore store(QString::fromLatin1(kGroup));
    Host h;
    int clicked = 0;
    QObject::connect(h.parts, &QPushButton::clicked, [&clicked] { ++clicked; });
    auto* o = tours::startTour(&h.window, QStringLiteral("rooms"), store);
    h.parts->setFocus();
    QTest::mouseClick(o, Qt::LeftButton, {}, h.parts->geometry().center());
    EXPECT_EQ(clicked, 0);
    // A click on the dimmed page puts the focus back in the card.
    EXPECT_EQ(QApplication::focusWidget(), cardButton(o, "TourNext"));
    cardButton(o, "TourSkip")->click();
    QApplication::processEvents();
    EXPECT_EQ(store.prefs().toursSeen, QStringList{ QStringLiteral("rooms") });
}

TEST_F(Tours, TheWelcomeOffersThreeWaysIn) {
    const auto& w = tours::catalogue().welcome;
    tours::WelcomeDialog dlg;
    auto* tour = dlg.findChild<QAbstractButton*>(QStringLiteral("WelcomeTour"));
    ASSERT_NE(tour, nullptr);
    EXPECT_EQ(tour->accessibleName(), w.tourLabel);
    EXPECT_EQ(dlg.findChild<QAbstractButton*>(QStringLiteral("WelcomeLayout"))->accessibleName(), w.layoutLabel);
    EXPECT_EQ(dlg.findChild<QAbstractButton*>(QStringLiteral("WelcomeClub"))->accessibleName(), w.clubLabel);
    EXPECT_EQ(dlg.findChild<QPushButton*>(QStringLiteral("WelcomeDismiss"))->text(), w.dismiss);
    tour->click();
    EXPECT_EQ(dlg.choice(), tours::WelcomeDialog::Choice::Tour);
    EXPECT_EQ(dlg.result(), QDialog::Accepted);

    tours::WelcomeDialog other;
    other.findChild<QPushButton*>(QStringLiteral("WelcomeDismiss"))->click();
    EXPECT_EQ(other.choice(), tours::WelcomeDialog::Choice::None);
}

TEST_F(Tours, TheWelcomeComesOnceOnFirstLaunch) {
    QStandardPaths::setTestModeEnabled(true);
    UpdateCheck::setCheckAtStartupEnabled(false);
    auto& store = theme::PrefsStore::instance();
    const theme::AppPrefs before = store.prefs();
    theme::AppPrefs fresh = before;
    fresh.toursSeen.clear();
    store.update(fresh);
    parts::PartsLibrary lib;
    {
        MainWindow w(lib);
        int shown = 0;
        // Answer the welcome when it opens.
        QTimer poke;
        QObject::connect(&poke, &QTimer::timeout, [&shown] {
            for (auto* top : QApplication::topLevelWidgets())
                if (auto* d = qobject_cast<tours::WelcomeDialog*>(top); d && d->isVisible()) {
                    ++shown;
                    d->findChild<QPushButton*>(QStringLiteral("WelcomeDismiss"))->click();
                }
        });
        poke.start(20);
        w.showWelcomeIfNew();
        EXPECT_EQ(shown, 1);
        EXPECT_TRUE(store.prefs().toursSeen.contains(QStringLiteral("welcome")));
        w.showWelcomeIfNew();  // seen: returns at once
        EXPECT_EQ(shown, 1);
    }
    store.update(before);
    QStandardPaths::setTestModeEnabled(false);
}

TEST_F(Tours, SettingsStartsToursAndShowsThemAgain) {
    theme::PrefsStore store(QString::fromLatin1(kGroup));
    theme::AppPrefs p;
    p.toursSeen = { QStringLiteral("welcome"), QStringLiteral("editor") };
    store.update(p);
    QString started;
    SettingsDialog dlg(store, QString(), {}, nullptr, [&started](const QString& id) { started = id; });
    auto* again = dlg.findChild<QPushButton*>(QStringLiteral("showToursAgain"));
    ASSERT_NE(again, nullptr);
    EXPECT_TRUE(again->isEnabled());
    again->click();
    EXPECT_TRUE(store.prefs().toursSeen.isEmpty());
    EXPECT_FALSE(again->isEnabled());
    dlg.findChild<QPushButton*>(QStringLiteral("tour.clubs"))->click();
    EXPECT_EQ(started, QStringLiteral("clubs"));
    EXPECT_EQ(dlg.result(), QDialog::Accepted);
    // Without a way to start one, no tour buttons.
    SettingsDialog plain(store, QString());
    EXPECT_EQ(plain.findChild<QPushButton*>(QStringLiteral("tour.clubs")), nullptr);
}
