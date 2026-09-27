// Import pipeline (model file -> PreparedPart -> library files) and the
// preview dialog's edits, against stub LDraw / parts libraries.

#include "ui/ImportPipeline.h"
#include "ui/ImportPreviewDialog.h"
#include "ui/BackgroundTask.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>

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
