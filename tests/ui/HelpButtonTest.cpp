// The "?" help button: a quiet circle at rest, the one-sentence tooltip on
// hover or keyboard focus (not on a click), the popover on click with
// "Show me" and "Learn more", Esc closing it and handing focus back, and
// hiding everywhere while Settings › Show help buttons is off. Then the
// Help menu and the main window's "?"s.

#include "ui/MainWindow.h"
#include "ui/UpdateCheck.h"
#include "ui/help/HelpButton.h"
#include "ui/help/HelpPages.h"
#include "ui/help/HelpTexts.h"
#include "ui/help/ShortcutsDialog.h"
#include "ui/theme/AppPrefs.h"
#include "ui/theme/PanelHeader.h"

#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QToolBar>

using namespace bld;
using namespace bld::ui;
using namespace bld::ui::help;

namespace {

void hover(QWidget* w) {
    QEnterEvent enter(QPointF(5, 5), QPointF(5, 5), w->mapToGlobal(QPointF(5, 5)));
    QApplication::sendEvent(w, &enter);
}

void unhover(QWidget* w) {
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(w, &leave);
}

// A small window: a field, its "?" (explaining snap) and another field.
class HelpButtonTest : public ::testing::Test {
protected:
    void SetUp() override {
        QSettings().remove(kGroup);
        store_ = std::make_unique<theme::PrefsStore>(QString::fromLatin1(kGroup));
        host_ = std::make_unique<QWidget>();
        auto* row = new QHBoxLayout(host_.get());
        before_ = new QLineEdit(host_.get());
        row->addWidget(before_);
        button_ = new HelpButton(QStringLiteral("toolbar.snap"), *store_, host_.get(), before_);
        row->addWidget(button_);
        after_ = new QLineEdit(host_.get());
        row->addWidget(after_);
        host_->resize(400, 60);
        host_->show();
        host_->activateWindow();
        ASSERT_TRUE(QTest::qWaitForWindowActive(host_.get()));
        opened_.clear();
        previousOpener_ = setHelpPageOpener([this](const QUrl& url) { opened_ << url; });
    }
    void TearDown() override {
        setHelpPageOpener(previousOpener_);
        host_.reset();
        store_.reset();
        QSettings().remove(kGroup);
    }
    void setHelpIcons(bool on) {
        theme::AppPrefs p = store_->prefs();
        p.helpIcons = on;
        store_->update(p);
    }

    static constexpr const char* kGroup = "test-help-button";
    std::unique_ptr<theme::PrefsStore> store_;
    std::unique_ptr<QWidget> host_;
    QLineEdit* before_ = nullptr;
    QLineEdit* after_ = nullptr;
    HelpButton* button_ = nullptr;
    QList<QUrl> opened_;
    UrlOpener previousOpener_;
};

}  // namespace

TEST_F(HelpButtonTest, RestsQuietly) {
    EXPECT_TRUE(button_->isVisible());
    EXPECT_EQ(button_->tooltip(), nullptr);
    EXPECT_EQ(button_->popover(), nullptr);
    EXPECT_EQ(button_->accessibleName(), QStringLiteral("Help: Snap"));
    EXPECT_EQ(button_->focusPolicy(), Qt::StrongFocus);
    EXPECT_EQ(button_->target(), before_);
}

TEST_F(HelpButtonTest, HoverShowsTheShortText) {
    hover(button_);
    ASSERT_NE(button_->tooltip(), nullptr);
    EXPECT_EQ(button_->tooltip()->text(), helpEntry(QStringLiteral("toolbar.snap"))->shortText);
    // Under the button.
    EXPECT_GT(button_->tooltip()->geometry().top(), button_->mapToGlobal(QPoint(0, 0)).y());
    unhover(button_);
    EXPECT_EQ(button_->tooltip(), nullptr);
}

TEST_F(HelpButtonTest, KeyboardFocusShowsTheShortTextButAClickDoesNot) {
    before_->setFocus();
    QTest::keyClick(before_, Qt::Key_Tab);
    ASSERT_TRUE(button_->hasFocus());
    ASSERT_NE(button_->tooltip(), nullptr);
    QTest::keyClick(button_, Qt::Key_Tab);
    EXPECT_EQ(button_->tooltip(), nullptr);

    button_->setFocus(Qt::MouseFocusReason);
    EXPECT_EQ(button_->tooltip(), nullptr);
}

