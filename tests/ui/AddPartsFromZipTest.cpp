// Tools > Parts > Add Parts from a Zip, walked as a person would, with real
// Brick Track Designer exports (fixtures/btd-layout-parts): the design's
// folder lands in the imports folder, the parts are read again, and the set
// that places the whole design is shown in the Parts panel. An archive that
// would write outside the folder, or holds no parts, adds nothing.

#include "import/zip/ZipWriter.h"
#include "parts/PartsLibrary.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/PartsBrowser.h"
#include "ui/UpdateCheck.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoStack>

#include <memory>

using namespace bld;

namespace {

QString sample(const char* name) {
    return QStringLiteral(BLD_SOURCE_DIR "/fixtures/btd-layout-parts/") + QString::fromUtf8(name);
}

QString writeZip(const QString& path, const QList<std::pair<QString, QByteArray>>& files) {
    import::ZipWriter w;
    for (const auto& [name, data] : files) w.add(name, data);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return {};
    f.write(w.finish());
    return path;
}

class Win : public ui::MainWindow {
public:
    using ui::MainWindow::askPartsZip;
    using ui::MainWindow::MainWindow;
};

class AddPartsFromZip : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ui::UpdateCheck::setCheckAtStartupEnabled(false);
        // Imported parts go under the Module library folder: a fresh one.
        QSettings().setValue(QStringLiteral("modules/libraryPath"), home_.path());
        QSettings().remove(QStringLiteral("partsLibrary/userPaths"));
        window_ = std::make_unique<Win>(lib_);
        window_->ensureDocument();
        browser_ = window_->findChild<ui::PartsBrowser*>();
        ASSERT_TRUE(browser_);
    }
    void TearDown() override {
        if (auto* view = window_->findChild<ui::MapView*>()) view->undoStack()->setClean();
        window_.reset();
        QSettings().remove(QStringLiteral("modules/libraryPath"));
        QStandardPaths::setTestModeEnabled(false);
    }
    QString imports() const { return home_.path() + QStringLiteral("/imports"); }

    // Tools > Parts > Add Parts from a Zip, picking `zip`. Returns the text
    // of the message boxes that opened (each closed at once).
    QStringList addFromMenu(const QString& zip) {
        window_->askPartsZip = [zip] { return zip; };
        auto* act = window_->findChild<QAction*>(QStringLiteral("act.parts.addZip"));
        EXPECT_TRUE(act);
        if (!act) return {};
        QStringList boxes;
        QTimer closer;
        QObject::connect(&closer, &QTimer::timeout, [&] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                boxes << box->text();
                box->close();
            }
        });
        closer.start(20);
        act->trigger();
        return boxes;
    }
    // The keys the Parts panel shows.
    QStringList shown() const {
        QStringList keys;
        for (int i = 0; i < browser_->grid()->count(); ++i)
            if (!browser_->grid()->item(i)->isHidden())
                keys << browser_->grid()->item(i)->data(Qt::UserRole + 1).toString();
        return keys;
    }

    QTemporaryDir home_;
    parts::PartsLibrary lib_;
    std::unique_ptr<Win> window_;
    ui::PartsBrowser* browser_ = nullptr;
};

} // namespace

TEST_F(AddPartsFromZip, TheMenuItemSitsWithTheOtherPartsActions) {
    auto* act = window_->findChild<QAction*>(QStringLiteral("act.parts.addZip"));
    ASSERT_TRUE(act);
    EXPECT_EQ(act->text(), QStringLiteral("Add Parts from a &Zip..."));
    auto* menu = window_->findChild<QMenu*>(QStringLiteral("menu.tools.parts"));
    ASSERT_TRUE(menu);
    EXPECT_TRUE(menu->actions().contains(act));
}

