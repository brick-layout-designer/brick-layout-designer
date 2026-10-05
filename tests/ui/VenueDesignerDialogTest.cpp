// The Venue Designer window: keys pick tools and type sizes, the view's
// clicks go through the same reducer as the web's, Save hands the venue
// back, and the floor plan is kept in the venue and calibrated.

#include "ui/venue/VenueDesignerDialog.h"
#include "ui/venue/VenueDesignerView.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPushButton>
#include <QTimer>

using namespace bld;
namespace ev = bld::edit::venue;

namespace {
constexpr double FT = 12 * ev::kStudsPerInch;

void key(QWidget& w, int k, const QString& text = {}, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QKeyEvent e(QEvent::KeyPress, k, mods, text);
    QApplication::sendEvent(&w, &e);
}
void type(QWidget& w, const QString& s) {
    for (const QChar c : s) key(w, c.toUpper().unicode(), QString(c));
}
} // namespace

TEST(VenueDesignerDialog, KeysAndClicksDrawARoomAndSaveHandsItBack) {
    std::optional<core::Venue> saved;
    ui::VenueDesignerDialog dlg(ev::emptyVenue(QStringLiteral("Hall")), QStringLiteral("test"),
                                QStringLiteral("Save"), [&](const core::Venue& v) {
                                    saved = v;
                                    return QString();
                                });
    auto* view = dlg.findChild<ui::VenueDesignerView*>();
    ASSERT_TRUE(view);

    type(dlg, QStringLiteral("r"));
    EXPECT_EQ(dlg.state().tool, ev::Tool::Room);
    emit view->pressed(QPointF(10 * FT, 10 * FT), false);
    emit view->moved(QPointF(40 * FT, 30 * FT), false);
    type(dlg, QStringLiteral("40'x20'"));
    EXPECT_EQ(dlg.state().typed, QStringLiteral("40'x20'"));
    key(dlg, Qt::Key_Return);
    ASSERT_EQ(dlg.venue().edges.size(), 4);
    const auto size = ev::roomSize(dlg.venue());
    ASSERT_TRUE(size);
    EXPECT_NEAR(size->w, 40 * FT, 1e-6);
    EXPECT_NEAR(size->h, 20 * FT, 1e-6);
    EXPECT_TRUE(dlg.dirty());

    // Ctrl+Z / Ctrl+Shift+Z step through it.
    key(dlg, Qt::Key_Z, {}, Qt::ControlModifier);
    EXPECT_TRUE(dlg.venue().edges.isEmpty());
    key(dlg, Qt::Key_Z, {}, Qt::ControlModifier | Qt::ShiftModifier);
    EXPECT_EQ(dlg.venue().edges.size(), 4);

    ASSERT_TRUE(dlg.saveNow());
    ASSERT_TRUE(saved);
    EXPECT_EQ(saved->edges.size(), 4);
    EXPECT_FALSE(dlg.dirty());
}

TEST(VenueDesignerDialog, AFailedSaveKeepsTheChangesUnsaved) {
    ui::VenueDesignerDialog dlg(ev::emptyVenue(), QStringLiteral("test"), QStringLiteral("Save"),
                                [](const core::Venue&) { return QStringLiteral("disk full"); });
    dlg.dispatch(ev::act::Edit{ ev::addPower(dlg.venue(), { 1, 1 }, true) });
    // The warning box is modal; close it as soon as it shows.
    QTimer::singleShot(0, [] {
        for (QWidget* w : QApplication::topLevelWidgets())
            if (w->inherits("QMessageBox")) w->close();
    });
    EXPECT_FALSE(dlg.saveNow());
    EXPECT_TRUE(dlg.dirty());
}

TEST(VenueDesignerDialog, FloorPlanTravelsInTheVenueAndCalibrates) {
    ui::FloorPlan plan;
    plan.image = QImage(200, 100, QImage::Format_RGB32);
    plan.image.fill(Qt::gray);
    plan.topLeft = { 10, 20 };
    plan.studsPerPx = 0.5;
    const auto v = ui::withFloorPlan(ev::emptyVenue(), plan);
    const auto back = ui::floorPlanOf(v);
    ASSERT_TRUE(back);
    EXPECT_EQ(back->image.size(), QSize(200, 100));
    EXPECT_EQ(back->topLeft, QPointF(10, 20));
    EXPECT_DOUBLE_EQ(back->studsPerPx, 0.5);
    EXPECT_TRUE(v.extras.value(QLatin1String("floorPlan"))
                    .toObject()
                    .value(QLatin1String("image"))
                    .toString()
                    .startsWith(QLatin1String("data:image/jpeg;base64,")));
    EXPECT_FALSE(ui::floorPlanOf(ui::withFloorPlan(v, std::nullopt)));

    // Points 100 studs apart on the plan are really 150 apart: the first stays put.
    const auto c = ui::calibratePlan(plan, { 60, 20 }, { 160, 20 }, 150);
    EXPECT_DOUBLE_EQ(c.studsPerPx, 0.75);
    EXPECT_DOUBLE_EQ(c.topLeft.x(), 60 - 50 * 1.5);
    EXPECT_DOUBLE_EQ(c.topLeft.y(), 20);
}

// BLD_VENUE_SHOTS=<dir>: a picture of a room's wall labels, to look at (not compared).
TEST(VenueDesignerDialog, PictureForReview) {
    const QString out = qEnvironmentVariable("BLD_VENUE_SHOTS");
    if (out.isEmpty()) GTEST_SKIP() << "BLD_VENUE_SHOTS not set";
    ui::VenueDesignerDialog dlg(ev::emptyVenue(QStringLiteral("Hall")), QStringLiteral("test"), QStringLiteral("Save"),
                                [](const core::Venue&) { return QString(); });
    dlg.resize(1200, 800);
    dlg.show();
    auto* view = dlg.findChild<ui::VenueDesignerView*>();
    type(dlg, QStringLiteral("r"));
    emit view->pressed(QPointF(10 * FT, 10 * FT), false);
    emit view->moved(QPointF(40 * FT, 30 * FT), false);
    type(dlg, QStringLiteral("40'x20'"));
    key(dlg, Qt::Key_Return);
    key(dlg, Qt::Key_Escape);
    QApplication::processEvents();  // the window's final size first
    view->fit();
    QApplication::processEvents();
    dlg.grab().save(out + QStringLiteral("/desk-venue.png"));
}