TEST_F(HelpButtonTest, ClickOpensThePopover) {
    hover(button_);
    QTest::mouseClick(button_, Qt::LeftButton);
    QWidget* pop = button_->popover();
    ASSERT_NE(pop, nullptr);
    EXPECT_EQ(button_->tooltip(), nullptr);  // the popover replaces the tooltip
    const auto entry = helpEntry(QStringLiteral("toolbar.snap"));
    EXPECT_EQ(pop->findChild<QLabel*>(QStringLiteral("HelpTitle"))->text(), entry->title);
    EXPECT_EQ(pop->findChild<QLabel*>(QStringLiteral("HelpMore"))->text(), entry->more);
    EXPECT_TRUE(pop->findChild<QPushButton*>(QStringLiteral("HelpShowMe"))->isVisible());
    // Snap has no help page: no Learn more.
    EXPECT_FALSE(pop->findChild<QPushButton*>(QStringLiteral("HelpLearnMore"))->isVisible());
    // Enter on the button does the same as a click (it toggles).
    button_->closePopover(true);
    QTest::keyClick(button_, Qt::Key_Return);
    EXPECT_NE(button_->popover(), nullptr);
}

TEST_F(HelpButtonTest, EscClosesThePopoverAndGivesFocusBack) {
    QTest::mouseClick(button_, Qt::LeftButton);
    QWidget* pop = button_->popover();
    ASSERT_NE(pop, nullptr);
    // From the popover itself, and from its Show me button.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    EXPECT_EQ(button_->popover(), nullptr);
    EXPECT_TRUE(button_->hasFocus());
    // Focus handed back doesn't bring the tooltip straight back.
    EXPECT_EQ(button_->tooltip(), nullptr);

    button_->showPopover();
    auto* showMe = button_->popover()->findChild<QPushButton*>(QStringLiteral("HelpShowMe"));
    showMe->setFocus(Qt::TabFocusReason);
    QTest::keyClick(showMe, Qt::Key_Escape);
    EXPECT_EQ(button_->popover(), nullptr);
    EXPECT_TRUE(button_->hasFocus());

    // Opened while another field had focus: Esc still hands focus to the
    // "?", not back to that field.
    after_->setFocus(Qt::TabFocusReason);
    ASSERT_TRUE(after_->hasFocus());
    button_->showPopover();
    ASSERT_NE(button_->popover(), nullptr);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    EXPECT_EQ(button_->popover(), nullptr);
    EXPECT_TRUE(button_->hasFocus());
    EXPECT_FALSE(after_->hasFocus());
    EXPECT_EQ(button_->tooltip(), nullptr);
}

TEST_F(HelpButtonTest, EscOnTheButtonHidesTheTooltip) {
    button_->setFocus(Qt::TabFocusReason);
    ASSERT_NE(button_->tooltip(), nullptr);
    QTest::keyClick(button_, Qt::Key_Escape);
    EXPECT_EQ(button_->tooltip(), nullptr);
}

TEST_F(HelpButtonTest, ShowMePulsesTheControl) {
    button_->showPopover();
    button_->popover()->findChild<QPushButton*>(QStringLiteral("HelpShowMe"))->click();
    EXPECT_EQ(button_->popover(), nullptr);
    auto* pulse = host_->findChild<QWidget*>(QStringLiteral("HelpPulse"));
    ASSERT_NE(pulse, nullptr);
    EXPECT_TRUE(pulse->isVisible());
    EXPECT_TRUE(pulse->geometry().contains(before_->geometry()));
    EXPECT_FALSE(pulse->geometry().contains(after_->geometry()));
    EXPECT_TRUE(pulse->testAttribute(Qt::WA_TransparentForMouseEvents));
}

TEST_F(HelpButtonTest, LearnMoreOpensTheHelpPageSection) {
    HelpButton sheets(QStringLiteral("panel.sheets"), *store_, host_.get());
    sheets.show();
    sheets.showPopover();
    auto* learn = sheets.popover()->findChild<QPushButton*>(QStringLiteral("HelpLearnMore"));
    ASSERT_TRUE(learn->isVisible());
    learn->click();
    ASSERT_EQ(opened_.size(), 1);
    EXPECT_TRUE(opened_[0].isLocalFile());
    EXPECT_TRUE(opened_[0].toLocalFile().endsWith(QLatin1String("/Getting_Started.htm")));
    EXPECT_EQ(opened_[0].fragment(), QStringLiteral("sheets"));
    EXPECT_EQ(sheets.popover(), nullptr);
    // Only sections the page has, and only paths.
    EXPECT_TRUE(helpPageFor(QStringLiteral("/help#nowhere")).isEmpty());
    EXPECT_TRUE(helpPageFor(QStringLiteral("https://example.org/help#sheets")).isEmpty());
}

