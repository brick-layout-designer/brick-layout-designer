// Edit > Settings...: shows the current settings, every change goes
// straight to the store, the note names the connected server's host, and
// "More options..." opens Preferences. No Expert mode switch.

#include "ui/SettingsDialog.h"
#include "ui/theme/AppPrefs.h"
#include "ServerList.h"

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTest>
#include <QToolButton>

using namespace bld::ui;
using namespace bld::ui::theme;

namespace {

class SettingsDialogTest : public ::testing::Test {
protected:
    void SetUp() override { QSettings().remove(kGroup); }
    void TearDown() override { QSettings().remove(kGroup); }
    static constexpr const char* kGroup = "test-settings-dialog";
};

}  // namespace

TEST_F(SettingsDialogTest, ShowsTheSettingsAndChangesThemAtOnce) {
    PrefsStore store(QString::fromLatin1(kGroup));
    AppPrefs p;
    p.accent = QStringLiteral("ocean");
    p.helpIcons = false;
    store.update(p);

    SettingsDialog dialog(store, QString());
    EXPECT_TRUE(dialog.findChild<QToolButton*>(QStringLiteral("theme_system"))->isChecked());
    EXPECT_TRUE(dialog.findChild<QToolButton*>(QStringLiteral("accent_ocean"))->isChecked());
    EXPECT_FALSE(dialog.findChild<QToolButton*>(QStringLiteral("accent_brick"))->isChecked());
    EXPECT_FALSE(dialog.findChild<QCheckBox*>(QStringLiteral("helpIcons"))->isChecked());
    EXPECT_EQ(dialog.findChild<QAbstractButton*>(QStringLiteral("expertMode")), nullptr);

    int changes = 0;
    QObject::connect(&store, &PrefsStore::changed, [&changes](bool local) { changes += local ? 1 : 100; });
    dialog.findChild<QToolButton*>(QStringLiteral("theme_dark"))->click();
    EXPECT_EQ(store.prefs().theme, ThemeChoice::Dark);
    dialog.findChild<QToolButton*>(QStringLiteral("accent_sunny"))->click();
    EXPECT_EQ(store.prefs().accent, QStringLiteral("sunny"));
    dialog.findChild<QCheckBox*>(QStringLiteral("largeText"))->click();
    EXPECT_TRUE(store.prefs().largeText);
    dialog.findChild<QCheckBox*>(QStringLiteral("helpIcons"))->click();
    EXPECT_TRUE(store.prefs().helpIcons);
    EXPECT_EQ(changes, 4);
    EXPECT_EQ(store.prefs().accent, QStringLiteral("sunny"));  // later changes kept the earlier ones

    // A change from elsewhere (a server pull) shows in the open dialog.
    AppPrefs theirs = store.prefs();
    theirs.accent = QStringLiteral("plum");
    store.adopt(theirs);
    EXPECT_TRUE(dialog.findChild<QToolButton*>(QStringLiteral("accent_plum"))->isChecked());
}

TEST_F(SettingsDialogTest, TheNoteNamesTheConnectedServer) {
    PrefsStore store(QString::fromLatin1(kGroup));
    SettingsDialog connected(store, QStringLiteral("layouts.club.example:8443"));
    EXPECT_TRUE(connected.syncNote()->text().contains(QStringLiteral("Synced with layouts.club.example:8443")))
        << connected.syncNote()->text().toStdString();

    SettingsDialog offline(store, QString());
    EXPECT_FALSE(offline.syncNote()->text().contains(QStringLiteral("Synced with")));
    EXPECT_TRUE(offline.syncNote()->text().contains(QStringLiteral("this computer")));
}

TEST_F(SettingsDialogTest, MoreOptionsOpensPreferences) {
    PrefsStore store(QString::fromLatin1(kGroup));
    int opened = 0;
    SettingsDialog dialog(store, QString(), [&opened] { ++opened; });
    auto* more = dialog.findChild<QPushButton*>(QStringLiteral("moreOptions"));
    ASSERT_NE(more, nullptr);
    more->click();
    EXPECT_EQ(opened, 1);

    SettingsDialog without(store, QString());
    EXPECT_EQ(without.findChild<QPushButton*>(QStringLiteral("moreOptions")), nullptr);
}

TEST_F(SettingsDialogTest, ServersSaysWhatIsSetUpAndOpensServers) {
    const QVariant before = QSettings().value(QLatin1String(bld::sync::ServerList::kKey));
    QSettings().setValue(QLatin1String(bld::sync::ServerList::kKey), QByteArray("[]"));
    PrefsStore store(QString::fromLatin1(kGroup));
    {
        SettingsDialog dlg(store, QString());
        auto* note = dlg.findChild<QLabel*>(QStringLiteral("serversNote"));
        auto* manage = dlg.findChild<QPushButton*>(QStringLiteral("manageServers"));
        ASSERT_NE(note, nullptr);
        ASSERT_NE(manage, nullptr);
        EXPECT_TRUE(note->text().startsWith(QStringLiteral("No servers yet")));
        EXPECT_TRUE(manage->isHidden());  // nothing to open it with

        // Opened from the app: the button opens Servers; a server added there shows at once.
        int opened = 0;
        dlg.setManageServers([&opened] {
            ++opened;
            bld::sync::ServerList list;
            list.add(QUrl(QStringLiteral("https://layouts.example.org")), QStringLiteral("Club server"));
            list.add(QUrl(QStringLiteral("https://other.example.org")), QString());
            list.save();
        });
        EXPECT_FALSE(manage->isHidden());
        EXPECT_EQ(manage->text(), QStringLiteral("Add a server…"));
        manage->click();
        ASSERT_TRUE(QTest::qWaitFor([&] { return opened == 1; }, 2000));
        EXPECT_EQ(note->text(), QStringLiteral("Main: Club server (layouts.example.org) and 1 more"));
        EXPECT_EQ(manage->text(), QStringLiteral("Manage servers…"));
    }
    QSettings().setValue(QLatin1String(bld::sync::ServerList::kKey), before);
}
