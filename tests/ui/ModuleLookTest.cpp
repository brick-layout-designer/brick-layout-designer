// A placed module's look window (ModuleLookDialog): Show name, the two
// colours with the "Same colour" link, and Reset to default, each written
// at once; and the ModulesPanel menu that opens it.

#include "ui/ModuleLookDialog.h"
#include "ui/ModulesPanel.h"

#include "core/Map.h"
#include "core/Module.h"

#include <gtest/gtest.h>

#include <QCheckBox>
#include <QApplication>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>

using namespace bld;

namespace {

struct Harness {
    core::Module mod;
    bool gone = false;
    int applied = 0;
    QStringList texts;
    ui::ModuleLookDialog dlg{ [this]() -> const core::Module* { return gone ? nullptr : &mod; },
                              [this](const core::Module& m, const QString& text) {
                                  mod = m;
                                  ++applied;
                                  texts << text;
                              } };
    Harness() {
        mod.id = QStringLiteral("m1");
        mod.name = QStringLiteral("Harbour");
        dlg.refresh();
    }
};

}  // namespace

TEST(ModuleLookDialog, ShowNameIsWrittenAtOnce) {
    Harness h;
    EXPECT_TRUE(h.dlg.showNameBox()->isChecked());
    h.dlg.showNameBox()->setChecked(false);
    EXPECT_FALSE(h.mod.showName);
    EXPECT_EQ(h.applied, 1);
    h.dlg.showNameBox()->setChecked(true);
    EXPECT_TRUE(h.mod.showName);
}

TEST(ModuleLookDialog, LinkedColoursMoveTogetherAndUnlinkedOnesDont) {
    Harness h;
    EXPECT_TRUE(h.dlg.sameColourBox()->isChecked());
    EXPECT_FALSE(h.dlg.resetButton()->isEnabled());
    h.dlg.pickColour(true, QColor(255, 136, 0));
    EXPECT_EQ(h.mod.outlineColor, QStringLiteral("#ff8800"));
    EXPECT_EQ(h.mod.nameColor, QStringLiteral("#ff8800"));
    EXPECT_TRUE(h.dlg.resetButton()->isEnabled());
    EXPECT_EQ(h.dlg.nameButton()->text(), QStringLiteral("#ff8800"));

    h.dlg.sameColourBox()->setChecked(false);
    EXPECT_FALSE(h.mod.sameColor);
    h.dlg.pickColour(false, QColor(0x11, 0x22, 0x33));
    EXPECT_EQ(h.mod.outlineColor, QStringLiteral("#ff8800"));
    EXPECT_EQ(h.mod.nameColor, QStringLiteral("#112233"));
}

TEST(ModuleLookDialog, ResetToDefaultBringsBackTheLightBlue) {
    Harness h;
    h.dlg.sameColourBox()->setChecked(false);
    h.dlg.pickColour(false, QColor(0x11, 0x22, 0x33));
    h.dlg.resetButton()->click();
    EXPECT_TRUE(h.mod.outlineColor.isEmpty());
    EXPECT_TRUE(h.mod.nameColor.isEmpty());
    EXPECT_TRUE(h.mod.sameColor);
    EXPECT_TRUE(h.dlg.sameColourBox()->isChecked());
    EXPECT_FALSE(h.dlg.resetButton()->isEnabled());
}

TEST(ModuleLookDialog, FollowsChangesFromElsewhereAndClosesWhenTheModuleIsGone) {
    Harness h;
    h.mod.showName = false;  // someone else hid it
    h.dlg.refresh();
    EXPECT_FALSE(h.dlg.showNameBox()->isChecked());
    EXPECT_EQ(h.applied, 0);  // reading it back writes nothing
    h.dlg.show();
    h.gone = true;
    h.dlg.refresh();
    QCoreApplication::processEvents();
    EXPECT_FALSE(h.dlg.isVisible());
}

TEST(ModulesPanel, TheMoreButtonOpensTheModuleMenuWithItsLook) {
    core::Map map;
    core::Module m;
    m.id = QStringLiteral("m1");
    m.name = QStringLiteral("Harbour");
    m.showName = false;
    map.sidecar.modules.push_back(m);
    ui::ModulesPanel panel;
    panel.setMap(&map);
    EXPECT_FALSE(panel.moreButton()->isEnabled());
    auto* list = panel.findChild<QListWidget*>();
    ASSERT_NE(list, nullptr);
    list->setCurrentRow(0);
    ASSERT_TRUE(panel.moreButton()->isEnabled());

    QString lookFor;
    bool showAsked = true;
    QObject::connect(&panel, &ui::ModulesPanel::lookRequested, [&](const QString& id) { lookFor = id; });
    QObject::connect(&panel, &ui::ModulesPanel::showNameRequested, [&](const QString&, bool on) { showAsked = on; });
    bool checkedShown = true;
    QTimer::singleShot(0, [&] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        ASSERT_NE(menu, nullptr);
        for (QAction* a : menu->actions()) {
            if (a->text() == QStringLiteral("Show name")) checkedShown = a->isChecked();
            if (a->text() == QStringLiteral("Colours...")) a->trigger();
        }
        menu->close();
    });
    panel.moreButton()->click();
    EXPECT_FALSE(checkedShown);  // the menu shows the module's own setting
    EXPECT_EQ(lookFor, QStringLiteral("m1"));
    EXPECT_TRUE(showAsked);
}