TEST_F(HelpButtonTest, HidesWhileHelpButtonsAreOff) {
    button_->showPopover();
    setHelpIcons(false);
    EXPECT_TRUE(button_->isHidden());
    EXPECT_EQ(button_->popover(), nullptr);
    // Made while off: hidden from the start.
    auto* late = new HelpButton(QStringLiteral("toolbar.rotateStep"), *store_, host_.get());
    host_->layout()->addWidget(late);
    EXPECT_FALSE(late->isVisible());
    setHelpIcons(true);
    EXPECT_TRUE(button_->isVisible());
    EXPECT_TRUE(late->isVisible());
}

TEST_F(HelpButtonTest, InAToolbarTheActionHides) {
    QToolBar bar;
    auto* b = new HelpButton(QStringLiteral("toolbar.snap"), *store_, &bar);
    QAction* a = bar.addWidget(b);
    b->setVisibilityAction(a);
    EXPECT_TRUE(a->isVisible());
    setHelpIcons(false);
    EXPECT_FALSE(a->isVisible());
    setHelpIcons(true);
    EXPECT_TRUE(a->isVisible());
}

TEST_F(HelpButtonTest, AnUnknownKeyNeverShows) {
    HelpButton unknown(QStringLiteral("no.such.key"), *store_, host_.get());
    EXPECT_TRUE(unknown.isHidden());
}

TEST_F(HelpButtonTest, PanelHeaderUsesIt) {
    QDockWidget dock(QStringLiteral("Sheets"));
    auto* header = theme::PanelHeader::install(&dock, QStringLiteral("panel.sheets"), *store_);
    ASSERT_NE(header->helpButton(), nullptr);
    EXPECT_EQ(header->helpButton()->key(), QStringLiteral("panel.sheets"));
    EXPECT_EQ(header->helpButton()->target(), &dock);
    QDockWidget plain(QStringLiteral("Plain"));
    EXPECT_EQ(theme::PanelHeader::install(&plain, QString(), *store_)->helpButton(), nullptr);
}

namespace {

// The app's own store (the main window's "?"s follow it), put back after.
class HelpInMainWindow : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        UpdateCheck::setCheckAtStartupEnabled(false);
        saved_ = theme::PrefsStore::instance().prefs();
        setHelpIcons(true);
        window_ = std::make_unique<MainWindow>(parts_);
    }
    void TearDown() override {
        window_.reset();
        theme::PrefsStore::instance().update(saved_);
        QStandardPaths::setTestModeEnabled(false);
    }
    static void setHelpIcons(bool on) {
        theme::AppPrefs p = theme::PrefsStore::instance().prefs();
        p.helpIcons = on;
        theme::PrefsStore::instance().update(p);
    }
    // Hidden, directly or through its toolbar action.
    static bool hidden(HelpButton* b) {
        if (auto* bar = qobject_cast<QToolBar*>(b->parentWidget()))
            for (QAction* a : bar->actions())
                if (bar->widgetForAction(a) == b) return !a->isVisible();
        return b->isHidden();
    }
    QAction* action(const QString& name) { return window_->findChild<QAction*>(name); }

    parts::PartsLibrary parts_;
    std::unique_ptr<MainWindow> window_;
    theme::AppPrefs saved_;
};

}  // namespace

TEST_F(HelpInMainWindow, PanelsToolbarAndStatusBarHaveTheirHelp) {
    QStringList keys;
    for (HelpButton* b : window_->findChildren<HelpButton*>()) {
        keys << b->key();
        EXPECT_TRUE(helpEntry(b->key())) << b->key().toStdString();
    }
    for (const char* k : { "panel.parts", "panel.sheets", "panel.partsList", "panel.modules", "panel.moduleLibrary",
                           "panel.roomLibrary", "toolbar.snap", "toolbar.rotateStep", "toolbar.paintColour",
                           "dialog.measure", "topbar.tasks", "status.sheet", "status.room", "status.budget" })
        EXPECT_TRUE(keys.contains(QLatin1String(k))) << k;
}

