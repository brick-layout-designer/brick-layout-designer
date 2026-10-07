// Import pipeline (model file -> PreparedPart -> library files) and the
// preview dialog's edits, against stub LDraw / parts libraries.

#include "ui/ImportPipeline.h"
#include "ui/ImportPreviewDialog.h"
#include "ui/BackgroundTask.h"
#include "ui/MainWindow.h"
#include "ui/NoticeArea.h"
#include "ui/ServerWindow.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QListWidget>
#include <QPushButton>
#include <QElapsedTimer>
#include <QGraphicsView>
#include <QLabel>
#include <QStandardPaths>
#include <QTest>
#include <QTemporaryDir>

#include <iostream>

using namespace bld;

namespace {

void writeFile(const QString& path, const QByteArray& body) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(body);
}

const ui::HeavyRunner kInline = [](const QString&, const std::function<void(ui::CancelToken&)>& work) {
    ui::CancelToken token;
    work(token);
    return true;
};

// Parts library with "TT.7": a 4 x 2 stud straight, connections at the ends.
struct TrackLibrary {
    QTemporaryDir dir;
    parts::PartsLibrary lib;
    TrackLibrary() {
        writeFile(dir.filePath(QStringLiteral("TT.7.xml")),
            "<part><ConnexionList>"
            "<connexion><type>1</type><position><x>-2</x><y>0</y></position><angle>180</angle></connexion>"
            "<connexion><type>1</type><position><x>2</x><y>0</y></position><angle>0</angle></connexion>"
            "</ConnexionList></part>");
        QImage sprite(32, 16, QImage::Format_ARGB32);
        sprite.fill(Qt::darkGray);
        sprite.save(dir.filePath(QStringLiteral("TT.7.png")));
        lib.addSearchPath(dir.path());
        lib.scan();
    }
};

QString twoStraights(QTemporaryDir& dir) {
    const QString path = dir.filePath(QStringLiteral("loop.ldr"));
    writeFile(path, "1 7 0 0 0 1 0 0 0 1 0 0 0 1 tt.dat\n"
                    "1 7 80 0 0 1 0 0 0 1 0 0 0 1 tt.dat\n");
    return path;
}

ui::PreparedPart samplePart() {
    ui::PreparedPart p;
    p.source = QStringLiteral("sample.ldr");
    p.sprite = QImage(4 * 8, 2 * 8, QImage::Format_ARGB32);
    p.sprite.fill(Qt::red);
    p.widthStuds = 4;
    p.heightStuds = 2;
    p.connections = { { QStringLiteral("1"), -2, 0, 180 }, { QStringLiteral("1"), 2, 0, 0 } };
    return p;
}

}  // namespace

TEST(ImportPipeline, RealGeometryWithSnapPoints) {
    TrackLibrary track;
    QTemporaryDir ldraw;
    writeFile(ldraw.filePath(QStringLiteral("LDConfig.ldr")), "0 stub\n");
    writeFile(ldraw.filePath(QStringLiteral("parts/tt.dat")), "4 16 -40 0 -20 40 0 -20 40 0 20 -40 0 20\n");
    QTemporaryDir models;
    ui::ImportSettings settings;
    settings.ldrawLibrary = ldraw.path();

    const auto part = ui::prepareImport(twoStraights(models), settings, track.lib, kInline);
    ASSERT_TRUE(part.ok()) << part.error.toStdString();
    EXPECT_EQ(part.widthStuds, 8);
    EXPECT_EQ(part.heightStuds, 2);
    EXPECT_EQ(part.sprite.size(), QSize(8 * 32, 2 * 32));
    EXPECT_EQ(part.stats.ldrawResolved, 2);
    ASSERT_EQ(part.connections.size(), 2);
    EXPECT_NEAR(part.connections[0].xStuds, -4.0, 1e-6);
    EXPECT_NEAR(part.connections[1].xStuds, 4.0, 1e-6);
}

