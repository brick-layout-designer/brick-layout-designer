// Version comparison for the update check.

#include "ui/UpdateCheck.h"

#include <gtest/gtest.h>

using bld::ui::isNewerVersion;

TEST(UpdateCheck, ComparesReleaseVersions) {
    EXPECT_TRUE(isNewerVersion(QStringLiteral("v1.3.0"), QStringLiteral("1.2.0")));
    EXPECT_TRUE(isNewerVersion(QStringLiteral("1.10.0"), QStringLiteral("1.9.9")));
    EXPECT_TRUE(isNewerVersion(QStringLiteral("2.0"), QStringLiteral("1.99.99")));
    EXPECT_FALSE(isNewerVersion(QStringLiteral("v1.2.0"), QStringLiteral("1.2.0")));
    EXPECT_FALSE(isNewerVersion(QStringLiteral("1.2"), QStringLiteral("1.2.0")));
    EXPECT_FALSE(isNewerVersion(QStringLiteral("1.1.9"), QStringLiteral("1.2.0")));
}

TEST(UpdateCheck, PreReleasesAndJunk) {
    EXPECT_TRUE(isNewerVersion(QStringLiteral("1.3.0"), QStringLiteral("1.3.0-beta.2")));
    EXPECT_FALSE(isNewerVersion(QStringLiteral("1.3.0-beta.1"), QStringLiteral("1.3.0")));
    EXPECT_TRUE(isNewerVersion(QStringLiteral("1.4.0-rc.1"), QStringLiteral("1.3.0")));
    EXPECT_FALSE(isNewerVersion(QStringLiteral("nightly"), QStringLiteral("1.2.0")));
    EXPECT_FALSE(isNewerVersion(QStringLiteral("1.3.0"), QStringLiteral("")));
    EXPECT_FALSE(isNewerVersion(QString(), QStringLiteral("1.2.0")));
}

// ---------------------------------------------------------------------------
// The update notice: a friendly card over the map, not a box.

#include "ui/MainWindow.h"
#include "ui/NoticeArea.h"
#include "parts/PartsLibrary.h"

#include <QApplication>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QToolButton>

using namespace bld;

namespace {

ui::ReleaseInfo release(const char* version, bool important = false) {
    ui::ReleaseInfo r;
    r.version = QString::fromLatin1(version);
    r.url = QStringLiteral("https://example.org/release");
    r.notes = QStringLiteral("## What's new\n\n- Faster loading");
    r.important = important;
    return r;
}

QStringList buttonsOf(QWidget* card) {
    QStringList out;
    for (auto* b : card->findChildren<QPushButton*>()) out << b->text();
    return out;
}

QString titleOf(QWidget* card) { return card->findChild<QLabel*>(QStringLiteral("NoticeTitle"))->text(); }

bool waitFor(const std::function<bool()>& done) {
    for (int i = 0; i < 200 && !done(); ++i) QTest::qWait(10);
    return done();
}

}  // namespace

TEST(UpdateCheck, ReadsAReleaseAndItsImportantMarker) {
    const ui::ReleaseInfo r = ui::readRelease(
        QJsonObject{ { QStringLiteral("tag_name"), QStringLiteral("v1.3.1") },
                     { QStringLiteral("body"), QStringLiteral("Fixes a sign-in problem. [Security]\n") } });
    EXPECT_EQ(r.version, QStringLiteral("1.3.1"));
    EXPECT_TRUE(r.important);
    EXPECT_EQ(r.notes, QStringLiteral("Fixes a sign-in problem. [Security]"));
    EXPECT_EQ(r.url, QStringLiteral("https://github.com/brick-layout-designer/brick-layout-designer/releases/latest"));
    EXPECT_TRUE(ui::isImportantRelease(QStringLiteral("[important] please update")));
    EXPECT_FALSE(ui::isImportantRelease(QStringLiteral("An important new tool")));
}

