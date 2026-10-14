// Updating an imported part, walked as a person would: Replace with
// Another File (from the Parts panel and from the part on the map), a
// name that's already taken on import (Replace / Keep both), and an
// original file that has moved (Choose the file…), from Re-import and from
// Re-import Changed Parts.

#include "ui/ImportPreviewDialog.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/MapViewInternal.h"
#include "ui/PartsBrowser.h"
#include "ui/UpdateCheck.h"
#include "parts/PartsLibrary.h"
#include "rendering/SceneBuilder.h"
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QImage>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUndoStack>

#include <memory>

using namespace bld;

namespace {

void writeFile(const QString& path, const QByteArray& body) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(body);
}

// Two models made of a 4 x 2 stud straight (TT.7, which the parts library
// has): one straight, and two in a row (8 x 2 studs).
QString oneStraight(const QString& dir) {
    const QString path = QDir(dir).filePath(QStringLiteral("one.ldr"));
    writeFile(path, "1 7 0 0 0 1 0 0 0 1 0 0 0 1 tt.dat\n");
    return path;
}
QString twoStraights(const QString& dir) {
    const QString path = QDir(dir).filePath(QStringLiteral("loop.ldr"));
    writeFile(path, "1 7 0 0 0 1 0 0 0 1 0 0 0 1 tt.dat\n"
                    "1 7 80 0 0 1 0 0 0 1 0 0 0 1 tt.dat\n");
    return path;
}

bool waitFor(const std::function<bool()>& done, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) QTest::qWait(10);
    return done();
}

// Answers whatever the app asks while it lives, as a person would: the
// import preview (with the name typed, if any) is saved, a question is
// answered with the next button named in `buttons`, and the moved-files
// list gets "Choose the file…" for `choose`. With `sameForAll`, a question's
// "do the same for the others" is ticked first; with `okTheRest`, a notice
// left once the buttons are used up is closed with OK. Batch Import's
// options are taken as they are.
struct Person {
    QString typeName;
    QStringList buttons;
    QString choose;
    bool sameForAll = false;
    bool okTheRest = false;
    int previews = 0;
    QStringList asked;  // the questions' texts
    QStringList told;   // the notices closed with OK
    QTimer timer;
    bool chosen = false;

    Person() {
        QObject::connect(&timer, &QTimer::timeout, [this] {
            QWidget* w = QApplication::activeModalWidget();
            if (auto* preview = qobject_cast<ui::ImportPreviewDialog*>(w)) {
                if (!typeName.isEmpty()) preview->findChild<QLineEdit*>(QStringLiteral("partName"))->setText(typeName);
                ++previews;
                preview->accept();
            } else if (auto* box = qobject_cast<QMessageBox*>(w)) {
                if (buttons.isEmpty()) {
                    if (okTheRest && box->button(QMessageBox::Ok)) {
                        told << box->text();
                        box->button(QMessageBox::Ok)->click();
                    }
                    return;
                }
                asked << box->text();
                if (sameForAll && box->checkBox()) {
                    asked.back() += QLatin1Char('\n') + box->checkBox()->text();
                    box->checkBox()->setChecked(true);
                }
                auto* b = box->findChild<QAbstractButton*>(buttons.takeFirst());
                if (b) b->click();
                else box->reject();
            } else if (auto* options = qobject_cast<QDialog*>(w);
                       options && options->objectName() == QStringLiteral("batchImportOptions")) {
                options->accept();
            } else if (w && w->objectName() == QStringLiteral("movedOriginals") && !chosen) {
                // Clicked from the event loop: the preview it opens is answered here too.
                // (Part numbers are the same in any case.)
                for (auto* b : w->findChildren<QPushButton*>())
                    if (b->objectName().compare(QStringLiteral("choose:") + choose, Qt::CaseInsensitive) == 0) {
                        chosen = true;
                        QMetaObject::invokeMethod(b, &QPushButton::click, Qt::QueuedConnection);
                    }
            }
        });
        timer.start(20);
    }
};

// The import entry points, as the menus call them.
class Win : public ui::MainWindow {
public:
    using ui::MainWindow::MainWindow;
    using ui::MainWindow::askModelFile;
    using ui::MainWindow::askModelFiles;
    using ui::MainWindow::importModelFile;
    using ui::MainWindow::onReimportChangedParts;
};