TEST(ImportPipeline, ComposesFromPartsLibraryWithoutLDraw) {
    TrackLibrary track;
    QTemporaryDir models;
    const auto part = ui::prepareImport(twoStraights(models), {}, track.lib, kInline);
    ASSERT_TRUE(part.ok()) << part.error.toStdString();
    EXPECT_EQ(part.widthStuds, 8);
    EXPECT_EQ(part.heightStuds, 2);
    EXPECT_EQ(part.sprite.size(), QSize(64, 16));
    ASSERT_EQ(part.connections.size(), 2);
    EXPECT_NEAR(part.connections[0].xStuds, -4.0, 1e-6);
    EXPECT_NEAR(part.connections[1].xStuds, 4.0, 1e-6);
}

TEST(ImportPipeline, UnknownPartsReportAnError) {
    TrackLibrary track;
    QTemporaryDir models;
    const QString path = models.filePath(QStringLiteral("x.ldr"));
    writeFile(path, "1 7 0 0 0 1 0 0 0 1 0 0 0 1 nothing.dat\n");
    const auto part = ui::prepareImport(path, {}, track.lib, kInline);
    EXPECT_FALSE(part.ok());
    EXPECT_EQ(part.stats.unresolved, 1);
}

TEST(ImportPipeline, RotateTurnsSpriteFootprintAndConnections) {
    auto p = samplePart();
    ui::rotatePart(p, 1);
    EXPECT_EQ(p.widthStuds, 2);
    EXPECT_EQ(p.heightStuds, 4);
    EXPECT_EQ(p.sprite.size(), QSize(16, 32));
    // Clockwise in y-down screen space: +x end moves to +y.
    EXPECT_NEAR(p.connections[1].xStuds, 0.0, 1e-9);
    EXPECT_NEAR(p.connections[1].yStuds, 2.0, 1e-9);
    EXPECT_NEAR(p.connections[1].angleDeg, 90.0, 1e-9);
    ui::rotatePart(p, -1);
    EXPECT_NEAR(p.connections[1].xStuds, 2.0, 1e-9);
    EXPECT_NEAR(p.connections[1].angleDeg, 0.0, 1e-9);
}

TEST(ImportPipeline, WriteKeepsOrReplacesExistingPart) {
    QTemporaryDir out;
    QString err;
    const auto p = samplePart();
    EXPECT_EQ(ui::writeImportedPart(p, QStringLiteral("Loop v1.5"), out.path(), {}, false, &err),
              QStringLiteral("Loop_v1.5"));
    EXPECT_EQ(ui::writeImportedPart(p, QStringLiteral("Loop v1.5"), out.path(), {}, false, &err),
              QStringLiteral("Loop_v1.5-2"));
    EXPECT_EQ(ui::writeImportedPart(p, QStringLiteral("Loop v1.5"), out.path(), {}, true, &err),
              QStringLiteral("Loop_v1.5"));
    EXPECT_EQ(QDir(out.path()).entryList({ QStringLiteral("*.xml") }).size(), 2);

    // Replacing a hi-res part with an 8 px/stud one drops the stale .png.
    auto hires = p;
    hires.sprite = QImage(4 * 32, 2 * 32, QImage::Format_ARGB32);
    hires.sprite.fill(Qt::blue);
    ASSERT_FALSE(ui::writeImportedPart(hires, QStringLiteral("hr"), out.path(), {}, false, &err).isEmpty());
    ASSERT_TRUE(QFile::exists(out.filePath(QStringLiteral("hr.png"))));
    ASSERT_FALSE(ui::writeImportedPart(p, QStringLiteral("hr"), out.path(), {}, true, &err).isEmpty());
    EXPECT_FALSE(QFile::exists(out.filePath(QStringLiteral("hr.png"))));
}

TEST(ImportPreviewDialog, RotateAndDropConnection) {
    ui::ImportPreviewDialog dlg(samplePart(), { QStringLiteral("imports") }, QStringLiteral("Track"),
        [](const QString& name, const QString&) { return name == QStringLiteral("sample"); });
    auto* list = dlg.findChild<QListWidget*>();
    ASSERT_TRUE(list);
    list->item(0)->setCheckState(Qt::Unchecked);
    for (auto* b : dlg.findChildren<QPushButton*>()) {
        if (b->text() == ui::ImportPreviewDialog::tr("Rotate ⟳")) b->click();
    }
    const auto r = dlg.result();
    EXPECT_EQ(r.widthStuds, 2);
    EXPECT_EQ(r.heightStuds, 4);
    ASSERT_EQ(r.connections.size(), 1);
    EXPECT_NEAR(r.connections[0].yStuds, 2.0, 1e-9);
    EXPECT_EQ(dlg.category(), QStringLiteral("Track"));
    EXPECT_EQ(dlg.partName(), QStringLiteral("sample"));
    EXPECT_FALSE(dlg.replaceExisting());  // offered, but not ticked
    // Remembered for a re-import, in the final (rotated) frame.
    EXPECT_EQ(r.quarterTurns, 1);
    ASSERT_EQ(r.droppedConnections.size(), 1);
    EXPECT_NEAR(r.droppedConnections[0].x(), 0.0, 1e-9);
    EXPECT_NEAR(r.droppedConnections[0].y(), -2.0, 1e-9);
}

