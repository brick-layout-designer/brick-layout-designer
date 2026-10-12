// Web/desktop contracts (fixtures/contracts/, the same files in both
// repos; the web checks them in apps/server/src/contracts.test.ts and
// friends):
//   - MANIFEST.sha256 lists every file with its hash, so a copy that drifts
//     from the other repo's fails here or there
//   - imported-part.xml is exactly what ImportToPart writes for the inputs
//     below (its date aside), and reads back as imported-part.json says
//   - the PickShape and the sprite's own mask agree on every hit point
//   - sidecar-hash.json: the sidecarItems blob hash of each vector
//   - part-naming.json: a server custom part cached by PartsSync is found
//     by its number and by the website's "custom:<id>" key
//
// BLD_WRITE_CONTRACTS=1 rewrites imported-part.xml and the rings in
// imported-part.json from the code instead of checking them; copy the
// folder to the web repo and update both manifests after.

#include "FakeHttp.h"
#include "PartsSync.h"
#include "SidecarItems.h"

#include "import/ImportToPart.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGraphicsPixmapItem>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimeZone>

#include <functional>

using namespace bld;

namespace {

QString contractsDir() { return QStringLiteral(BLD_SOURCE_DIR "/fixtures/contracts"); }

QByteArray readFile(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QJsonObject readJson(const QString& name) {
    return QJsonDocument::fromJson(readFile(contractsDir() + QLatin1Char('/') + name)).object();
}

bool writing() { return qEnvironmentVariableIntValue("BLD_WRITE_CONTRACTS") == 1; }

// The model the fixture part is made from: a 12 x 12 stud L (the top-right
// quarter cut away) with a 2 x 2 stud hole, at 16 px a stud.
constexpr int kStuds = 12;
constexpr int kPx = 16;

QImage fixtureSprite() {
    QImage img(kStuds * kPx, kStuds * kPx, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    for (int py = 0; py < img.height(); ++py) {
        for (int px = 0; px < img.width(); ++px) {
            const double x = (px + 0.5) / kPx - kStuds / 2.0;
            const double y = (py + 0.5) / kPx - kStuds / 2.0;
            const bool cut = x > 0 && y < 0;
            const bool hole = x > -4 && x < -2 && y > 2 && y < 4;
            if (!cut && !hole) img.setPixel(px, py, qRgba(200, 40, 40, 255));
        }
    }
    return img;
}

QString writeFixturePart(const QString& dir) {
    import::ImportSource src;
    src.path = QStringLiteral("/home/sam/models/Harbour yard.io");
    src.modified = QDateTime(QDate(2026, 10, 1), QTime(12, 0), QTimeZone::UTC);
    src.quarterTurns = 1;
    src.droppedConnections = { QPointF(0, 6) };
    src.displayName = QStringLiteral("Harbour yard");
    src.format = QStringLiteral("studio");
    src.align = QStringLiteral("bottom");
    src.nudgeStuds = QPointF(0.5, 0);
    src.designer = QStringLiteral("Sam Builder");
    src.designerUrl = QStringLiteral("https://example.com/sam");
    const QVector<import::ImportedConnection> conns = {
        { QStringLiteral("rail"), -6.0, 3.0, 180.0 },
        { QStringLiteral("rail"), 6.0, 3.0, 0.0 },
    };
    QString err;
    const QString key = import::writeImportedModelAsLibraryPart(
        src.path, fixtureSprite(), kStuds, kStuds, dir, QStringLiteral("Sam Builder"), conns, &err,
        /*replaceExisting=*/true, &src, QMargins(1, 0, 2, 1));
    EXPECT_FALSE(key.isEmpty()) << err.toStdString();
    return key;
}

/** The XML with its import date (the day the test runs) taken out. */
QByteArray withoutDate(QByteArray xml) {
    static const QRegularExpression date(QStringLiteral(R"(date="\d{4}-\d{2}-\d{2}")"));
    return QString::fromUtf8(xml).replace(date, QStringLiteral("date=\"(date)\"")).toUtf8();
}

bool waitFor(const std::function<bool()>& done, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

}  // namespace

TEST(Contracts, EveryFixtureMatchesTheManifest) {
    // The web repo holds the same files with the same MANIFEST.sha256.
    const QByteArray manifest = readFile(contractsDir() + QStringLiteral("/MANIFEST.sha256"));
    ASSERT_FALSE(manifest.isEmpty());
    QStringList listed;
    for (const QByteArray& line : manifest.split('\n')) {
        if (line.trimmed().isEmpty()) continue;
        const QList<QByteArray> parts = line.split(' ');
        ASSERT_GE(parts.size(), 3) << line.toStdString();
        const QString name = QString::fromUtf8(parts.last());
        listed << name;
        const QByteArray data = readFile(contractsDir() + QLatin1Char('/') + name);
        EXPECT_EQ(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex(), parts.first())
            << name.toStdString() << " differs from MANIFEST.sha256 (and so from the web repo's copy)";
    }
    QStringList onDisk = QDir(contractsDir()).entryList(QDir::Files);
    onDisk.removeAll(QStringLiteral("MANIFEST.sha256"));
    onDisk.removeAll(QStringLiteral("README.md"));
    listed.sort();
    onDisk.sort();
    EXPECT_EQ(listed, onDisk);
}

TEST(Contracts, TheImportedPartXmlIsWhatImportToPartWrites) {
    QTemporaryDir dir;
    const QString key = writeFixturePart(dir.path());
    ASSERT_FALSE(key.isEmpty());
    const QByteArray xml = withoutDate(readFile(dir.filePath(key + QStringLiteral(".xml"))));
    const QString fixture = contractsDir() + QStringLiteral("/imported-part.xml");
    if (writing()) {
        QFile f(fixture);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(xml);
        GTEST_SKIP() << "wrote " << fixture.toStdString();
    }
    EXPECT_EQ(xml, readFile(fixture)) << "ImportToPart's output changed: rerun with BLD_WRITE_CONTRACTS=1 and copy to the web repo";
}

TEST(Contracts, TheImportedPartReadsAsTheFixtureSays) {
    // The fixture XML, read by the parts library next to its sprite.
    QTemporaryDir dir;
    const QString key = writeFixturePart(dir.path());
    ASSERT_FALSE(key.isEmpty());
    QFile::remove(dir.filePath(key + QStringLiteral(".xml")));
    ASSERT_TRUE(QFile::copy(contractsDir() + QStringLiteral("/imported-part.xml"), dir.filePath(key + QStringLiteral(".xml"))));
    parts::PartsLibrary lib;
    lib.addSearchPath(dir.path());
    lib.scan();
    const auto meta = lib.metadata(key);
    ASSERT_TRUE(meta.has_value());

    QJsonObject want = readJson(QStringLiteral("imported-part.json"));
    EXPECT_EQ(meta->author, want.value(QStringLiteral("author")).toString());
    ASSERT_FALSE(meta->descriptions.isEmpty());
    EXPECT_EQ(meta->descriptions.first().text, want.value(QStringLiteral("description")).toString());
    EXPECT_EQ(meta->pxPerStud, want.value(QStringLiteral("pxPerStud")).toInt());

    const QJsonArray conns = want.value(QStringLiteral("connections")).toArray();
    ASSERT_EQ(meta->connections.size(), conns.size());
    for (int i = 0; i < conns.size(); ++i) {
        const QJsonObject c = conns[i].toObject();
        EXPECT_EQ(meta->connections[i].type, c.value(QStringLiteral("type")).toString());
        EXPECT_DOUBLE_EQ(meta->connections[i].position.x(), c.value(QStringLiteral("x")).toDouble());
        EXPECT_DOUBLE_EQ(meta->connections[i].position.y(), c.value(QStringLiteral("y")).toDouble());
        EXPECT_DOUBLE_EQ(meta->connections[i].angleDegrees, c.value(QStringLiteral("angle")).toDouble());
    }
    const QJsonObject m = want.value(QStringLiteral("snapMargin")).toObject();
    EXPECT_EQ(meta->snapMargin.left, m.value(QStringLiteral("left")).toDouble());
    EXPECT_EQ(meta->snapMargin.right, m.value(QStringLiteral("right")).toDouble());
    EXPECT_EQ(meta->snapMargin.top, m.value(QStringLiteral("top")).toDouble());
    EXPECT_EQ(meta->snapMargin.bottom, m.value(QStringLiteral("bottom")).toDouble());
    const QJsonObject d = want.value(QStringLiteral("designer")).toObject();
    EXPECT_EQ(meta->designer, d.value(QStringLiteral("name")).toString());
    EXPECT_EQ(meta->designerUrl, d.value(QStringLiteral("url")).toString());
    const QJsonObject from = want.value(QStringLiteral("importedFrom")).toObject();
    ASSERT_TRUE(meta->importedFrom.has_value());
    EXPECT_EQ(meta->importedFrom->file, from.value(QStringLiteral("file")).toString());
    EXPECT_EQ(meta->importedFrom->format, from.value(QStringLiteral("format")).toString());
    // The desktop keeps its own ImportSource (re-import); the server strips it.
    ASSERT_TRUE(meta->importSource.has_value());
    EXPECT_EQ(meta->importSource->path, QStringLiteral("/home/sam/models/Harbour yard.io"));

    QJsonArray rings;
    for (const QPolygonF& ring : meta->pickShape) {
        QJsonArray pts;
        for (const QPointF& p : ring) pts.append(QJsonArray{ p.x(), p.y() });
        rings.append(pts);
    }
    if (writing()) {
        want.insert(QStringLiteral("pickShape"), rings);
        QFile f(contractsDir() + QStringLiteral("/imported-part.json"));
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(want).toJson(QJsonDocument::Indented));
        GTEST_SKIP() << "wrote the rings";
    }
    EXPECT_EQ(rings, want.value(QStringLiteral("pickShape")).toArray());
}

TEST(Contracts, ThePickShapeAndTheSpriteMaskAgreeOnEveryHitPoint) {
    // The desktop picks an imported part by its sprite's mask (SceneBuilder:
    // MaskShape); the web by its <PickShape> rings, even-odd. Both must give
    // the fixture's answer at each point (studs from the sprite centre).
    const QJsonObject want = readJson(QStringLiteral("imported-part.json"));
    QPainterPath rings;
    rings.setFillRule(Qt::OddEvenFill);
    for (const QJsonValue& r : want.value(QStringLiteral("pickShape")).toArray()) {
        QPolygonF poly;
        for (const QJsonValue& p : r.toArray()) poly << QPointF(p.toArray()[0].toDouble(), p.toArray()[1].toDouble());
        rings.addPolygon(poly);
        rings.closeSubpath();
    }
    const QImage sprite = fixtureSprite();
    QGraphicsPixmapItem item(QPixmap::fromImage(sprite));
    item.setShapeMode(QGraphicsPixmapItem::MaskShape);
    item.setOffset(-sprite.width() / 2.0, -sprite.height() / 2.0);

    const QJsonArray hits = want.value(QStringLiteral("hits")).toArray();
    ASSERT_GE(hits.size(), 6);
    for (const QJsonValue& v : hits) {
        const QJsonObject h = v.toObject();
        const QPointF p(h.value(QStringLiteral("x")).toDouble(), h.value(QStringLiteral("y")).toDouble());
        const bool inside = h.value(QStringLiteral("inside")).toBool();
        EXPECT_EQ(rings.contains(p), inside) << "PickShape at " << p.x() << "," << p.y();
        EXPECT_EQ(item.contains(p * kPx), inside) << "sprite mask at " << p.x() << "," << p.y();
    }
}

TEST(Contracts, TheSidecarHashVectors) {
    const QJsonArray vectors = readJson(QStringLiteral("sidecar-hash.json")).value(QStringLiteral("vectors")).toArray();
    ASSERT_GE(vectors.size(), 5);
    for (const QJsonValue& v : vectors) {
        const QString json = v.toObject().value(QStringLiteral("json")).toString();
        const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        ASSERT_TRUE(doc.isObject()) << json.toStdString();
        EXPECT_EQ(sync::sidecarBlobHash(doc.object()), v.toObject().value(QStringLiteral("hash")).toString()) << json.toStdString();
    }
}

TEST(Contracts, AServerCustomPartIsFoundByNumberAndByItsWebsiteKey) {
    const QJsonObject want = readJson(QStringLiteral("part-naming.json"));
    const QString id = want.value(QStringLiteral("id")).toString();
    const QString number = want.value(QStringLiteral("partNumber")).toString();
    ASSERT_FALSE(id.isEmpty());
    QTemporaryDir dir;
    synctest::FakeHttp http;
    const QString xmlUrl = QStringLiteral("/api/custom-parts/%1/xml").arg(id);
    const QString spriteUrl = QStringLiteral("/api/custom-parts/%1/sprite").arg(id);
    http.reply("/api/parts/manifest", 200,
               { { QStringLiteral("libraries"), QJsonArray{} },
                 { QStringLiteral("customParts"),
                   QJsonArray{ QJsonObject{ { QStringLiteral("id"), id },
                                            { QStringLiteral("partNumber"), number },
                                            { QStringLiteral("hash"), QStringLiteral("h1") },
                                            { QStringLiteral("xmlUrl"), xmlUrl },
                                            { QStringLiteral("spriteUrl"), spriteUrl } } } } });
    http.replyRaw(xmlUrl.toUtf8(), 200, want.value(QStringLiteral("xml")).toString().toUtf8(), "text/plain");
    http.replyRaw(spriteUrl.toUtf8(), 200, QByteArray("\x89PNG-contract"), "image/png");
    sync::PartsSync s(http.base(), QStringLiteral("bld_pat_abc"), dir.path());
    bool done = false;
    QObject::connect(&s, &sync::PartsSync::finished, [&](const sync::PartsSyncResult& r) {
        EXPECT_TRUE(r.failed.isEmpty()) << r.failed.join(QStringLiteral("; ")).toStdString();
        done = true;
    });
    s.start();
    ASSERT_TRUE(waitFor([&] { return done; }));

    parts::PartsLibrary lib;
    lib.addSearchPath(dir.filePath(QStringLiteral("custom")));
    lib.scan();
    const auto byNumber = lib.metadata(number);
    const auto byKey = lib.metadata(QStringLiteral("custom:") + id);
    const auto byUpperKey = lib.metadata(QStringLiteral("CUSTOM:") + id.toUpper());
    ASSERT_TRUE(byNumber.has_value());
    ASSERT_TRUE(byKey.has_value()) << "a layout made on the website places it as custom:<id>";
    ASSERT_TRUE(byUpperKey.has_value());
    EXPECT_EQ(byKey->xmlFilePath, byNumber->xmlFilePath);
    EXPECT_EQ(byUpperKey->xmlFilePath, byNumber->xmlFilePath);
}