class ImportReplace : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ui::UpdateCheck::setCheckAtStartupEnabled(false);
        // Imported parts go under the Module library folder: a fresh one.
        QSettings().setValue(QStringLiteral("modules/libraryPath"), home_.path());
        QSettings().remove(QStringLiteral("import/ldrawLibraryPath"));
        writeFile(trackDir_.filePath(QStringLiteral("TT.7.xml")),
                  "<part><ConnexionList>"
                  "<connexion><type>1</type><position><x>-2</x><y>0</y></position><angle>180</angle></connexion>"
                  "<connexion><type>1</type><position><x>2</x><y>0</y></position><angle>0</angle></connexion>"
                  "</ConnexionList></part>");
        QImage sprite(32, 16, QImage::Format_ARGB32);
        sprite.fill(Qt::darkGray);
        sprite.save(trackDir_.filePath(QStringLiteral("TT.7.png")));
        lib_.addSearchPath(trackDir_.path());
        lib_.scan();
        window_ = std::make_unique<Win>(lib_);
        window_->resize(1200, 800);
        window_->ensureDocument();
        window_->show();
        view_ = window_->findChild<ui::MapView*>();
        browser_ = window_->findChild<ui::PartsBrowser*>();
        ASSERT_TRUE(view_ && browser_);
    }
    void TearDown() override {
        view_->undoStack()->setClean();
        window_.reset();
        QSettings().remove(QStringLiteral("modules/libraryPath"));
        QStandardPaths::setTestModeEnabled(false);
    }

    // Tools › Import with the model, saved from the preview as `name`.
    void importAs(const QString& model, const QString& name, const QStringList& answers = {}) {
        Person me;
        me.typeName = name;
        me.buttons = answers;
        window_->importModelFile(model);
        EXPECT_EQ(me.previews, 1);
        asked_ = me.asked;
    }
    // The part's width in studs, from the picture the parts library draws.
    int widthStuds(const QString& key) {
        const auto meta = lib_.metadata(key);
        if (!meta) return 0;
        return lib_.pixmap(key).width() / meta->pxPerStud;
    }
    QList<QGraphicsItem*> placed(const QString& key) {
        QList<QGraphicsItem*> out;
        for (QGraphicsItem* it : view_->scene()->items())
            if (ui::detail::isBrickItem(it)
                && it->data(rendering::SceneBuilder::kBrickPartRole).toString().compare(key, Qt::CaseInsensitive) == 0)
                out << it;
        return out;
    }
    QString originalFile(const QString& key) {
        const auto meta = lib_.metadata(key);
        return meta && meta->importSource ? QFileInfo(meta->importSource->path).absoluteFilePath() : QString();
    }
    // Right-click the part in the Parts panel and pick `action`.
    void partsPanelMenu(const QString& key, const QString& action) {
        browser_->showPart(key);
        QListWidgetItem* item = nullptr;
        for (int i = 0; i < browser_->grid()->count(); ++i)
            if (browser_->grid()->item(i)->data(Qt::UserRole + 1).toString().compare(key, Qt::CaseInsensitive) == 0)
                item = browser_->grid()->item(i);
        ASSERT_NE(item, nullptr) << key.toStdString();
        pickFromMenu(action, [&] {
            emit browser_->grid()->customContextMenuRequested(browser_->grid()->visualItemRect(item).center());
        });
    }
    // Right-click the part on the map and pick `action`.
    void mapMenu(const QString& key, const QString& action) {
        const auto items = placed(key);
        ASSERT_FALSE(items.isEmpty());
        view_->centerOn(items.front());
        QApplication::processEvents();
        const QPoint at = view_->mapFromScene(items.front()->sceneBoundingRect().center());
        ASSERT_EQ(view_->itemAt(at), items.front());
        pickFromMenu(action, [&] {
            QContextMenuEvent ce(QContextMenuEvent::Mouse, at, view_->viewport()->mapToGlobal(at));
            QApplication::sendEvent(view_->viewport(), &ce);
        });
    }
    void pickFromMenu(const QString& action, const std::function<void()>& open) {
        bool found = false;
        QTimer menuTimer;
        QObject::connect(&menuTimer, &QTimer::timeout, [&] {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!menu) return;
            menuTimer.stop();
            for (QAction* a : menu->actions())
                if (a->objectName() == action) {
                    found = true;
                    menu->close();
                    a->trigger();
                }
            menu->close();
        });
        menuTimer.start(20);
        open();
        EXPECT_TRUE(found) << action.toStdString();
    }

    QTemporaryDir home_, trackDir_, models_;
    parts::PartsLibrary lib_;
    std::unique_ptr<Win> window_;
    ui::MapView* view_ = nullptr;
    ui::PartsBrowser* browser_ = nullptr;
    QStringList asked_;
};

}  // namespace

