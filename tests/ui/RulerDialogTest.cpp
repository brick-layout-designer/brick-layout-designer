// The Edit ruler dialog keeps a typed dash pattern to what a server takes
// (fixtures/contracts/update-guard.json › drawingSettings): a dash over 1000,
// a negative one, "inf" or a pattern adding up to nothing would have the
// server refuse a live layout's edit and end it.

#include "ui/EditDialogs.h"

#include "core/LayerRuler.h"
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QUndoStack>

#include <memory>
#include <vector>

using namespace bld;

namespace {

// A map holding one linear ruler, "R".
std::unique_ptr<core::Map> mapWithRuler() {
    auto map = std::make_unique<core::Map>();
    auto layer = std::make_unique<core::LayerRuler>();
    layer->guid = QStringLiteral("L");
    core::LayerRuler::AnyRuler r;
    r.kind = core::RulerKind::Linear;
    r.linear.guid = QStringLiteral("R");
    r.linear.point2 = QPointF(10, 0);
    r.linear.guidelineDashPattern = { 4.0f, 2.0f };
    layer->rulers.push_back(r);
    map->layers().push_back(std::move(layer));
    return map;
}

// Opens the dialog, types `dashes` into its dash pattern and presses OK.
std::vector<float> savedDashes(const QString& dashes) {
    auto map = mapWithRuler();
    QUndoStack undo;
    bool typed = false;
    QTimer::singleShot(0, [&] {
        auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg) return;
        if (auto* edit = dlg->findChild<QLineEdit*>(QStringLiteral("dashPattern"))) {
            edit->setText(dashes);
            typed = true;
        }
        dlg->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    EXPECT_TRUE(ui::editRulerDialog(nullptr, *map, 0, QStringLiteral("R"), undo));
    EXPECT_TRUE(typed);
    return static_cast<core::LayerRuler&>(*map->layers()[0]).rulers[0].linear.guidelineDashPattern;
}

} // namespace

TEST(RulerDialog, DashPatternKeepsToWhatServersTake) {
    EXPECT_EQ(savedDashes(QStringLiteral("4, 99999, -2, inf, 2")), (std::vector<float>{ 4.0f, 1000.0f, 0.0f, 2.0f }));
}

TEST(RulerDialog, DashPatternOfNothingIsASolidLine) {
    EXPECT_TRUE(savedDashes(QStringLiteral("0, 0.001")).empty());
}

TEST(RulerDialog, DashPatternKeepsSixtyFourLengths) {
    QStringList many;
    for (int i = 0; i < 100; ++i) many << QStringLiteral("2");
    EXPECT_EQ(savedDashes(many.join(QLatin1Char(','))).size(), 64U);
}

TEST(RulerDialog, AnOrdinaryDashPatternIsKept) {
    EXPECT_EQ(savedDashes(QStringLiteral("4,2,1,2")), (std::vector<float>{ 4.0f, 2.0f, 1.0f, 2.0f }));
}
