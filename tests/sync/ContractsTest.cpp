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
//   - update-guard.json: the update guard refuses what the web's does, with
//     the same limits; its gridSize vectors (grid sizes the server refuses)
//     all read here
//
// BLD_WRITE_CONTRACTS=1 rewrites imported-part.xml and the rings in
// imported-part.json from the code instead of checking them; copy the
// folder to the web repo and update both manifests after.

#include "FakeHttp.h"
#include "PartsSync.h"
#include "SidecarItems.h"
#include "SyncDoc.h"
#include "UpdateGuard.h"
#include "WebModel.h"

#include "core/DrawnLimits.h"
#include "core/LayerGrid.h"
#include "core/Map.h"

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

#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>

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

/** The XML with its import date (the day the test runs) taken out, and
 *  Unix line ends (ImportToPart writes text mode: CRLF on Windows). */
QByteArray withoutDate(QByteArray xml) {
    static const QRegularExpression date(QStringLiteral(R"(date="\d{4}-\d{2}-\d{2}")"));
    xml.replace("\r\n", "\n");
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
    QStringList onDisk;
    for (const QString& f : QDir(contractsDir()).entryList(QDir::Files))
        if (!f.startsWith(QLatin1Char('.')) && f != QStringLiteral("MANIFEST.sha256") && f != QStringLiteral("README.md")) onDisk << f;
    listed.sort();
    onDisk.sort();
    EXPECT_EQ(listed.join(QLatin1Char(' ')).toStdString(), onDisk.join(QLatin1Char(' ')).toStdString());
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
    EXPECT_EQ(QString::fromUtf8(xml), QString::fromUtf8(readFile(fixture))) << "ImportToPart's output changed: rerun with BLD_WRITE_CONTRACTS=1 and copy to the web repo";
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

namespace {
QByteArray fromHex(const QJsonValue& v) { return QByteArray::fromHex(v.toString().toLatin1()); }
}  // namespace

TEST(Contracts, TheUpdateGuardRefusesWhatTheWebsDoes) {
    const QJsonObject c = readJson(QStringLiteral("update-guard.json"));
    EXPECT_EQ(c.value(QStringLiteral("maxUpdateBytes")).toInteger(), sync::guard::kMaxUpdateBytes);
    EXPECT_EQ(c.value(QStringLiteral("maxNesting")).toInt(), sync::guard::kMaxNesting);
    const QJsonArray vectors = c.value(QStringLiteral("vectors")).toArray();
    ASSERT_GE(vectors.size(), 10);
    for (const QJsonValue& v : vectors) {
        const QJsonObject o = v.toObject();
        const QByteArray bytes = fromHex(o.value(QStringLiteral("hex")));
        const QString why = o.value(QStringLiteral("kind")).toString() == QLatin1String("stateVector")
                                ? sync::guard::checkStateVector(bytes)
                                : sync::guard::checkUpdate(bytes);
        EXPECT_EQ(!why.isEmpty(), o.value(QStringLiteral("refused")).toBool())
            << o.value(QStringLiteral("what")).toString().toStdString() << ": " << why.toStdString();
    }
}

// Grid sizes the server refuses (gridSize): every vector still reads here
// (the refusal is the server's; MapViewPaint draws a size below 1 as one
// stud), and the ones it takes leave a size of 1 or more.
TEST(Contracts, TheGridSizeVectorsReadAndDraw) {
    const QJsonObject g = readJson(QStringLiteral("update-guard.json")).value(QStringLiteral("gridSize")).toObject();
    const QByteArray base = fromHex(g.value(QStringLiteral("base")));
    const QJsonArray vectors = g.value(QStringLiteral("vectors")).toArray();
    ASSERT_GE(vectors.size(), 15);
    for (const QJsonValue& v : vectors) {
        const QJsonObject o = v.toObject();
        const std::string what = o.value(QStringLiteral("what")).toString().toStdString();
        const QByteArray update = fromHex(o.value(QStringLiteral("hex")));
        EXPECT_TRUE(sync::guard::checkUpdate(update).isEmpty()) << what;
        sync::SyncDoc doc;
        if (o.value(QStringLiteral("kind")).toString() == QLatin1String("update")) {
            ASSERT_TRUE(doc.applyUpdate(base)) << what;
        }
        ASSERT_TRUE(doc.applyUpdate(update)) << what;
        QString error;
        const auto map = sync::mapFromDocJson(doc.toJson(), &error);
        ASSERT_TRUE(map) << what << ": " << error.toStdString();
        for (const auto& layer : map->layers()) {
            if (layer->kind() != core::LayerKind::Grid) continue;
            const int size = static_cast<const core::LayerGrid&>(*layer).gridSizeInStud;
            if (!o.value(QStringLiteral("refused")).toBool()) {
                EXPECT_GE(size, 1) << what;
            }
        }
    }
}

// Drawing settings (drawingSettings): what the desktop brings a setting
// into (core/DrawnLimits.h) is always something the server takes, so a
// layout read here and then put on a server, or a live layout whose
// settings the next edit writes back, is never refused.
namespace {

struct Limit {
    bool whole = false;
    double min = 0, max = 0;
};

// "number 0 to 100", "whole number 1 to 100", "number 1 to 2147483647 (…)".
std::optional<Limit> parseLimit(const QString& text) {
    static const QRegularExpression re(QStringLiteral(R"(^(whole )?number (-?[0-9.]+) to (-?[0-9.]+))"));
    const auto m = re.match(text);
    if (!m.hasMatch()) return std::nullopt;
    return Limit{ !m.captured(1).isEmpty(), m.captured(2).toDouble(), m.captured(3).toDouble() };
}

QJsonObject drawingLimits() {
    return readJson(QStringLiteral("update-guard.json"))
        .value(QStringLiteral("drawingSettings"))
        .toObject()
        .value(QStringLiteral("limits"))
        .toObject();
}

template <typename T>
void expectInside(const core::limits::Range<T>& desktop, const QString& key, const QJsonObject& limits) {
    const auto server = parseLimit(limits.value(key).toString());
    ASSERT_TRUE(server) << key.toStdString() << ": " << limits.value(key).toString().toStdString();
    EXPECT_GE(static_cast<double>(desktop.min), server->min) << key.toStdString();
    EXPECT_LE(static_cast<double>(desktop.max), server->max) << key.toStdString();
    EXPECT_GE(static_cast<double>(desktop.fallback), server->min) << key.toStdString();
    EXPECT_LE(static_cast<double>(desktop.fallback), server->max) << key.toStdString();
    if (server->whole) {
        EXPECT_TRUE(std::is_integral_v<T>) << key.toStdString() << " is a whole number there";
    }
}

// The server's check of one drawn number (finite, whole if so, in range).
bool takes(const QJsonValue& v, const Limit& l) {
    if (!v.isDouble()) return false;
    const double d = v.toDouble();
    return std::isfinite(d) && (!l.whole || d == std::floor(d)) && d >= l.min && d <= l.max;
}

// Every drawn setting of `after` that differs from `before` is one the
// server takes (it doesn't refuse a value that was already there).
void expectServerTakes(const QJsonObject& before, const QJsonObject& after, const std::string& what) {
    const QJsonObject limits = drawingLimits();
    const auto limit = [&](const char* key) { return *parseLimit(limits.value(QLatin1String(key)).toString()); };
    const auto at = [](QJsonValue v, const QStringList& path) {
        for (const QString& k : path) v = v.isArray() ? v.toArray().at(k.toInt()) : v.toObject().value(k);
        return v;
    };
    const auto check = [&](const QStringList& path, const char* key) {
        const QJsonValue now = at(after, path), was = at(before, path);
        if (now.isUndefined() || now == was) return;
        EXPECT_TRUE(takes(now, limit(key))) << what << ": " << path.join(QLatin1Char('.')).toStdString() << " = "
                                            << QJsonDocument(QJsonArray{ now }).toJson(QJsonDocument::Compact).toStdString();
    };
    const QJsonObject data = after.value(QStringLiteral("layerData")).toObject();
    for (auto it = data.begin(); it != data.end(); ++it) {
        const QStringList sheet{ QStringLiteral("layerData"), it.key() };
        const QJsonObject o = it.value().toObject();
        const QString type = o.value(QStringLiteral("type")).toString();
        check(sheet + QStringList{ QStringLiteral("transparency") }, "sheet.transparency");
        check(sheet + QStringList{ QStringLiteral("hullProperties"), QStringLiteral("hullThickness") },
              "sheet.hullProperties.hullThickness");
        if (type == QLatin1String("grid")) {
            check(sheet + QStringList{ QStringLiteral("gridSizeInStud") }, "grid.gridSizeInStud");
            check(sheet + QStringList{ QStringLiteral("gridThickness") }, "grid.gridThickness");
            check(sheet + QStringList{ QStringLiteral("subGridThickness") }, "grid.subGridThickness");
            check(sheet + QStringList{ QStringLiteral("subDivisionNumber") }, "grid.subDivisionNumber");
            check(sheet + QStringList{ QStringLiteral("cellIndexFont"), QStringLiteral("size") }, "grid.cellIndexFont.size");
            check(sheet + QStringList{ QStringLiteral("cellIndexCorner"), QStringLiteral("x") }, "grid.cellIndexCorner.x/y");
            check(sheet + QStringList{ QStringLiteral("cellIndexCorner"), QStringLiteral("y") }, "grid.cellIndexCorner.x/y");
        } else if (type == QLatin1String("area")) {
            check(sheet + QStringList{ QStringLiteral("areaCellSize") }, "area.areaCellSize");
        } else if (type == QLatin1String("ruler")) {
            const QJsonArray rulers = o.value(QStringLiteral("rulerItems")).toArray();
            for (int i = 0; i < rulers.size(); ++i) {
                const QStringList r = sheet + QStringList{ QStringLiteral("rulerItems"), QString::number(i) };
                for (const char* k : { "lineThickness", "guidelineThickness", "unit", "radius", "offsetDistance" })
                    check(r + QStringList{ QLatin1String(k) }, k == std::string("lineThickness") ? "ruler.lineThickness"
                                                              : k == std::string("guidelineThickness") ? "ruler.guidelineThickness"
                                                              : k == std::string("unit") ? "ruler.unit"
                                                              : k == std::string("radius") ? "ruler.radius"
                                                                                           : "ruler.offsetDistance");
                check(r + QStringList{ QStringLiteral("measureFont"), QStringLiteral("size") }, "ruler.measureFont.size");
                for (const char* p : { "point1", "point2", "center" })
                    for (const char* c : { "x", "y" })
                        check(r + QStringList{ QLatin1String(p), QLatin1String(c) }, "ruler.point1/point2/center.x/y");
                const QJsonValue dash = at(after, r + QStringList{ QStringLiteral("guidelineDashPattern") });
                if (dash.isArray() && dash != at(before, r + QStringList{ QStringLiteral("guidelineDashPattern") })) {
                    const QJsonArray d = dash.toArray();
                    double total = 0;
                    bool each = d.size() <= 64;
                    for (const QJsonValue& v : d) {
                        each = each && takes(v, Limit{ false, 0, 1000 });
                        total += v.toDouble();
                    }
                    EXPECT_TRUE(each && (d.isEmpty() || total >= 0.01)) << what << ": a dash pattern the server refuses";
                }
            }
        }
    }
}

}  // namespace

TEST(Contracts, TheDesktopsDrawingRangesAreInsideTheServers) {
    namespace L = core::limits;
    const QJsonObject limits = drawingLimits();
    ASSERT_FALSE(limits.isEmpty());
    // Every server limit is one the desktop keeps (a new one fails here until it does).
    const QStringList known{ QStringLiteral("sheet.transparency"), QStringLiteral("sheet.hullProperties.hullThickness"),
                             QStringLiteral("grid.gridSizeInStud"), QStringLiteral("grid.gridThickness"),
                             QStringLiteral("grid.subGridThickness"), QStringLiteral("grid.subDivisionNumber"),
                             QStringLiteral("grid.cellIndexFont.size"), QStringLiteral("grid.cellIndexCorner.x/y"),
                             QStringLiteral("area.areaCellSize"), QStringLiteral("ruler.lineThickness"),
                             QStringLiteral("ruler.guidelineThickness"), QStringLiteral("ruler.guidelineDashPattern"),
                             QStringLiteral("ruler.unit"), QStringLiteral("ruler.measureFont.size"),
                             QStringLiteral("ruler.point1/point2/center.x/y"), QStringLiteral("ruler.radius"),
                             QStringLiteral("ruler.offsetDistance") };
    for (const QString& k : limits.keys()) EXPECT_TRUE(known.contains(k)) << "the desktop doesn't keep " << k.toStdString();
    expectInside(L::kTransparency, QStringLiteral("sheet.transparency"), limits);
    expectInside(L::kHullThickness, QStringLiteral("sheet.hullProperties.hullThickness"), limits);
    expectInside(L::kGridSizeInStud, QStringLiteral("grid.gridSizeInStud"), limits);
    expectInside(L::kGridThickness, QStringLiteral("grid.gridThickness"), limits);
    expectInside(L::kSubGridThickness, QStringLiteral("grid.subGridThickness"), limits);
    expectInside(L::kSubDivisionNumber, QStringLiteral("grid.subDivisionNumber"), limits);
    expectInside(L::kFontSize, QStringLiteral("grid.cellIndexFont.size"), limits);
    expectInside(L::kCellIndexCorner, QStringLiteral("grid.cellIndexCorner.x/y"), limits);
    expectInside(L::kAreaCellSizeInStud, QStringLiteral("area.areaCellSize"), limits);
    expectInside(L::kLineThickness, QStringLiteral("ruler.lineThickness"), limits);
    expectInside(L::kGuidelineThickness, QStringLiteral("ruler.guidelineThickness"), limits);
    expectInside(L::kRulerUnit, QStringLiteral("ruler.unit"), limits);
    expectInside(L::kFontSize, QStringLiteral("ruler.measureFont.size"), limits);
    expectInside(L::kRulerCoordinate, QStringLiteral("ruler.point1/point2/center.x/y"), limits);
    expectInside(L::kRulerCoordinate, QStringLiteral("ruler.offsetDistance"), limits);
    expectInside(L::kRulerRadius, QStringLiteral("ruler.radius"), limits);
    // "at most 64 numbers 0 to 1000, adding up to 0.01 or more (or none)"
    static const QRegularExpression dash(
        QStringLiteral(R"(^at most ([0-9]+) numbers (-?[0-9.]+) to (-?[0-9.]+), adding up to ([0-9.]+) or more)"));
    const auto m = dash.match(limits.value(QStringLiteral("ruler.guidelineDashPattern")).toString());
    ASSERT_TRUE(m.hasMatch());
    EXPECT_LE(L::kMaxDashEntries, m.captured(1).toInt());
    EXPECT_GE(0.0, m.captured(2).toDouble());
    EXPECT_LE(static_cast<double>(L::kMaxDash), m.captured(3).toDouble());
    EXPECT_GE(L::kMinDashTotal, m.captured(4).toDouble());
}

// Each drawingSettings vector read here, then an unrelated edit made: what
// the desktop writes back (its in-range values) the server takes, a second
// read writes nothing more, and a peer still holding the old values ends up
// with the same document without writing anything either (no back and forth).
TEST(Contracts, WhatTheDesktopWritesBackTheServerTakesAndItSettles) {
    const QJsonObject d = readJson(QStringLiteral("update-guard.json")).value(QStringLiteral("drawingSettings")).toObject();
    const QByteArray base = fromHex(d.value(QStringLiteral("base")));
    const QJsonArray vectors = d.value(QStringLiteral("vectors")).toArray();
    ASSERT_GE(vectors.size(), 20);
    for (const QJsonValue& v : vectors) {
        const QJsonObject o = v.toObject();
        const std::string what = o.value(QStringLiteral("what")).toString().toStdString();
        sync::SyncDoc doc;
        if (o.value(QStringLiteral("kind")).toString() == QLatin1String("update")) {
            ASSERT_TRUE(doc.applyUpdate(base)) << what;
        }
        ASSERT_TRUE(doc.applyUpdate(fromHex(o.value(QStringLiteral("hex"))))) << what;
        const QByteArray before = doc.encodeState();
        const QJsonObject was = doc.toJson();

        // The vectors' sheets go by their key alone; a layout's also carry it
        // as their id, which edits are matched by.
        QJsonObject withIds = was;
        QJsonObject data = withIds.value(QStringLiteral("layerData")).toObject();
        for (auto it = data.begin(); it != data.end(); ++it) {
            QJsonObject sheet = it.value().toObject();
            if (!sheet.contains(QStringLiteral("id"))) sheet.insert(QStringLiteral("id"), it.key());
            it.value() = sheet;
        }
        withIds.insert(QStringLiteral("layerData"), data);
        QString error;
        auto map = sync::mapFromDocJson(withIds, &error);
        ASSERT_TRUE(map) << what << ": " << error.toStdString();
        map->comment = QStringLiteral("an unrelated edit");
        const QByteArray update = doc.writeMap(*map);
        ASSERT_FALSE(update.isEmpty()) << what;
        EXPECT_TRUE(sync::guard::checkUpdate(update).isEmpty()) << what;
        expectServerTakes(was, doc.toJson(), what);
        // It does write back what it brought into range.
        if (what == "the sub-divisions set to a billion") {
            EXPECT_EQ(doc.toJson().value(QStringLiteral("layerData")).toObject().value(QStringLiteral("G")).toObject()
                          .value(QStringLiteral("subDivisionNumber")).toDouble(),
                      core::limits::kSubDivisionNumber.max);
        }

        // Read again: nothing more to write.
        auto again = sync::mapFromDocJson(doc.toJson(), &error);
        ASSERT_TRUE(again) << what;
        EXPECT_TRUE(doc.writeMap(*again).isEmpty()) << what << ": the desktop writes again";

        // A peer with the old values takes the update and agrees; reading
        // what it now has writes nothing either.
        sync::SyncDoc peer;
        ASSERT_TRUE(peer.applyUpdate(before));
        ASSERT_TRUE(peer.applyUpdate(update));
        EXPECT_EQ(peer.toJson(), doc.toJson()) << what;
        auto peers = sync::mapFromDocJson(peer.toJson(), &error);
        ASSERT_TRUE(peers) << what;
        EXPECT_TRUE(peer.writeMap(*peers).isEmpty()) << what << ": the peer writes back";
    }
}
