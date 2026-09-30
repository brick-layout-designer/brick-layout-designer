// The main window with a live layout: offline edits, back in step, go
// through the compare window, which applies, discards, replaces the
// server's or saves mine as a new layout.

#include "FakeHttp.h"
#include "FakeSyncServer.h"

#include "ConnectDialog.h"
#include "CompareDialog.h"
#include "edit/EditCommands.h"
#include "parts/PartsLibrary.h"
#include "ui/LiveLayout.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/UpdateCheck.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QUndoStack>

#include <functional>

using namespace bld;
using namespace bld::synctest;
using namespace std::chrono_literals;
using Status = sync::SyncClient::Status;

namespace {

// Answers the modal dialogs that open while `run` runs, in order; a dialog
// beyond the answers is closed and counted as unexpected.
struct Answers {
    std::vector<std::function<void(QWidget*)>> steps;
    int unexpected = 0;
    std::size_t given = 0;
    void during(const std::function<void()>& run) {
        QTimer poll;
        poll.setInterval(20);
        QObject::connect(&poll, &QTimer::timeout, [&] {
            QWidget* modal = QApplication::activeModalWidget();
            if (!modal || !modal->isVisible()) return;
            if (given < steps.size()) steps[given++](modal);
            else {
                ++unexpected;
                modal->close();
            }
        });
        poll.start();
        run();
        poll.stop();
        EXPECT_EQ(given, steps.size()) << "fewer dialogs than expected";
    }
    // Every answer given and no dialog left open.
    bool done() const { return given == steps.size() && !QApplication::activeModalWidget(); }
};

// Presses the button, queued: what it opens next is answered by the next step.
std::function<void(QWidget*)> press(const QString& text) {
    return [text](QWidget* w) {
        for (auto* b : w->findChildren<QPushButton*>())
            if (QString(b->text()).remove(QLatin1Char('&')) == text) {
                QMetaObject::invokeMethod(b, &QPushButton::click, Qt::QueuedConnection);
                return;
            }
        ADD_FAILURE() << "no button " << text.toStdString() << " in " << w->metaObject()->className();
        w->close();
    };
}

// The compare window, then `text` pressed in it.
std::function<void(QWidget*)> compare(const QString& text) {
    return [text](QWidget* w) {
        EXPECT_NE(qobject_cast<sync::CompareDialog*>(w), nullptr) << w->metaObject()->className();
        press(text)(w);
    };
}

// A message box whose text contains `about`, answered with `button`.
std::function<void(QWidget*)> box(const QString& about, const QString& button) {
    return [about, button](QWidget* w) {
        auto* b = qobject_cast<QMessageBox*>(w);
        ASSERT_NE(b, nullptr) << w->metaObject()->className();
        EXPECT_TRUE(b->text().contains(about)) << b->text().toStdString();
        press(button)(w);
    };
}

core::Brick& brickAt(core::Map& m, int i) {
    for (auto& l : m.layers())
        if (l->kind() == core::LayerKind::Brick) return static_cast<core::LayerBrick&>(*l).bricks[i];
    throw std::runtime_error("no bricks");
}

QRectF brickArea(const sync::SyncDoc& d, int i) { return brickAt(*sync::mapFromDocJson(d.toJson()), i).displayArea; }

edit::BrickRef brickRef(core::Map& m, int i) {
    for (int l = 0; l < static_cast<int>(m.layers().size()); ++l)
        if (m.layers()[l]->kind() == core::LayerKind::Brick)
            return { l, static_cast<core::LayerBrick&>(*m.layers()[l]).bricks[i].guid };
    return {};
}

// openLive() without the connect dialog.
class TestWindow : public ui::MainWindow {
public:
    using ui::MainWindow::MainWindow;
    using ui::MainWindow::openLive;
};

QString liveCache() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/live");
}