TEST(ImportPipeline, WriteRecordsTheSourceForReimport) {
    QTemporaryDir out;
    const QString source = out.filePath(QStringLiteral("model.ldr"));
    writeFile(source, "0 model\n");
    auto p = samplePart();
    p.source = source;
    ui::rotatePart(p, 3);
    p.droppedConnections = { QPointF(1.5, -0.25) };
    QString err;
    const QString key = ui::writeImportedPart(p, QStringLiteral("model"), out.path(), {}, false, &err);
    ASSERT_FALSE(key.isEmpty()) << err.toStdString();

    parts::PartsLibrary lib;
    lib.addSearchPath(out.path());
    lib.scan();
    const auto meta = lib.metadata(key);
    ASSERT_TRUE(meta && meta->importSource);
    EXPECT_EQ(meta->importSource->path, QFileInfo(source).absoluteFilePath());
    EXPECT_EQ(meta->importSource->modified.toSecsSinceEpoch(), QFileInfo(source).lastModified().toSecsSinceEpoch());
    EXPECT_EQ(meta->importSource->quarterTurns, 3);
    ASSERT_EQ(meta->importSource->droppedConnections.size(), 1);
    EXPECT_EQ(meta->importSource->droppedConnections[0], QPointF(1.5, -0.25));
    EXPECT_EQ(meta->connections.size(), 2) << "the part itself is unchanged";
}

TEST(ImportPipeline, ApplyImportEditsRepeatsRotationAndDrops) {
    auto p = samplePart();
    // Earlier import: turned once, the (-2, 0) end (now at (0, -2)) removed.
    ui::applyImportEdits(p, 1, { QPointF(0.1, -1.8) });
    EXPECT_EQ(p.quarterTurns, 1);
    EXPECT_EQ(p.widthStuds, 2);
    ASSERT_EQ(p.connections.size(), 1);
    EXPECT_NEAR(p.connections[0].yStuds, 2.0, 1e-9);
    ASSERT_EQ(p.droppedConnections.size(), 1);
    EXPECT_NEAR(p.droppedConnections[0].y(), -2.0, 1e-9);
    // A point with nothing within half a stud drops nothing.
    auto q = samplePart();
    ui::applyImportEdits(q, 0, { QPointF(5, 5) });
    EXPECT_EQ(q.connections.size(), 2);
}

TEST(ImportPipeline, StudioFileWithItsOwnCustomParts) {
    // fixtures/studio/zipcrypto-small.io (scripts/make-zipcrypto-io.py): encrypted like
    // Studio's, a submodel, and a custom part that only the file itself has.
    TrackLibrary track;
    QTemporaryDir ldraw;
    writeFile(ldraw.filePath(QStringLiteral("LDConfig.ldr")), "0 stub\n");
    writeFile(ldraw.filePath(QStringLiteral("parts/3001.dat")), "4 16 -40 0 -20 40 0 -20 40 0 20 -40 0 20\n");
    writeFile(ldraw.filePath(QStringLiteral("parts/3023.dat")), "4 16 -20 0 -10 20 0 -10 20 0 10 -20 0 10\n");
    ui::ImportSettings settings;
    settings.studioLibrary = ldraw.path();
    const auto part = ui::prepareImport(QStringLiteral(BLD_SOURCE_DIR "/fixtures/studio/zipcrypto-small.io"),
                                        settings, track.lib, kInline);
    ASSERT_TRUE(part.ok()) << part.error.toStdString();
    EXPECT_EQ(part.kindLabel, QStringLiteral("Studio import"));
    EXPECT_EQ(part.stats.ldrawResolved, 4) << part.warnings.join(QLatin1Char('\n')).toStdString();
    EXPECT_EQ(part.stats.unresolved, 0);
}