TEST(UpdateCheck, OffersNewerReleasesUnlessSkipped) {
    QStandardPaths::setTestModeEnabled(true);
    QSettings().remove(QStringLiteral("updates/skippedVersion"));
    QWidget parent;
    ui::UpdateCheck check(&parent);
    QSignalSpy offered(&check, &ui::UpdateCheck::updateAvailable);
    check.offer(release("1.3.0"), QStringLiteral("1.3.0"));
    check.offer(release("1.2.0"), QStringLiteral("1.3.0"));
    EXPECT_EQ(offered.size(), 0);
    check.offer(release("1.4.0"), QStringLiteral("1.3.0"));
    EXPECT_EQ(offered.size(), 1);
    ui::UpdateCheck::skipVersion(QStringLiteral("1.4.0"));
    check.offer(release("1.4.0"), QStringLiteral("1.3.0"));
    EXPECT_EQ(offered.size(), 1);
    // A newer one than the skipped version, or an important fix, still comes.
    check.offer(release("1.4.1"), QStringLiteral("1.3.0"));
    check.offer(release("1.4.0", true), QStringLiteral("1.3.0"));
    EXPECT_EQ(offered.size(), 3);
    QSettings().remove(QStringLiteral("updates/skippedVersion"));
}

TEST(UpdateCheck, ShowsANoticeWithDownloadSkipAndLater) {
    QStandardPaths::setTestModeEnabled(true);
    ui::UpdateCheck::setCheckAtStartupEnabled(false);
    QSettings().remove(QStringLiteral("updates/skippedVersion"));
    parts::PartsLibrary lib;
    ui::MainWindow window(lib);
    window.resize(1200, 800);
    window.show();
    auto* check = window.findChild<ui::UpdateCheck*>();
    ASSERT_NE(check, nullptr);
    check->offer(release("9.0.0"), QStringLiteral("1.3.0"));
    QWidget* card = window.notices()->card(QStringLiteral("update"));
    ASSERT_NE(card, nullptr);
    EXPECT_TRUE(card->isVisible());
    EXPECT_EQ(titleOf(card), QStringLiteral("Version 9.0.0 is ready"));
    EXPECT_EQ(buttonsOf(card), (QStringList{ QStringLiteral("What's new"), QStringLiteral("Download"),
                                             QStringLiteral("Skip This Version"), QStringLiteral("Later") }));
    // Not a modal box: nothing waits on it.
    EXPECT_EQ(QApplication::activeModalWidget(), nullptr);
    // What's new shows the notes under the card.
    auto* notes = card->findChild<QScrollArea*>(QStringLiteral("NoticeDetails"));
    ASSERT_NE(notes, nullptr);
    EXPECT_FALSE(notes->isVisible());
    card->findChild<QPushButton*>(QStringLiteral("NoticeWhatsNew"))->click();
    EXPECT_TRUE(notes->isVisible());
    // Skip this version: remembered, and the card goes.
    for (auto* b : card->findChildren<QPushButton*>())
        if (b->text() == QStringLiteral("Skip This Version")) b->click();
    ASSERT_TRUE(waitFor([&] { return !window.notices()->isShown(QStringLiteral("update")); }));
    EXPECT_EQ(ui::UpdateCheck::skippedVersion(), QStringLiteral("9.0.0"));
    QSettings().remove(QStringLiteral("updates/skippedVersion"));

    // An important update says so and can't be skipped.
    check->offer(release("9.0.1", true), QStringLiteral("1.3.0"));
    card = window.notices()->card(QStringLiteral("update"));
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(titleOf(card), QStringLiteral("Important: Version 9.0.1 is ready"));
    EXPECT_TRUE(card->property("important").toBool());
    EXPECT_FALSE(buttonsOf(card).contains(QStringLiteral("Skip This Version")));
    // The ✕ hides it.
    card->findChild<QToolButton*>(QStringLiteral("NoticeClose"))->click();
    ASSERT_TRUE(waitFor([&] { return !window.notices()->isShown(QStringLiteral("update")); }));
}