TEST_F(ImportReplace, ReplaceWithAnotherFileKeepsThePartNumberAndRedrawsTheMap) {
    const QString loop = twoStraights(models_.path());
    const QString one = oneStraight(models_.path());
    importAs(loop, QStringLiteral("Station"));
    ASSERT_TRUE(lib_.metadata(QStringLiteral("Station")));
    EXPECT_EQ(widthStuds(QStringLiteral("Station")), 8);
    ASSERT_EQ(placed(QStringLiteral("Station")).size(), 1);
    const double wide = placed(QStringLiteral("Station")).front()->sceneBoundingRect().width();

    // Parts panel › right-click › Replace with Another File..., pick one.ldr.
    QString pickerStartedIn;
    window_->askModelFile = [&](const QString&, const QString& dir) {
        pickerStartedIn = dir;
        return one;
    };
    {
        Person me;
        partsPanelMenu(QStringLiteral("Station"), QStringLiteral("act.part.replace"));
        EXPECT_EQ(me.previews, 1);
    }
    // The picker opened where the model was.
    EXPECT_EQ(QDir(pickerStartedIn).absolutePath(), QDir(models_.path()).absolutePath());
    // Same part number, no copy; the new picture; the new original file.
    EXPECT_TRUE(lib_.metadata(QStringLiteral("Station")));
    EXPECT_FALSE(lib_.metadata(QStringLiteral("Station-2")));
    EXPECT_EQ(widthStuds(QStringLiteral("Station")), 4);
    EXPECT_EQ(originalFile(QStringLiteral("Station")), QFileInfo(one).absoluteFilePath());
    // The part already on the map redrew at once, as the new model.
    ASSERT_EQ(placed(QStringLiteral("Station")).size(), 1);
    EXPECT_NEAR(placed(QStringLiteral("Station")).front()->sceneBoundingRect().width(), wide / 2, wide * 0.05);

    // On the map: right-click the placed part › Replace with Another File..., back to the loop.
    window_->askModelFile = [&](const QString&, const QString&) { return loop; };
    {
        Person me;
        mapMenu(QStringLiteral("Station"), QStringLiteral("ctxReplaceImported"));
        EXPECT_EQ(me.previews, 1);
    }
    EXPECT_EQ(widthStuds(QStringLiteral("Station")), 8);
    EXPECT_EQ(originalFile(QStringLiteral("Station")), QFileInfo(loop).absoluteFilePath());
    ASSERT_EQ(placed(QStringLiteral("Station")).size(), 1);
    EXPECT_NEAR(placed(QStringLiteral("Station")).front()->sceneBoundingRect().width(), wide, wide * 0.05);

    // Re-import from Original File now reads the file picked last.
    writeFile(loop, "1 7 0 0 0 1 0 0 0 1 0 0 0 1 tt.dat\n");
    {
        Person me;
        partsPanelMenu(QStringLiteral("Station"), QStringLiteral("act.part.reimport"));
        EXPECT_EQ(me.previews, 1);
    }
    EXPECT_EQ(widthStuds(QStringLiteral("Station")), 4);
}

