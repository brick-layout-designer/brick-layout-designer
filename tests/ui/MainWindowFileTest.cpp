// File › Save / Save As / Export BlueBrick Map with the native layout file.

#include "ui/LibraryPathsDialog.h"
#include "ui/MainWindow.h"
#include "ui/UpdateCheck.h"

#include "core/LayerBrick.h"
#include "core/Map.h"
#include "import/LayoutFile.h"
#include "parts/PartsLibrary.h"
#include "saveload/BbmReader.h"
#include "saveload/BbmWriter.h"
#include "saveload/SidecarIO.h"
#include "saveload/VenueIO.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QImage>
#include <QSettings>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>

using namespace bld;

namespace {

const QString kCorpus = QStringLiteral(BLD_BBM_CORPUS_DIR);

// Answers the modal dialogs that open while `run` runs, in order; a dialog
// beyond the answers is closed and counted as unexpected.
struct Answers {
    std::vector<std::function<void(QWidget*)>> steps;
    int unexpected = 0;
    void during(const std::function<void()>& run) {
        std::size_t next = 0;
        QTimer poll;
        poll.setInterval(20);
        QObject::connect(&poll, &QTimer::timeout, [&] {
            QWidget* modal = QApplication::activeModalWidget();
            if (!modal || !modal->isVisible()) return;
            if (next < steps.size()) steps[next++](modal);
            else {
                ++unexpected;
                modal->close();
            }
        });
        poll.start();
        run();
        poll.stop();
        EXPECT_EQ(next, steps.size()) << "fewer dialogs than expected";
    }
};

std::function<void(QWidget*)> click(const QString& text) {
    return [text](QWidget* w) {
        auto* box = qobject_cast<QMessageBox*>(w);
        ASSERT_NE(box, nullptr);
        for (QAbstractButton* b : box->buttons()) {
            if (QString(b->text()).remove(QLatin1Char('&')) == text) {
                b->click();
                return;
            }
        }
        ADD_FAILURE() << "no button " << text.toStdString();
        box->close();
    };
}

std::function<void(QWidget*)> chooseFile(const QString& path) {
    return [path](QWidget* w) {
        auto* dialog = qobject_cast<QFileDialog*>(w);
        ASSERT_NE(dialog, nullptr);
        // selectFile() can lose to the directory model still loading;
        // the name box is what accept() reads.
        dialog->setDirectory(QFileInfo(path).absolutePath());
        auto* name = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
        ASSERT_NE(name, nullptr);
        name->setText(QFileInfo(path).fileName());
        QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
    };
}

bool invoke(ui::MainWindow& w, const char* slot) {
    bool result = false;
    QMetaObject::invokeMethod(&w, slot, Qt::DirectConnection, qReturnArg(result));
    return result;
}

QAction* action(QWidget& w, const QString& text) {
    for (QAction* a : w.findChildren<QAction*>())
        if (a->text() == text) return a;
    return nullptr;
}

class MainWindowFile : public ::testing::Test {
protected:
    void SetUp() override {
        QStandardPaths::setTestModeEnabled(true);
        ui::UpdateCheck::setCheckAtStartupEnabled(false);
        bbm_ = dir_.filePath(QStringLiteral("corner.bbm"));
        ASSERT_TRUE(QFile::copy(kCorpus + QStringLiteral("/tight-corner.bbm"), bbm_));
        window_ = std::make_unique<ui::MainWindow>(parts_);
    }
    void TearDown() override {
        window_.reset();
        QStandardPaths::setTestModeEnabled(false);
    }

    QTemporaryDir dir_;
    QString bbm_;
    parts::PartsLibrary parts_;
    std::unique_ptr<ui::MainWindow> window_;
};

}  // namespace