TEST(ImportPipeline, StudioErrorsReachTheUser) {
    TrackLibrary track;
    QTemporaryDir models;
    const QString path = models.filePath(QStringLiteral("x.io"));
    writeFile(path, "not a zip");
    const auto part = ui::prepareImport(path, {}, track.lib, kInline);
    EXPECT_FALSE(part.ok());
    EXPECT_TRUE(part.error.contains(QStringLiteral("isn't a Studio model"))) << part.error.toStdString();
}

// Opt-in, with a real Studio file and library (never committed):
//   BLD_STUDIO_IO=/path/to/set.io BLD_STUDIO_LDRAW=<Studio 2.0>/ldraw [BLD_LDRAW=<LDraw library>]
TEST(ImportPipeline, RealStudioFile) {
    const QString path = qEnvironmentVariable("BLD_STUDIO_IO");
    if (path.isEmpty()) GTEST_SKIP() << "set BLD_STUDIO_IO (and BLD_STUDIO_LDRAW) to import a real Studio file";
    TrackLibrary track;
    ui::ImportSettings settings;
    settings.studioLibrary = qEnvironmentVariable("BLD_STUDIO_LDRAW");
    settings.ldrawLibrary = qEnvironmentVariable("BLD_LDRAW");
    QElapsedTimer t;
    t.start();
    const auto part = ui::prepareImport(path, settings, track.lib, kInline);
    ASSERT_TRUE(part.ok()) << part.error.toStdString();
    std::cout << "[ import ] " << part.widthStuds << " x " << part.heightStuds << " studs, sprite "
              << part.sprite.width() << " x " << part.sprite.height() << ", " << part.stats.ldrawResolved
              << " parts drawn, " << part.stats.unresolved << " missing, " << part.warnings.size()
              << " warning(s), " << part.connections.size() << " connection(s) in " << t.elapsed() << " ms\n";
    for (int i = 0; i < std::min<qsizetype>(part.warnings.size(), 15); ++i)
        std::cout << "[ warn ] " << part.warnings[i].toStdString() << '\n';
}

namespace {

// How much of the view the whole sprite fills, in the tighter direction
// (1 = fitted edge to edge), and whether it is all in view.
struct Fit {
    double fill = 0;
    bool inside = false;
};
Fit fitOf(QGraphicsView* view) {
    const QRectF shown = view->mapFromScene(view->scene()->sceneRect()).boundingRect();
    const QRectF port = view->viewport()->rect();
    return { std::max(shown.width() / port.width(), shown.height() / port.height()),
             port.adjusted(-2, -2, 2, 2).contains(shown) };
}

}  // namespace

TEST(ImportPreviewDialog, ModelIsFittedAsSoonAsItShowsAndOnResize) {
    // Aaron: the model only fitted after a rotate. A wide model (40 x 4 studs) in the dialog.
    ui::PreparedPart wide = samplePart();
    wide.sprite = QImage(40 * 32, 4 * 32, QImage::Format_ARGB32);
    wide.sprite.fill(Qt::red);
    wide.widthStuds = 40;
    wide.heightStuds = 4;
    wide.connections.clear();
    ui::ImportPreviewDialog dlg(wide, { QStringLiteral("imports") }, QStringLiteral("imports"), {});
    auto* view = dlg.findChild<QGraphicsView*>();
    ASSERT_TRUE(view);
    dlg.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&dlg));
    QCoreApplication::processEvents();
    Fit fit = fitOf(view);
    EXPECT_TRUE(fit.inside);
    EXPECT_GT(fit.fill, 0.9) << "not fitted on first show";
    // Centred.
    const QPointF centre = view->mapFromScene(view->scene()->sceneRect().center());
    EXPECT_NEAR(centre.x(), view->viewport()->width() / 2.0, 3.0);
    EXPECT_NEAR(centre.y(), view->viewport()->height() / 2.0, 3.0);

    // A bigger window: still fitted.
    dlg.resize(dlg.width() + 400, dlg.height() + 200);
    QCoreApplication::processEvents();
    fit = fitOf(view);
    EXPECT_TRUE(fit.inside);
    EXPECT_GT(fit.fill, 0.9) << "not refitted on resize";

    // Zoomed in by hand: a resize leaves the zoom alone.
    for (auto* b : dlg.findChildren<QPushButton*>())
        if (b->text() == QStringLiteral("+")) b->click();
    const double zoomed = view->transform().m11();
    dlg.resize(dlg.width() - 200, dlg.height() - 100);
    QCoreApplication::processEvents();
    EXPECT_DOUBLE_EQ(view->transform().m11(), zoomed);
    // Fit brings it back.
    for (auto* b : dlg.findChildren<QPushButton*>())
        if (b->text() == ui::ImportPreviewDialog::tr("Fit")) b->click();
    fit = fitOf(view);
    EXPECT_TRUE(fit.inside);
    EXPECT_GT(fit.fill, 0.9);
}

