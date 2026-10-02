// A layout file saved from a server layout says so (manifest "source"):
// opening it shows a card, never a connection. From one of your servers it
// offers the live version or keeping the copy; from another server, adding
// it to your servers. Saving keeps the source and the manifest's unknown
// fields; a local layout gets none.

#include "ServerList.h"
#include "core/Map.h"
#include "import/LayoutFile.h"
#include "parts/PartsLibrary.h"
#include "ui/MainWindow.h"
#include "ui/NoticeArea.h"
#include "ui/UpdateCheck.h"
#include "ui/theme/ThemeManager.h"
#include "FakeHttp.h"

#include <gtest/gtest.h>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace bld;

namespace {

const QString kFixture = QStringLiteral(BLD_SOURCE_DIR "/fixtures/layouts/web-made-source.bld-layout");
const QString kId = QStringLiteral("layoutSource");

class TestWindow : public ui::MainWindow {
public:
    using ui::MainWindow::MainWindow;
    using ui::MainWindow::manifestExtras;
    using ui::MainWindow::newDocument;
    using ui::MainWindow::writeMapTo;
    QList<import::LayoutSource> openedLive;

protected:
    void openSourceLive(const import::LayoutSource& source) override { openedLive << source; }
};

class LayoutSourceCard : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ui::UpdateCheck::setCheckAtStartupEnabled(false);
        QSettings().remove(QStringLiteral("sync/serverAddress"));
        QSettings().setValue(QLatin1String(sync::ServerList::kKey), QByteArray("[]"));
        window_ = std::make_unique<TestWindow>(parts_);
    }
    void TearDown() override {
        window_.reset();
        QSettings().setValue(QLatin1String(sync::ServerList::kKey), QByteArray("[]"));
        QStandardPaths::setTestModeEnabled(false);
    }
    static void addServer(const QString& url, const QString& name) {
        auto list = sync::ServerList::load();
        list.add(QUrl(url), name);
        list.save();
    }
    QFrame* card() const { return window_->notices()->card(kId); }
    QString title() const { return card()->findChild<QLabel*>(QStringLiteral("NoticeTitle"))->text(); }
    QPushButton* button(const QString& text) const {
        for (auto* b : card()->findChildren<QPushButton*>())
            if (b->text() == text) return b;
        return nullptr;
    }
    static void settle() {
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
    }

    parts::PartsLibrary parts_;
    std::unique_ptr<TestWindow> window_;
};

}  // namespace

TEST_F(LayoutSourceCard, FromOneOfYourServersOffersTheLiveVersion) {
    addServer(QStringLiteral("https://collab.example.org"), QStringLiteral("Club server"));
    ASSERT_TRUE(window_->openFile(kFixture));
    ASSERT_TRUE(window_->notices()->isShown(kId));
    EXPECT_EQ(title(), QStringLiteral("This layout is on Club server"));
    EXPECT_TRUE(card()->findChild<QLabel*>(QStringLiteral("NoticeText"))->text().contains(QStringLiteral("“Show 2026”")));
    EXPECT_EQ(button(QStringLiteral("Add collab.example.org to your servers")), nullptr);
    // Nothing connects until it's asked to.
    settle();
    EXPECT_TRUE(window_->openedLive.isEmpty());

    QPushButton* open = button(QStringLiteral("Open the live version"));
    ASSERT_NE(open, nullptr);
    open->click();
    settle();
    ASSERT_EQ(window_->openedLive.size(), 1);
    EXPECT_EQ(window_->openedLive.first().server, QStringLiteral("https://collab.example.org"));
    EXPECT_EQ(window_->openedLive.first().layoutId, QStringLiteral("L-42"));
}

TEST_F(LayoutSourceCard, KeepWorkingOnTheCopyJustClosesTheCard) {
    addServer(QStringLiteral("https://collab.example.org"), QString());
    ASSERT_TRUE(window_->openFile(kFixture));
    QPushButton* keep = button(QStringLiteral("Keep working on this copy"));
    ASSERT_NE(keep, nullptr);
    keep->click();
    settle();
    EXPECT_FALSE(window_->notices()->isShown(kId));
    EXPECT_TRUE(window_->openedLive.isEmpty());
    // The copy still knows where it came from, for its next save.
    ASSERT_TRUE(window_->manifestExtras().source);
    EXPECT_EQ(window_->manifestExtras().source->layoutId, QStringLiteral("L-42"));
}

TEST_F(LayoutSourceCard, FromAnotherServerOffersToAddItThenTheLiveVersion) {
    // In the app's own look, as the card is built in it.
    using namespace ui::theme;
    qApp->setStyleSheet(buildStyleSheet(Mode::Light, accent(QLatin1String(kDefaultAccent))));
    addServer(QStringLiteral("https://elsewhere.example.org"), QString());
    ASSERT_TRUE(window_->openFile(kFixture));
    EXPECT_EQ(title(), QStringLiteral("This layout came from collab.example.org"));
    EXPECT_EQ(button(QStringLiteral("Open the live version")), nullptr);
    QPushButton* add = button(QStringLiteral("Add collab.example.org to your servers"));
    ASSERT_NE(add, nullptr);
    // A long address doesn't cut the buttons off.
    {
        window_->resize(1280, 800);
        window_->show();
        for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
        for (auto* b : card()->findChildren<QPushButton*>()) {
            if (!b->isVisible()) continue;
            EXPECT_GE(b->width(), b->sizeHint().width()) << b->text().toStdString();
            EXPECT_LE(b->mapTo(card(), QPoint(b->width(), 0)).x(), card()->width()) << b->text().toStdString();
        }
        qApp->setStyleSheet(QString());
    }
    add->click();
    settle();
    const auto list = sync::ServerList::load();
    ASSERT_NE(list.find(QUrl(QStringLiteral("https://collab.example.org"))), nullptr);
    EXPECT_EQ(list.servers().size(), 2);
    // Now one of yours: the card offers the live version, still without connecting.
    ASSERT_TRUE(window_->notices()->isShown(kId));
    EXPECT_EQ(title(), QStringLiteral("This layout is on collab.example.org"));
    EXPECT_NE(button(QStringLiteral("Open the live version")), nullptr);
    EXPECT_TRUE(window_->openedLive.isEmpty());
}

TEST_F(LayoutSourceCard, SavingKeepsTheSourceAndUnknownFieldsAndANewLayoutHasNone) {
    ASSERT_TRUE(window_->openFile(kFixture));
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("copy.bld-layout"));
    ASSERT_TRUE(window_->writeMapTo(path));
    const auto saved = import::readLayoutFile(path, dir.filePath(QStringLiteral("assets")));
    ASSERT_TRUE(saved.ok()) << saved.error.toStdString();
    ASSERT_TRUE(saved.source);
    EXPECT_EQ(saved.source->layoutId, QStringLiteral("L-42"));
    EXPECT_EQ(saved.source->exportedAt, QStringLiteral("2026-10-01T12:00:00.000Z"));
    EXPECT_TRUE(saved.manifest.contains(QStringLiteral("futureField")));

    // Another file, or a new layout: no card, no source, nothing kept.
    ASSERT_TRUE(window_->newDocument());
    EXPECT_FALSE(window_->notices()->isShown(kId));
    EXPECT_FALSE(window_->manifestExtras().source);
    EXPECT_TRUE(window_->manifestExtras().keep.isEmpty());
    const QString local = dir.filePath(QStringLiteral("local.bld-layout"));
    ASSERT_TRUE(window_->writeMapTo(local));
    EXPECT_FALSE(import::readLayoutFile(local, dir.filePath(QStringLiteral("assets"))).source);
}