TEST_F(MainWindowFile, SavingABbmOffersTheOneFileFormat) {
    ASSERT_TRUE(window_->openFile(bbm_));
    const QString native = dir_.filePath(QStringLiteral("corner.bld-layout"));
    Answers answers{ { click(QStringLiteral("Save as .bld-layout")) } };
    bool saved = false;
    answers.during([&] { saved = invoke(*window_, "onSave"); });
    EXPECT_TRUE(saved);
    EXPECT_EQ(answers.unexpected, 0);
    ASSERT_TRUE(QFile::exists(native));
    EXPECT_FALSE(QFile::exists(saveload::sidecarPathFor(native)));
    EXPECT_TRUE(import::readLayoutFile(native, dir_.path()).ok());
    EXPECT_TRUE(window_->windowTitle().startsWith(QStringLiteral("corner.bld-layout")))
        << window_->windowTitle().toStdString();

    // Now it is one: Save asks nothing.
    Answers none;
    none.during([&] { saved = invoke(*window_, "onSave"); });
    EXPECT_TRUE(saved);
    EXPECT_EQ(none.unexpected, 0);
}

TEST_F(MainWindowFile, KeepingTheBbmAsksOnlyOnce) {
    ASSERT_TRUE(window_->openFile(bbm_));
    QFile::remove(bbm_);
    Answers keep{ { click(QStringLiteral("Keep .bbm")) } };
    bool saved = false;
    keep.during([&] { saved = invoke(*window_, "onSave"); });
    EXPECT_TRUE(saved);
    EXPECT_TRUE(QFile::exists(bbm_));
    EXPECT_FALSE(QFile::exists(dir_.filePath(QStringLiteral("corner.bld-layout"))));

    Answers again;
    again.during([&] { saved = invoke(*window_, "onSave"); });
    EXPECT_TRUE(saved);
    EXPECT_EQ(again.unexpected, 0);
}

TEST_F(MainWindowFile, CancelSavesNothing) {
    ASSERT_TRUE(window_->openFile(bbm_));
    QFile::remove(bbm_);
    Answers cancel{ { click(QStringLiteral("Cancel")) } };
    bool saved = true;
    cancel.during([&] { saved = invoke(*window_, "onSave"); });
    EXPECT_FALSE(saved);
    EXPECT_FALSE(QFile::exists(bbm_));
    EXPECT_FALSE(QFile::exists(dir_.filePath(QStringLiteral("corner.bld-layout"))));
}

TEST_F(MainWindowFile, ExportingABbmLeavesOutWhatBlueBrickCannotHold) {
    // A layout with a venue in it.
    auto map = saveload::readBbm(bbm_).map;
    ASSERT_TRUE(map);
    map->sidecar.venue = saveload::readVenueFile(QStringLiteral(BLD_SOURCE_DIR "/fixtures/venues/grand-lobby.bld-venue"));
    ASSERT_TRUE(map->sidecar.venue);
    const QString native = dir_.filePath(QStringLiteral("lobby.bld-layout"));
    QString error;
    ASSERT_TRUE(import::writeLayoutFile(*map, native, &error)) << error.toStdString();
    ASSERT_TRUE(window_->openFile(native));

    const QString exported = dir_.filePath(QStringLiteral("for-bluebrick.bbm"));
    QAction* exportAct = action(*window_, QStringLiteral("Export as &BlueBrick Map (.bbm)..."));
    ASSERT_NE(exportAct, nullptr);
    Answers answers{ { chooseFile(exported), click(QStringLiteral("Yes")) } };
    answers.during([&] { exportAct->trigger(); });
    EXPECT_EQ(answers.unexpected, 0);
    ASSERT_TRUE(QFile::exists(exported));
    EXPECT_FALSE(QFile::exists(saveload::sidecarPathFor(exported)));
    auto back = saveload::readBbm(exported);
    ASSERT_TRUE(back.ok());
    EXPECT_FALSE(back.map->sidecar.venue.has_value());
    // The layout is still the .bld-layout, venue and all.
    EXPECT_TRUE(window_->windowTitle().startsWith(QStringLiteral("lobby.bld-layout")))
        << window_->windowTitle().toStdString();
}

namespace {

QByteArray partXml() {
    return QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<part>\n\t<Author>Me</Author>\n"
                             "\t<Description>\n\t\t<en>My part</en>\n\t</Description>\n</part>\n");
}