class MainWindowLive : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ui::UpdateCheck::setCheckAtStartupEnabled(false);
        QDir(liveCache()).removeRecursively();
        http_.upgrade = [this](QTcpSocket* s) { ws_.take(s); };
        window_ = std::make_unique<TestWindow>(parts_);
        view_ = window_->findChild<ui::MapView*>();
        live_ = window_->findChild<ui::LiveLayout*>();
        ASSERT_TRUE(view_ && live_);
        live_->session().client().setReconnectDelays(50ms, 200ms);
        window_->openLive({ http_.base(), QStringLiteral("bld_pat_test"), QStringLiteral("L1"),
                           QStringLiteral("Show 2026"), false });
        ASSERT_TRUE(waitFor([&] { return session().status() == Status::Synced && view_->currentMap(); }));
        original_ = brickArea(ws_.doc, 0);
        otherOriginal_ = brickArea(ws_.doc, 1);
    }
    void TearDown() override {
        window_.reset();
        QDir(liveCache()).removeRecursively();
        QStandardPaths::setTestModeEnabled(false);
    }

    sync::SyncSession& session() { return live_->session(); }
    QAction* reviewAction() {
        for (QAction* a : window_->findChildren<QAction*>())
            if (a->text() == QStringLiteral("Review &Offline Changes...")) return a;
        return nullptr;
    }

    // Offline: I move brick 0 right by 8 in the view while someone else
    // moves brick 1 down by 16 on the server.
    void editOffline() {
        ws_.dropAll(QWebSocketProtocol::CloseCodeGoingAway);
        ASSERT_TRUE(waitFor([&] { return session().status() != Status::Synced; }));
        core::Map& m = *view_->currentMap();
        const QPointF at = brickAt(m, 0).displayArea.topLeft();
        view_->undoStack()->push(new edit::MoveBricksCommand(m, { { brickRef(m, 0), at, at + QPointF(8, 0) } }));
        ASSERT_TRUE(waitFor([&] { return session().offlineEdits().has_value(); }));
        ws_.remoteEdit([](core::Map& s) { brickAt(s, 1).displayArea.translate(0, 16); });
    }

    FakeHttp http_;
    FakeServer ws_;
    parts::PartsLibrary parts_;
    std::unique_ptr<TestWindow> window_;
    ui::MapView* view_ = nullptr;
    ui::LiveLayout* live_ = nullptr;
    QRectF original_, otherOriginal_;
};

}  // namespace

TEST_F(MainWindowLive, BackInStepTheCompareWindowOpensAndApplyMerges) {
    editOffline();
    Answers answers{ { compare(QStringLiteral("Apply")) } };
    answers.during([&] { ASSERT_TRUE(waitFor([&] { return answers.done() && !session().offlineEdits(); })); });
    EXPECT_EQ(answers.unexpected, 0);
    // Mine and theirs, both on the server.
    ASSERT_TRUE(waitFor([&] { return brickArea(ws_.doc, 0) == original_.translated(8, 0); }));
    EXPECT_EQ(brickArea(ws_.doc, 1), otherOriginal_.translated(0, 16));
    EXPECT_EQ(brickAt(*view_->currentMap(), 1).displayArea, otherOriginal_.translated(0, 16));
    EXPECT_FALSE(reviewAction()->isEnabled());
}

TEST_F(MainWindowLive, LaterKeepsThemForReviewOfflineChangesAndDiscardLeavesTheServer) {
    QAction* review = reviewAction();
    ASSERT_NE(review, nullptr);
    EXPECT_FALSE(review->isEnabled());
    editOffline();
    Answers later{ { compare(QStringLiteral("Later")) } };
    later.during([&] { ASSERT_TRUE(waitFor([&] { return later.done(); })); });
    EXPECT_EQ(later.unexpected, 0);
    ASSERT_TRUE(session().offlineEdits());
    EXPECT_TRUE(review->isEnabled());
    // The editor still shows mine.
    EXPECT_EQ(brickAt(*view_->currentMap(), 0).displayArea, original_.translated(8, 0));

    Answers discard{ { compare(QStringLiteral("Discard My Changes")),
                       box(QStringLiteral("Throw away"), QStringLiteral("Yes")) } };
    discard.during([&] { review->trigger(); });
    EXPECT_EQ(discard.unexpected, 0);
    EXPECT_FALSE(session().offlineEdits());
    EXPECT_FALSE(review->isEnabled());
    waitFor([] { return false; }, 150);
    EXPECT_EQ(brickArea(ws_.doc, 0), original_);
    EXPECT_EQ(brickArea(ws_.doc, 1), otherOriginal_.translated(0, 16));
    EXPECT_EQ(brickAt(*view_->currentMap(), 0).displayArea, original_);
}

