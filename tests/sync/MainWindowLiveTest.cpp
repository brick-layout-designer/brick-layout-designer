// The main window with a live layout: offline edits, back in step, go
// through the compare window, which applies, discards, replaces the
// server's or saves mine as a new layout.

#include "FakeHttp.h"
#include "FakeSyncServer.h"

#include "ConnectDialog.h"
#include "ServerList.h"
#include "TokenStore.h"
#include "CompareDialog.h"
#include "edit/EditCommands.h"
#include "parts/PartsLibrary.h"
#include "ui/LiveLayout.h"
#include "ui/LoadingCard.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/NoticeArea.h"
#include "ui/UpdateCheck.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QLabel>

#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QUuid>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSettings>

#include <algorithm>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>

#include <functional>

using namespace bld;
using namespace bld::synctest;
using namespace std::chrono_literals;
using Status = sync::SyncClient::Status;

namespace {

// Answers the modal dialogs that open while `run` runs, in order; a dialog
// beyond the answers is closed and counted as unexpected.
// Each dialog is answered once. The poll sees a dialog again while its
// queued click hasn't landed yet, while it is busy (an upload in flight), or
// for a moment once a box it opened has closed (before the dialog itself
// finishes): none of that is a new dialog. A dialog answered before only
// takes the next answer when it comes back to the front after another one,
// as the compare window does when its confirmation is turned down.
struct Answers {
    std::vector<std::function<void(QWidget*)>> steps;
    int unexpected = 0;
    std::size_t given = 0;
    void during(const std::function<void()>& run) {
        QTimer poll;
        poll.setInterval(20);
        QList<QPointer<QWidget>> answered;
        QPointer<QWidget> last;
        QObject::connect(&poll, &QTimer::timeout, [&] {
            QWidget* modal = QApplication::activeModalWidget();
            if (!modal || !modal->isVisible() || modal == last) return;
            const bool again = answered.contains(modal);
            if (again && given == steps.size()) return;
            last = modal;
            if (!again) answered << modal;
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
    using ui::MainWindow::setTokenStore;
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
        // No servers from an earlier test.
        QSettings().remove(QStringLiteral("sync/serverAddress"));
        QSettings().setValue(QLatin1String(sync::ServerList::kKey), QByteArray("[]"));
        http_.upgrade = [this](QTcpSocket* s) { ws_.take(s); };
        beforeOpen();
        window_ = std::make_unique<TestWindow>(parts_);
        window_->setTokenStore(tokens_);
        view_ = window_->findChild<ui::MapView*>();
        live_ = window_->findChild<ui::LiveLayout*>();
        ASSERT_TRUE(view_ && live_);
        live_->session().client().setReconnectDelays(50ms, 200ms);
        window_->openLive({ http_.base(), QStringLiteral("bld_pat_test"), QStringLiteral("L1"),
                           QStringLiteral("Show 2026"), false, serverInfo() });
        ASSERT_TRUE(waitFor([&] { return session().status() == Status::Synced && view_->currentMap(); }));
        original_ = brickArea(ws_.doc, 0);
        otherOriginal_ = brickArea(ws_.doc, 1);
    }
    void TearDown() override {
        window_.reset();
        QDir(liveCache()).removeRecursively();
        QStandardPaths::setTestModeEnabled(false);
    }

    // Before the window opens the live layout.
    virtual void beforeOpen() {}
    // What the server said about itself (default: a server from before the
    // version checks, which asks nothing and is taken to have everything).
    virtual sync::ServerInfo serverInfo() const { return {}; }

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
    std::shared_ptr<sync::MemoryTokenStore> tokens_ = std::make_shared<sync::MemoryTokenStore>();
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

namespace {

// A part of my own that the server's catalog lacks.
class MainWindowLiveMyPart : public MainWindowLive {
protected:
    void beforeOpen() override {
        const QString dir =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/imports");
        QDir(dir).removeRecursively();
        ASSERT_TRUE(QDir().mkpath(dir));
        QFile xml(dir + QStringLiteral("/MYPART.1.xml"));
        ASSERT_TRUE(xml.open(QIODevice::WriteOnly));
        xml.write("<part><Description><en>My curve</en></Description></part>");
        xml.close();
        QFile gif(dir + QStringLiteral("/MYPART.1.gif"));
        ASSERT_TRUE(gif.open(QIODevice::WriteOnly));
        gif.write("GIF89a");
        gif.close();
        http_.reply("/api/parts/catalog", 200,
                    QJsonObject{ { QStringLiteral("parts"),
                                   QJsonArray{ QJsonObject{ { QStringLiteral("key"), QStringLiteral("3001") },
                                                            { QStringLiteral("partNumber"), QStringLiteral("3001") } } } } });
    }
    void TearDown() override {
        MainWindowLive::TearDown();
        QStandardPaths::setTestModeEnabled(true);
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/imports"))
            .removeRecursively();
        QStandardPaths::setTestModeEnabled(false);
    }

    // Once the catalog is in: without it nothing is offered.
    void waitForCatalog() {
        ASSERT_TRUE(waitFor([&] {
            for (const auto& r : http_.requests)
                if (r.path == "/api/parts/catalog") return true;
            return false;
        }));
        waitFor([] { return false; }, 150);
    }

    // Places a MYPART.1 brick in the view, as the parts browser would.
    void place(double x) {
        core::Map& m = *view_->currentMap();
        core::Brick b;
        b.guid = QStringLiteral("my-%1").arg(x);
        b.partNumber = QStringLiteral("MYPART.1");
        b.displayArea = QRectF(x, 0, 8, 8);
        view_->undoStack()->push(new edit::AddBrickCommand(m, brickRef(m, 0).layerIndex, b));
    }

    int customPartPosts() const {
        int n = 0;
        for (const auto& r : http_.requests)
            if (r.method == "POST" && r.path == "/api/custom-parts") ++n;
        return n;
    }
};

}  // namespace

TEST_F(MainWindowLiveMyPart, NotNowAsksOnlyOnce) {
    waitForCatalog();
    Answers answers{ { box(QStringLiteral("MYPART.1 isn't on the server yet"), QStringLiteral("Not Now")) } };
    answers.during([&] {
        place(400);
        ASSERT_TRUE(waitFor([&] { return answers.done(); }));
        // Another one of it: not asked again.
        place(420);
        waitFor([] { return false; }, 300);
    });
    EXPECT_EQ(answers.unexpected, 0);
    EXPECT_EQ(customPartPosts(), 0);
}

// The offer box deletes itself on close; the upload dialog, with its own
// event loop, must open after the box's finished signal has returned, not
// inside it, or the box is destroyed while it is still closing.
TEST_F(MainWindowLiveMyPart, TheUploadDialogOpensOnceTheOfferBoxHasClosed) {
    waitForCatalog();
    bool boxFinishedReturned = false;
    bool dialogSeen = false;
    const auto answerBox = box(QStringLiteral("MYPART.1 isn't on the server yet"), QStringLiteral("Upload..."));
    Answers answers{ { [&](QWidget* w) {
                          // Connected after the window's own handler, so this runs once that has returned.
                          QObject::connect(qobject_cast<QMessageBox*>(w), &QMessageBox::finished,
                                           [&] { boxFinishedReturned = true; });
                          answerBox(w);
                      },
                       [&](QWidget* w) {
                           dialogSeen = true;
                           EXPECT_TRUE(boxFinishedReturned)
                               << "the upload dialog opened inside the offer box's finished signal";
                           press(QStringLiteral("Cancel"))(w);
                       } } };
    answers.during([&] {
        place(400);
        ASSERT_TRUE(waitFor([&] { return answers.done(); }));
    });
    EXPECT_TRUE(dialogSeen);
    EXPECT_EQ(answers.unexpected, 0);
    EXPECT_EQ(customPartPosts(), 0);
}

TEST_F(MainWindowLiveMyPart, UploadSendsThePartToTheServer) {
    http_.reply("/api/custom-parts", 201, QJsonObject{ { QStringLiteral("id"), QStringLiteral("p1") } });
    waitForCatalog();
    Answers answers{ { box(QStringLiteral("MYPART.1 isn't on the server yet"), QStringLiteral("Upload...")),
                       press(QStringLiteral("Upload")) } };
    answers.during([&] {
        place(400);
        ASSERT_TRUE(waitFor([&] { return answers.done() && customPartPosts() == 1; }));
    });
    EXPECT_EQ(answers.unexpected, 0);
    const Request* post = nullptr;
    for (const auto& r : http_.requests)
        if (r.method == "POST" && r.path == "/api/custom-parts") post = &r;
    ASSERT_NE(post, nullptr);
    EXPECT_EQ(post->authorization, QByteArrayLiteral("Bearer bld_pat_test"));
    const QJsonObject body = QJsonDocument::fromJson(post->body).object();
    EXPECT_EQ(body.value(QStringLiteral("partNumber")).toString(), QStringLiteral("MYPART.1"));
    EXPECT_EQ(body.value(QStringLiteral("displayName")).toString(), QStringLiteral("My curve"));
}

namespace {

// The server's parts list fails once, then works.
class MainWindowLiveServerParts : public MainWindowLive {
protected:
    void beforeOpen() override {
        http_.reply("/api/parts/manifest", 503, QJsonObject{});
        http_.reply("/api/parts/manifest", 200,
                    QJsonObject{ { QStringLiteral("libraries"), QJsonArray{} },
                                 { QStringLiteral("customParts"), QJsonArray{} } });
    }
    QToolButton* failedButton() { return window_->findChild<QToolButton*>(QStringLiteral("partsSyncFailed")); }
    int manifestGets() const {
        int n = 0;
        for (const auto& r : http_.requests)
            if (r.path == "/api/parts/manifest") ++n;
        return n;
    }
};

}  // namespace

TEST_F(MainWindowLiveServerParts, AFailedDownloadStaysInSightAndTryAgainFetchesThem) {
    QToolButton* button = failedButton();
    ASSERT_NE(button, nullptr);
    ASSERT_TRUE(waitFor([&] { return button->isVisibleTo(window_.get()); }));
    EXPECT_TRUE(button->text().contains(QStringLiteral("Try again"))) << button->text().toStdString();
    EXPECT_TRUE(button->toolTip().contains(QStringLiteral("503"))) << button->toolTip().toStdString();
    // It doesn't time out like a status message.
    waitFor([] { return false; }, 300);
    EXPECT_TRUE(button->isVisibleTo(window_.get()));

    button->click();
    ASSERT_TRUE(waitFor([&] { return manifestGets() == 2 && !button->isVisibleTo(window_.get()); }));
}

namespace {

// The server has 20 part files this computer doesn't.
class MainWindowLiveDownloadCard : public MainWindowLive {
protected:
    static constexpr int kFiles = 20;
    static QString serverParts() {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/server-parts");
    }
    void beforeOpen() override {
        // Files left by an earlier run would be skipped as up to date.
        QDir(serverParts()).removeRecursively();
        QJsonArray files;
        for (int i = 0; i < kFiles; ++i) {
            const QByteArray data = "<part>" + QByteArray::number(i) + "</part>";
            const QString path = QStringLiteral("dl/p%1.xml").arg(i);
            files.append(QJsonObject{
                { QStringLiteral("path"), path },
                { QStringLiteral("sha256"),
                  QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()) },
                { QStringLiteral("size"), data.size() } });
            http_.replyRaw("/parts/libraries/club/" + path.toUtf8(), 200, data, "text/xml");
        }
        // A new hash every run, so nothing is skipped as already fetched.
        const QString hash = QUuid::createUuid().toString(QUuid::WithoutBraces);
        http_.reply("/api/parts/manifest", 200,
                    { { QStringLiteral("libraries"),
                        QJsonArray{ QJsonObject{ { QStringLiteral("slug"), QStringLiteral("club") },
                                                 { QStringLiteral("name"), QStringLiteral("Club parts") },
                                                 { QStringLiteral("urlPrefix"), QStringLiteral("/parts/libraries/club/") },
                                                 { QStringLiteral("hash"), hash } } } },
                      { QStringLiteral("customParts"), QJsonArray{} } });
        http_.reply("/api/parts/manifest/libraries/club", 200,
                    { { QStringLiteral("slug"), QStringLiteral("club") },
                      { QStringLiteral("urlPrefix"), QStringLiteral("/parts/libraries/club/") },
                      { QStringLiteral("files"), files } });
        // Write down what the download card says, every turn of the event loop.
        sampler_.setInterval(0);
        QObject::connect(&sampler_, &QTimer::timeout, [this] {
            for (QWidget* w : QApplication::allWidgets()) {
                if (w->objectName() != QStringLiteral("partsDownloadCard")) continue;
                auto* card = static_cast<ui::LoadingCard*>(w);
                const QString now = card->isVisibleTo(card->window())
                                        ? card->title() + QLatin1Char('|') + card->count()
                                        : QStringLiteral("hidden");
                if (seen_.isEmpty() || seen_.last() != now) seen_ << now;
            }
        });
        sampler_.start();
    }
    void TearDown() override {
        sampler_.stop();
        MainWindowLive::TearDown();
        QStandardPaths::setTestModeEnabled(true);
        QDir(serverParts()).removeRecursively();
        QStandardPaths::setTestModeEnabled(false);
    }
    int fileGets() const {
        int n = 0;
        for (const auto& r : http_.requests)
            if (r.path.startsWith("/parts/libraries/club/dl/")) ++n;
        return n;
    }
    QTimer sampler_;
    QStringList seen_;
};

}  // namespace

TEST_F(MainWindowLiveDownloadCard, DownloadingServerPartsShowsACountThenGoesAway) {
    auto* card = window_->findChild<ui::LoadingCard*>(QStringLiteral("partsDownloadCard"));
    ASSERT_NE(card, nullptr);
    ASSERT_TRUE(waitFor([&] { return fileGets() == kFiles && !card->isVisibleTo(window_.get()); }));
    waitFor([] { return false; }, 50);
    const QString all = seen_.join(QLatin1Char('\n'));
    EXPECT_TRUE(seen_.contains(QStringLiteral("Checking the server's parts…|"))) << all.toStdString();
    bool midway = false;
    for (const QString& s : seen_) {
        if (!s.startsWith(QStringLiteral("Downloading server parts…|"))) continue;
        const QString count = s.section(QLatin1Char('|'), 1);
        EXPECT_TRUE(count.endsWith(QStringLiteral(" of %1").arg(kFiles))) << all.toStdString();
        midway |= count != QStringLiteral("%1 of %1").arg(kFiles);
    }
    EXPECT_TRUE(midway) << all.toStdString();
    EXPECT_EQ(seen_.last(), QStringLiteral("hidden")) << all.toStdString();
}

// A server that suggests a newer app and lacks some features.
class MainWindowLiveOlderServer : public MainWindowLive {
protected:
    void beforeOpen() override {
        before_ = QCoreApplication::applicationVersion();
        QCoreApplication::setApplicationVersion(QStringLiteral("1.3.0"));
    }
    void TearDown() override {
        MainWindowLive::TearDown();
        QCoreApplication::setApplicationVersion(before_);
    }
    QString before_;
    sync::ServerInfo serverInfo() const override {
        sync::ServerInfo info;
        info.version = QStringLiteral("nightly-old");
        info.schemaVersion = 1;
        info.protocols = { QStringLiteral("y-websocket/1") };
        info.desktopMinimum = QStringLiteral("0.1.0");
        info.desktopRecommended = QStringLiteral("99.0.0");
        info.downloadUrl = QStringLiteral("https://example.org/get");
        info.features = QStringList{ QStringLiteral("liveSync"), QStringLiteral("signIn"), QStringLiteral("publish"),
                                     QStringLiteral("clubs"), QStringLiteral("venues"), QStringLiteral("uploadParts"),
                                     QStringLiteral("ownerTags"), QStringLiteral("partFilesWithToken") };
        return info;
    }
};

TEST_F(MainWindowLiveOlderServer, SaysToUpdateNamesWhatIsMissingAndDoesntAskForIt) {
    auto* notices = window_->notices();
    ASSERT_TRUE(notices->isShown(QStringLiteral("server-update")));
    QWidget* update = notices->card(QStringLiteral("server-update"));
    EXPECT_EQ(update->findChild<QLabel*>(QStringLiteral("NoticeTitle"))->text(),
              QStringLiteral("Please update Brick Layout Designer"));
    EXPECT_TRUE(update->findChild<QLabel*>(QStringLiteral("NoticeText"))->text().contains(QStringLiteral("99.0.0")));

    ASSERT_TRUE(notices->isShown(QStringLiteral("server-features")));
    const QString text =
        notices->card(QStringLiteral("server-features"))->findChild<QLabel*>(QStringLiteral("NoticeText"))->text();
    EXPECT_TRUE(text.contains(QStringLiteral("• Getting the server's parts"))) << text.toStdString();
    EXPECT_TRUE(text.contains(QStringLiteral("• Your settings on every computer"))) << text.toStdString();
    EXPECT_FALSE(text.contains(QStringLiteral("venue library"))) << text.toStdString();
    // The live layout itself still works.
    EXPECT_EQ(session().status(), Status::Synced);
    // What the server lacks isn't asked for.
    waitFor([] { return false; }, 200);
    for (const auto& r : http_.requests) {
        EXPECT_FALSE(r.path.startsWith("/api/parts/manifest")) << r.path.toStdString();
        EXPECT_NE(r.path, QByteArray("/api/me/preferences"));
    }
}

TEST_F(MainWindowLive, AServerFromBeforeTheChecksAsksNothing) {
    // No "please update"; only a quiet note that it may not do everything.
    EXPECT_FALSE(window_->notices()->isShown(QStringLiteral("server-update")));
    ASSERT_TRUE(window_->notices()->isShown(QStringLiteral("server-features")));
    EXPECT_EQ(window_->notices()->card(QStringLiteral("server-features"))->findChild<QLabel*>(QStringLiteral("NoticeTitle"))->text(),
              QStringLiteral("This server may not do everything yet"));
}

namespace {

// Two servers: the club's is Main; the layout is live on the other one.
class MainWindowLiveElsewhere : public MainWindowLive {
protected:
    void beforeOpen() override {
        sync::ServerList list;
        list.add(main_.base(), QStringLiteral("Train club"));
        list.add(http_.base(), QStringLiteral("Show hall"));
        list.save();
        tokens_->save(main_.base(), QStringLiteral("bld_pat_main"));
        tokens_->save(http_.base(), QStringLiteral("bld_pat_test"));
        main_.reply("/api/me/preferences", 200,
                    { { QStringLiteral("prefs"), QJsonObject{} }, { QStringLiteral("updatedAt"), QJsonValue() } });
    }
    FakeHttp main_;
};

}  // namespace

TEST_F(MainWindowLiveElsewhere, SettingsFollowTheMainServerWithItsOwnToken) {
    ASSERT_TRUE(waitFor([&] {
        return std::any_of(main_.requests.cbegin(), main_.requests.cend(),
                           [](const auto& r) { return r.path == "/api/me/preferences"; });
    }));
    for (const auto& r : main_.requests) EXPECT_EQ(r.authorization, QByteArray("Bearer bld_pat_main")) << r.path.toStdString();
    // The live server is never asked for settings, nor sent Main's token.
    waitFor([] { return false; }, 200);
    for (const auto& r : http_.requests) {
        EXPECT_NE(r.path, QByteArray("/api/me/preferences"));
        EXPECT_TRUE(r.authorization.isEmpty() || r.authorization == "Bearer bld_pat_test") << r.path.toStdString();
    }
    for (const QString& h : ws_.authHeaders) EXPECT_EQ(h, QStringLiteral("Bearer bld_pat_test"));
}

TEST_F(MainWindowLiveElsewhere, TheWindowSaysWhichServerItIsLiveOnAndRemembersTheLayout) {
    EXPECT_TRUE(window_->windowTitle().contains(QStringLiteral("Live on Show hall"))) << window_->windowTitle().toStdString();
    bool status = false;
    for (QLabel* l : window_->findChildren<QLabel*>()) status |= l->text().startsWith(QStringLiteral("Live on Show hall: "));
    EXPECT_TRUE(status);
    const sync::ServerList list = sync::ServerList::load();
    const sync::ServerEntry* live = list.find(http_.base());
    ASSERT_NE(live, nullptr);
    ASSERT_EQ(live->recent.size(), 1);
    EXPECT_EQ(live->recent.first().id, QStringLiteral("L1"));
    EXPECT_EQ(live->recent.first().title, QStringLiteral("Show 2026"));
    EXPECT_TRUE(list.find(main_.base())->recent.isEmpty());
    EXPECT_EQ(list.lastUsed()->url, http_.base());
    EXPECT_EQ(list.settingsAccount(), main_.base());
}

TEST_F(MainWindowLive, LiveOnTheOnlyServerItBecomesMainAndKeepsItsSettings) {
    const sync::ServerList list = sync::ServerList::load();
    ASSERT_EQ(list.servers().size(), 1);
    EXPECT_EQ(list.settingsAccount(), http_.base());
    ASSERT_TRUE(waitFor([&] {
        return std::any_of(http_.requests.cbegin(), http_.requests.cend(),
                           [](const auto& r) { return r.path == "/api/me/preferences"; });
    }));
}