TEST_F(HelpInMainWindow, HelpMenuTurnsHelpButtonsOffAndOn) {
    QAction* toggle = action(QStringLiteral("help.toggleButtons"));
    ASSERT_NE(toggle, nullptr);
    EXPECT_EQ(toggle->text(), QStringLiteral("Turn Help Buttons &Off"));
    const auto buttons = window_->findChildren<HelpButton*>();
    ASSERT_FALSE(buttons.isEmpty());
    toggle->trigger();
    EXPECT_FALSE(theme::PrefsStore::instance().prefs().helpIcons);
    EXPECT_EQ(toggle->text(), QStringLiteral("Turn Help Buttons &On"));
    for (HelpButton* b : buttons) EXPECT_TRUE(hidden(b)) << b->key().toStdString();
    toggle->trigger();
    EXPECT_TRUE(theme::PrefsStore::instance().prefs().helpIcons);
    for (HelpButton* b : buttons)
        if (b->key().startsWith(QLatin1String("panel.")) || b->parentWidget()->inherits("QToolBar"))
            EXPECT_FALSE(hidden(b)) << b->key().toStdString();
}

TEST_F(HelpInMainWindow, HelpMenuHasGettingStartedAndShortcuts) {
    ASSERT_NE(action(QStringLiteral("help.gettingStarted")), nullptr);
    ASSERT_NE(action(QStringLiteral("help.shortcuts")), nullptr);
    QList<QUrl> opened;
    const UrlOpener previous = setHelpPageOpener([&opened](const QUrl& u) { opened << u; });
    action(QStringLiteral("help.gettingStarted"))->trigger();
    setHelpPageOpener(previous);
    ASSERT_EQ(opened.size(), 1);
    EXPECT_TRUE(opened[0].toLocalFile().endsWith(QLatin1String("/Getting_Started.htm")));
}

TEST_F(HelpInMainWindow, ShortcutsComeFromTheMenus) {
    const auto groups = collectShortcuts(window_->menuBar());
    ASSERT_GE(groups.size(), 3);
    const auto find = [&groups](const QString& does) -> QString {
        for (const auto& g : groups)
            for (const auto& s : g.items)
                if (s.does == does) return g.title + QLatin1Char('|') + s.keys;
        return {};
    };
    EXPECT_EQ(find(QStringLiteral("Save")),
              QStringLiteral("File|") + QKeySequence(QKeySequence::Save).toString(QKeySequence::NativeText));
    EXPECT_EQ(find(QStringLiteral("Contents")),
              QStringLiteral("Help|") + QKeySequence(QKeySequence::HelpContents).toString(QKeySequence::NativeText));
    EXPECT_EQ(groups.last().title, QStringLiteral("On the map"));
    // A shortcut added to a menu item shows up.
    auto* extra = window_->menuBar()->actions().first()->menu()->addAction(QStringLiteral("&Brand new..."));
    extra->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+J")));
    EXPECT_EQ(find(QStringLiteral("Brand new")), QString());  // groups were read before
    const auto again = collectShortcuts(window_->menuBar());
    bool found = false;
    for (const auto& s : again.first().items) found = found || s.does == QLatin1String("Brand new");
    EXPECT_TRUE(found);
}

// Where parts come from is one Tools > Parts submenu with plain words, not
// three loose "Parts Library" items spread over the Tools menu.
TEST_F(HelpInMainWindow, PartsActionsLiveInOneToolsPartsMenu) {
    QMenu* tools = nullptr;
    for (QAction* a : window_->menuBar()->actions())
        if (a->menu() && a->text().remove(QLatin1Char('&')) == QLatin1String("Tools")) tools = a->menu();
    ASSERT_NE(tools, nullptr);

    QMenu* parts = nullptr;
    QStringList loose;
    for (QAction* a : tools->actions()) {
        const QString text = a->text().remove(QLatin1Char('&'));
        if (a->menu() && text == QLatin1String("Parts")) parts = a->menu();
        else if (!a->menu() && text.contains(QLatin1String("Parts"))) loose << text;
    }
    ASSERT_NE(parts, nullptr);
    EXPECT_TRUE(loose.isEmpty()) << loose.join(QStringLiteral(", ")).toStdString();

    QStringList inParts;
    for (QAction* a : parts->actions())
        if (!a->isSeparator()) inParts << a->text().remove(QLatin1Char('&'));
    EXPECT_EQ(inParts, (QStringList{ QStringLiteral("Get More Parts..."), QStringLiteral("Parts Folders..."),
                                     QStringLiteral("Reload Parts") }));
    for (QAction* a : parts->actions())
        if (!a->isSeparator()) EXPECT_FALSE(a->toolTip().isEmpty()) << a->text().toStdString();
}