TEST_F(MainWindowLive, ReplaceServerWithMineAsksThenPutsMyVersionInPlace) {
    editOffline();
    Answers answers{ { compare(QStringLiteral("Replace Server with Mine...")),
                       box(QStringLiteral("in place of the server's"), QStringLiteral("Yes")) } };
    answers.during([&] { ASSERT_TRUE(waitFor([&] { return answers.done() && !session().offlineEdits(); })); });
    EXPECT_EQ(answers.unexpected, 0);
    // Mine as it was: their move is undone.
    ASSERT_TRUE(waitFor([&] { return brickArea(ws_.doc, 0) == original_.translated(8, 0); }));
    ASSERT_TRUE(waitFor([&] { return brickArea(ws_.doc, 1) == otherOriginal_; }));
}

TEST_F(MainWindowLive, ReplaceServerCanBeCancelledAtTheConfirmation) {
    editOffline();
    Answers answers{ { compare(QStringLiteral("Replace Server with Mine...")),
                       box(QStringLiteral("in place of the server's"), QStringLiteral("No")),
                       compare(QStringLiteral("Later")) } };
    answers.during([&] { ASSERT_TRUE(waitFor([&] { return answers.done(); })); });
    EXPECT_EQ(answers.unexpected, 0);
    EXPECT_TRUE(session().offlineEdits());
    EXPECT_EQ(brickArea(ws_.doc, 0), original_);
}

TEST_F(MainWindowLive, SaveMineAsANewLayoutPublishesItAndLeavesTheLiveOne) {
    http_.reply("/api/layouts", 201,
                QJsonObject{ { QStringLiteral("id"), QStringLiteral("L2") },
                             { QStringLiteral("title"), QStringLiteral("Show 2026 (offline copy)") } });
    editOffline();
    Answers answers{ { compare(QStringLiteral("Save Mine as a New Layout...")),
                       box(QStringLiteral("saved as \"Show 2026 (offline copy)\""), QStringLiteral("OK")) } };
    answers.during([&] { ASSERT_TRUE(waitFor([&] { return answers.done() && !session().offlineEdits(); })); });
    EXPECT_EQ(answers.unexpected, 0);

    const Request* post = nullptr;
    for (const auto& r : http_.requests)
        if (r.method == "POST" && r.path == "/api/layouts") post = &r;
    ASSERT_NE(post, nullptr);
    EXPECT_EQ(post->authorization, QByteArrayLiteral("Bearer bld_pat_test"));
    const QJsonObject body = QJsonDocument::fromJson(post->body).object();
    EXPECT_EQ(body.value(QStringLiteral("title")).toString(), QStringLiteral("Show 2026 (offline copy)"));
    EXPECT_FALSE(body.value(QStringLiteral("bbm")).toString().isEmpty());
    EXPECT_FALSE(body.contains(QStringLiteral("orgSlug")));

    // The live layout keeps the server's: my move isn't there, theirs is.
    waitFor([] { return false; }, 150);
    EXPECT_EQ(brickArea(ws_.doc, 0), original_);
    EXPECT_EQ(brickArea(ws_.doc, 1), otherOriginal_.translated(0, 16));
    EXPECT_EQ(brickAt(*view_->currentMap(), 0).displayArea, original_);
    EXPECT_FALSE(reviewAction()->isEnabled());
}

TEST_F(MainWindowLive, AFailedSaveAsNewKeepsTheOfflineEdits) {
    http_.reply("/api/layouts", 500, QJsonObject{ { QStringLiteral("error"), QStringLiteral("disk full") } });
    editOffline();
    Answers answers{ { compare(QStringLiteral("Save Mine as a New Layout...")),
                       box(QStringLiteral("disk full"), QStringLiteral("OK")) } };
    answers.during([&] { ASSERT_TRUE(waitFor([&] { return answers.done(); })); });
    EXPECT_EQ(answers.unexpected, 0);
    EXPECT_TRUE(session().offlineEdits());
    EXPECT_TRUE(reviewAction()->isEnabled());
    EXPECT_EQ(brickArea(ws_.doc, 0), original_);
}