QByteArray partGif() {
    QImage image(16, 8, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QBuffer b;
    b.open(QIODevice::WriteOnly);
    image.save(&b, "PNG");
    return b.data();
}

// A layout of one MYPART.1 brick.
std::unique_ptr<core::Map> oneBrickOf(const QString& part) {
    auto map = std::make_unique<core::Map>();
    auto layer = std::make_unique<core::LayerBrick>();
    core::Brick b;
    b.partNumber = part;
    layer->bricks.push_back(b);
    map->layers().push_back(std::move(layer));
    return map;
}

QString layoutPartsDir() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/layout-parts");
}

}  // namespace

TEST_F(MainWindowFile, SavingALayoutCarriesTheUsersOwnParts) {
    // A part of the user's own, outside the bundled library.
    const QString mine = dir_.filePath(QStringLiteral("mine"));
    QDir().mkpath(mine);
    QFile xml(mine + QStringLiteral("/MYPART.1.xml")), png(mine + QStringLiteral("/MYPART.1.png"));
    ASSERT_TRUE(xml.open(QIODevice::WriteOnly) && png.open(QIODevice::WriteOnly));
    xml.write(partXml());
    png.write(partGif());
    xml.close();
    png.close();
    parts_.addSearchPath(mine);
    parts_.scan();
    ASSERT_TRUE(parts_.metadata(QStringLiteral("MYPART.1")));

    QString error;
    const QString source = dir_.filePath(QStringLiteral("source.bld-layout"));
    ASSERT_TRUE(import::writeLayoutFile(*oneBrickOf(QStringLiteral("MYPART.1")), source, &error));
    ASSERT_TRUE(window_->openFile(source));
    const QString saved = dir_.filePath(QStringLiteral("saved.bld-layout"));
    QAction* saveAs = action(*window_, QStringLiteral("Save &As..."));
    ASSERT_NE(saveAs, nullptr);
    Answers answers{ { chooseFile(saved) } };
    answers.during([&] { saveAs->trigger(); });
    const auto read = import::readLayoutFile(saved, dir_.path());
    ASSERT_TRUE(read.ok());
    EXPECT_EQ(read.partFiles.keys(), (QStringList{ QStringLiteral("MYPART.1.png"), QStringLiteral("MYPART.1.xml") }));
    EXPECT_EQ(read.partFiles.value(QStringLiteral("MYPART.1.xml")), partXml());
}

TEST_F(MainWindowFile, OpeningALayoutAddsThePartsItCarries) {
    QDir(layoutPartsDir()).removeRecursively();
    ASSERT_FALSE(parts_.metadata(QStringLiteral("THEIRPART.1")));
    QString error;
    const QString file = dir_.filePath(QStringLiteral("theirs.bld-layout"));
    ASSERT_TRUE(import::writeLayoutFile(*oneBrickOf(QStringLiteral("THEIRPART.1")), file, &error, nullptr,
                                        { { QStringLiteral("THEIRPART.1.xml"), partXml() },
                                          { QStringLiteral("THEIRPART.1.png"), partGif() } }));
    ASSERT_TRUE(window_->openFile(file));
    const auto meta = parts_.metadata(QStringLiteral("THEIRPART.1"));
    ASSERT_TRUE(meta);
    EXPECT_TRUE(meta->xmlFilePath.startsWith(layoutPartsDir())) << meta->xmlFilePath.toStdString();
    EXPECT_FALSE(meta->gifFilePath.isEmpty());
    // The folder stays in the library after a restart.
    QSettings s;
    s.beginGroup(ui::LibraryPathsDialog::kSettingsGroup);
    QStringList paths = s.value(ui::LibraryPathsDialog::kSettingsKey).toStringList();
    EXPECT_TRUE(paths.contains(layoutPartsDir()));
    paths.removeAll(layoutPartsDir());
    s.setValue(ui::LibraryPathsDialog::kSettingsKey, paths);
    s.endGroup();
    QDir(layoutPartsDir()).removeRecursively();
}