TEST_F(ImportReplace, ANameAlreadyTakenAsksReplaceOrKeepBoth) {
    const QString loop = twoStraights(models_.path());
    const QString one = oneStraight(models_.path());
    importAs(loop, QStringLiteral("Station"));
    ASSERT_EQ(widthStuds(QStringLiteral("Station")), 8);
    EXPECT_TRUE(asked_.isEmpty());

    // The same name again: asked, and Replace updates the part in place.
    importAs(one, QStringLiteral("Station"), { QStringLiteral("clashReplace") });
    ASSERT_EQ(asked_.size(), 1);
    EXPECT_TRUE(asked_.front().contains(QStringLiteral("Station"))) << asked_.front().toStdString();
    EXPECT_FALSE(lib_.metadata(QStringLiteral("Station-2")));
    EXPECT_EQ(widthStuds(QStringLiteral("Station")), 4);
    EXPECT_EQ(originalFile(QStringLiteral("Station")), QFileInfo(one).absoluteFilePath());
    // Already on the map: it changed there, and no second copy was placed.
    EXPECT_EQ(placed(QStringLiteral("Station")).size(), 1);

    // Once more: Keep both adds a -2 part, and the first stays as it is.
    importAs(loop, QStringLiteral("Station"), { QStringLiteral("clashKeepBoth") });
    EXPECT_EQ(asked_.size(), 1);
    ASSERT_TRUE(lib_.metadata(QStringLiteral("Station-2")));
    EXPECT_EQ(widthStuds(QStringLiteral("Station-2")), 8);
    EXPECT_EQ(widthStuds(QStringLiteral("Station")), 4);
    EXPECT_EQ(placed(QStringLiteral("Station-2")).size(), 1);

    // Cancel imports nothing.
    importAs(loop, QStringLiteral("station"), { QStringLiteral("cancel") });
    EXPECT_FALSE(lib_.metadata(QStringLiteral("Station-3")));
}

TEST_F(ImportReplace, AMovedOriginalFileCanBeFoundAgain) {
    const QString loop = twoStraights(models_.path());
    importAs(loop, QStringLiteral("Station"));
    ASSERT_EQ(widthStuds(QStringLiteral("Station")), 8);

    // The model moves to another folder (and gets shorter on the way).
    QDir(models_.path()).mkpath(QStringLiteral("moved"));
    const QString moved = models_.filePath(QStringLiteral("moved/loop.ldr"));
    ASSERT_TRUE(QFile::rename(loop, moved));
    writeFile(moved, "1 7 0 0 0 1 0 0 0 1 0 0 0 1 tt.dat\n");
    window_->askModelFile = [&](const QString&, const QString&) { return moved; };

    // Re-import from Original File: not a dead end, it offers to find it.
    {
        Person me;
        me.buttons = { QStringLiteral("chooseFile") };
        partsPanelMenu(QStringLiteral("Station"), QStringLiteral("act.part.reimport"));
        ASSERT_EQ(me.asked.size(), 1);
        EXPECT_TRUE(me.asked.front().contains(QStringLiteral("moved"))) << me.asked.front().toStdString();
        EXPECT_EQ(me.previews, 1);
    }
    EXPECT_EQ(originalFile(QStringLiteral("Station")), QFileInfo(moved).absoluteFilePath());
    EXPECT_EQ(widthStuds(QStringLiteral("Station")), 4);

    // Moved again: Re-import Changed Parts lists it to choose (or skip).
    const QString back = models_.filePath(QStringLiteral("loop.ldr"));
    ASSERT_TRUE(QFile::rename(moved, back));
    writeFile(back, "1 7 0 0 0 1 0 0 0 1 0 0 0 1 tt.dat\n"
                    "1 7 80 0 0 1 0 0 0 1 0 0 0 1 tt.dat\n");
    window_->askModelFile = [&](const QString&, const QString&) { return back; };
    {
        Person me;
        me.choose = QStringLiteral("Station");
        window_->onReimportChangedParts();
        EXPECT_TRUE(me.chosen);
        EXPECT_EQ(me.previews, 1);
    }
    EXPECT_EQ(originalFile(QStringLiteral("Station")), QFileInfo(back).absoluteFilePath());
    EXPECT_EQ(widthStuds(QStringLiteral("Station")), 8);
    EXPECT_TRUE(waitFor([&] { return placed(QStringLiteral("Station")).size() == 1; }));
}