TEST_F(AddPartsFromZip, ABrickTrackDesignerExportLandsInItsFolderAndItsSetIsShown) {
    const QStringList boxes = addFromMenu(sample("Yard.zip"));
    EXPECT_TRUE(boxes.isEmpty()) << boxes.join(QStringLiteral(" / ")).toStdString();
    // The design's folder, as the zip has it; its README stays out.
    EXPECT_TRUE(QFile::exists(imports() + QStringLiteral("/Yard/Yard.track-plate1.xml")));
    EXPECT_TRUE(QFile::exists(imports() + QStringLiteral("/Yard/Yard.track-plate1.gif")));
    EXPECT_TRUE(QFile::exists(imports() + QStringLiteral("/Yard/Yard.whole.set.xml")));
    EXPECT_TRUE(QFile::exists(imports() + QStringLiteral("/Yard/config/About.txt")));
    EXPECT_FALSE(QFile::exists(imports() + QStringLiteral("/README.txt")));
    // Read again: the 15 parts and the set of all of them.
    for (int i = 1; i <= 15; ++i)
        EXPECT_TRUE(lib_.metadata(QStringLiteral("Yard.track-plate%1").arg(i))) << i;
    const auto set = lib_.metadata(QStringLiteral("Yard.whole"));
    ASSERT_TRUE(set);
    EXPECT_EQ(set->kind, parts::PartKind::Group);
    EXPECT_EQ(set->subparts.size(), 15);
    // The Parts panel shows the set, the way to put the whole design down.
    EXPECT_TRUE(shown().contains(QStringLiteral("Yard.whole"), Qt::CaseInsensitive))
        << shown().join(QStringLiteral(", ")).toStdString();
    EXPECT_TRUE(window_->statusBar()->currentMessage().contains(QStringLiteral("“Yard”")))
        << window_->statusBar()->currentMessage().toStdString();
}

TEST_F(AddPartsFromZip, EverySampleAddsAndAnExportUnzipsOverTheOlderOne) {
    for (const char* zip : { "Left switch cut 150.zip", "Monorail loop.zip", "Yard.zip" })
        EXPECT_TRUE(addFromMenu(sample(zip)).isEmpty()) << zip;
    EXPECT_TRUE(lib_.metadata(QStringLiteral("Left_switch_cut_150.whole")));
    const auto monorail = lib_.metadata(QStringLiteral("Monorail_loop.monorail-M-1"));
    ASSERT_TRUE(monorail);
    ASSERT_FALSE(monorail->connections.isEmpty());
    for (const auto& c : monorail->connections) EXPECT_EQ(c.type, QStringLiteral("3"));
    // Exported again: the same folder, replaced, no copy beside it.
    EXPECT_TRUE(addFromMenu(sample("Yard.zip")).isEmpty());
    EXPECT_EQ(QDir(imports()).entryList(QDir::Dirs | QDir::NoDotAndDotDot),
              (QStringList{ QStringLiteral("Left switch cut 150"), QStringLiteral("Monorail loop"),
                            QStringLiteral("Yard") }));
}

TEST_F(AddPartsFromZip, AZipThatWouldWriteOutsideTheFolderAddsNothingAndSaysWhy) {
    QTemporaryDir dir;
    const QString zip = writeZip(dir.filePath(QStringLiteral("Bad.zip")),
                                 { { QStringLiteral("Good/GOOD.1.xml"), "<part/>" },
                                   { QStringLiteral("Good/../../evil.xml"), "<part/>" } });
    const QStringList boxes = addFromMenu(zip);
    ASSERT_EQ(boxes.size(), 1);
    EXPECT_TRUE(boxes[0].contains(QStringLiteral("outside the parts folder"))) << boxes[0].toStdString();
    EXPECT_FALSE(QFile::exists(imports() + QStringLiteral("/Good/GOOD.1.xml")));
    EXPECT_FALSE(QFile::exists(home_.path() + QStringLiteral("/evil.xml")));
}

TEST_F(AddPartsFromZip, AZipWithoutPartsAddsNothingAndSaysSo) {
    QTemporaryDir dir;
    const QString zip = writeZip(dir.filePath(QStringLiteral("Notes.zip")),
                                 { { QStringLiteral("Notes/readme.txt"), "hello" } });
    const QStringList boxes = addFromMenu(zip);
    ASSERT_EQ(boxes.size(), 1);
    EXPECT_TRUE(boxes[0].contains(QStringLiteral("no parts"))) << boxes[0].toStdString();
    EXPECT_FALSE(QDir(imports() + QStringLiteral("/Notes")).exists());
}

TEST_F(AddPartsFromZip, PartsZippedWithoutAFolderGetOneNamedAfterTheZip) {
    QTemporaryDir dir;
    const QString zip = writeZip(
        dir.filePath(QStringLiteral("My parts.zip")),
        { { QStringLiteral("LOOSE.1.xml"), "<part><Description><en>Loose</en></Description></part>" } });
    EXPECT_TRUE(addFromMenu(zip).isEmpty());
    EXPECT_TRUE(QFile::exists(imports() + QStringLiteral("/My parts/LOOSE.1.xml")));
}