TEST(ImportPipeline, AfterAnImportSendingToTheServerIsOneClick) {
    QStandardPaths::setTestModeEnabled(true);
    parts::PartsLibrary lib;
    ui::MainWindow window(lib);
    window.resize(1200, 800);
    window.show();
    window.offerToSendImportedPart(QStringLiteral("ninjago-city"));
    QFrame* card = window.notices()->card(QStringLiteral("imported-part"));
    ASSERT_NE(card, nullptr);
    EXPECT_TRUE(card->isVisible());
    QPushButton* send = nullptr;
    for (auto* b : card->findChildren<QPushButton*>())
        if (b->text() == QStringLiteral("Send to server…")) send = b;
    ASSERT_NE(send, nullptr);
    bool mentionsPart = false;
    for (auto* l : card->findChildren<QLabel*>()) mentionsPart |= l->text().contains(QStringLiteral("ninjago-city"));
    EXPECT_TRUE(mentionsPart);
    // Not signed in: the click opens the Server window, which shows how.
    send->click();
    for (int i = 0; i < 100 && !(window.serverWindow() && window.serverWindow()->isVisible()); ++i) QTest::qWait(10);
    ASSERT_NE(window.serverWindow(), nullptr);
    EXPECT_TRUE(window.serverWindow()->isVisible());
}

// Opt-in, with a real LDD model and LDD install (never committed):
//   BLD_LDD_IMPORT=/path/to/model.lxf[:more.lxfml...] BLD_LDD_ROOT=<LDD data folder with db.lif>
//   [BLD_LDRAW=<LDraw library>] [BLD_LDD_LDRAW_XML=<newer ldraw.xml>] [BLD_PARTS=<BlueBrickParts/parts>]
TEST(ImportPipeline, RealLddFiles) {
    const QStringList paths = qEnvironmentVariable("BLD_LDD_IMPORT").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (paths.isEmpty()) GTEST_SKIP() << "set BLD_LDD_IMPORT (and BLD_LDD_ROOT) to import real LDD models";
    TrackLibrary track;
    // The real parts library, for snap points on track: BLD_PARTS=<BlueBrickParts/parts>.
    if (const QString real = qEnvironmentVariable("BLD_PARTS"); !real.isEmpty()) {
        track.lib.addSearchPath(real);
        track.lib.scan();
    }
    ui::ImportSettings settings;
    settings.lddPath = qEnvironmentVariable("BLD_LDD_ROOT");
    settings.lddLdrawXml = qEnvironmentVariable("BLD_LDD_LDRAW_XML");
    settings.ldrawLibrary = qEnvironmentVariable("BLD_LDRAW");
    for (const QString& path : paths) {
        SCOPED_TRACE(path.toStdString());
        QElapsedTimer t;
        t.start();
        const auto part = ui::prepareImport(path, settings, track.lib, kInline);
        ASSERT_TRUE(part.ok()) << part.error.toStdString();
        std::cout << "[ import ] " << QFileInfo(path).fileName().toStdString() << ": " << part.widthStuds << " x "
                  << part.heightStuds << " studs, " << part.stats.lddRendered << " LDD parts drawn, "
                  << part.stats.unresolved << " missing, " << part.connections.size() << " connection(s) in "
                  << t.elapsed() << " ms\n";
        for (int i = 0; i < std::min<qsizetype>(part.warnings.size(), 5); ++i)
            std::cout << "[ warn ] " << part.warnings[i].toStdString() << '\n';
    }
}