TEST_F(ImportReplace, BatchImportAsksAboutTakenNamesOnceWhenToldToDoTheSame) {
    // Station and Depot are already parts (8 studs long); Yard is new.
    const QString first = models_.filePath(QStringLiteral("first"));
    const QString station = QDir(first).filePath(QStringLiteral("Station.ldr"));
    const QString depot = QDir(first).filePath(QStringLiteral("Depot.ldr"));
    writeFile(station, "1 7 0 0 0 1 0 0 0 1 0 0 0 1 tt.dat\n1 7 80 0 0 1 0 0 0 1 0 0 0 1 tt.dat\n");
    writeFile(depot, "1 7 0 0 0 1 0 0 0 1 0 0 0 1 tt.dat\n1 7 80 0 0 1 0 0 0 1 0 0 0 1 tt.dat\n");
    importAs(station, QStringLiteral("Station"));
    importAs(depot, QStringLiteral("Depot"));
    ASSERT_EQ(widthStuds(QStringLiteral("Station")), 8);
    ASSERT_EQ(widthStuds(QStringLiteral("Depot")), 8);
    const auto importedParts = [&] {
        QStringList out;
        for (const QString& k : lib_.keys())
            if (const auto meta = lib_.metadata(k); meta && meta->importSource) out << k;
        return out;
    };
    const QStringList before = importedParts();
    ASSERT_EQ(before.size(), 2);

    // The batch: shorter Station and Depot (4 studs), and Yard.
    const QString batch = models_.filePath(QStringLiteral("batch"));
    const QStringList files = { QDir(batch).filePath(QStringLiteral("Station.ldr")),
                                QDir(batch).filePath(QStringLiteral("Depot.ldr")),
                                QDir(batch).filePath(QStringLiteral("Yard.ldr")) };
    for (const QString& f : files) writeFile(f, "1 7 0 0 0 1 0 0 0 1 0 0 0 1 tt.dat\n");
    int pickerOpened = 0;
    window_->askModelFiles = [&](const QString&) {
        ++pickerOpened;
        return files;
    };

    // Tools › Import › Batch Import: the first taken name asks; "do the same" ticked, Replace.
    QAction* batchAct = nullptr;
    for (QAction* a : window_->findChildren<QAction*>())
        if (a->text() == QStringLiteral("&Batch Import...")) batchAct = a;
    ASSERT_NE(batchAct, nullptr);
    Person me;
    me.buttons = { QStringLiteral("clashReplace") };
    me.sameForAll = true;
    me.okTheRest = true;
    batchAct->trigger();
    EXPECT_EQ(pickerOpened, 1);
    ASSERT_EQ(me.asked.size(), 1);
    EXPECT_TRUE(me.asked.front().contains(QStringLiteral("Station"))) << me.asked.front().toStdString();
    EXPECT_TRUE(me.asked.front().contains(QStringLiteral("the other 1 part")))
        << me.asked.front().toStdString();
    ASSERT_EQ(me.told.size(), 1);
    EXPECT_TRUE(me.told.front().contains(QStringLiteral("3 of 3"))) << me.told.front().toStdString();

    // Both updated in place: same part numbers, the new models, no copies.
    EXPECT_EQ(widthStuds(QStringLiteral("Station")), 4);
    EXPECT_EQ(widthStuds(QStringLiteral("Depot")), 4);
    EXPECT_EQ(originalFile(QStringLiteral("Station")), QFileInfo(files[0]).absoluteFilePath());
    EXPECT_EQ(originalFile(QStringLiteral("Depot")), QFileInfo(files[1]).absoluteFilePath());
    EXPECT_FALSE(lib_.metadata(QStringLiteral("Station-2")));
    EXPECT_FALSE(lib_.metadata(QStringLiteral("Depot-2")));
    // The third is a new part.
    QStringList added = importedParts();
    for (const QString& k : before) added.removeAll(k);
    ASSERT_EQ(added.size(), 1) << added.join(QLatin1Char(' ')).toStdString();
    EXPECT_EQ(originalFile(added.front()), QFileInfo(files[2]).absoluteFilePath());
    EXPECT_EQ(widthStuds(added.front()), 4);
}
