// Sync phase P3a, cross-language: the shared document the web server builds
// for a .bbm (fixtures/sync/<name>.ydoc, scripts/sync-fixtures), read by the
// desktop through yrs and mapped onto core::Map, must write the very bytes
// the desktop writes for that .bbm itself. layers.bbm has every layer kind:
// grid, brick, text (with groups), ruler, and area.

#include "DocJson.h"
#include "WebModel.h"
#include "WebModelWriter.h"

#include "core/LayerArea.h"
#include "core/LayerBrick.h"
#include "core/LayerGrid.h"
#include "core/LayerRuler.h"
#include "core/LayerText.h"
#include "core/Map.h"
#include "saveload/BbmReader.h"
#include "saveload/BbmWriter.h"

#include <gtest/gtest.h>

#include <QBuffer>
#include <QFile>
#include <QJsonArray>

#include <array>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

using namespace bld;

namespace {

QByteArray readFile(const QString& path) {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/") + path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QByteArray bbmBytes(const core::Map& map) {
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    const auto res = saveload::writeBbm(map, buf);
    EXPECT_TRUE(res.ok) << res.error.toStdString();
    return buf.data();
}

struct Case {
    const char* ydoc;
    const char* bbm;
};

class WebModelRoundTrip : public ::testing::TestWithParam<Case> {};

}  // namespace

TEST_P(WebModelRoundTrip, WritesWhatTheDesktopWritesForTheSameBbm) {
    const Case c = GetParam();
    const QByteArray bbm = readFile(QString::fromLatin1(c.bbm));
    ASSERT_FALSE(bbm.isEmpty());
    QBuffer in;
    in.setData(bbm);
    in.open(QIODevice::ReadOnly);
    const auto desktop = saveload::readBbm(in);
    ASSERT_TRUE(desktop.ok()) << desktop.error.toStdString();

    QString err;
    const auto json = sync::docToJson(readFile(QString::fromLatin1(c.ydoc)), &err);
    ASSERT_TRUE(json) << err.toStdString();
    const auto fromDoc = sync::mapFromDocJson(*json, &err);
    ASSERT_TRUE(fromDoc) << err.toStdString();

    const QByteArray want = bbmBytes(*desktop.map);
    const QByteArray got = bbmBytes(*fromDoc);
    if (got != want) {
        // Point at the first differing line rather than dumping 500 kB.
        const QList<QByteArray> a = got.split('\n'), b = want.split('\n');
        for (qsizetype i = 0; i < std::min(a.size(), b.size()); ++i)
            ASSERT_EQ(a[i].toStdString(), b[i].toStdString()) << "line " << i + 1;
        ASSERT_EQ(a.size(), b.size());
    }
}

INSTANTIATE_TEST_SUITE_P(Fixtures, WebModelRoundTrip,
    ::testing::Values(Case{ "fixtures/sync/tight-corner.ydoc", "fixtures/bbm-corpus/tight-corner.bbm" },
                      Case{ "fixtures/sync/layers.ydoc", "fixtures/sync/layers.bbm" }),
    [](const auto& info) { return info.index == 0 ? std::string("TightCorner") : std::string("AllLayerKinds"); });

TEST(WebModel, RefusesADocWithoutAHeaderOrWithAnUnknownLayer) {
    QString err;
    EXPECT_FALSE(sync::mapFromDocJson(QJsonObject{}, &err));
    EXPECT_FALSE(err.isEmpty());
    const QJsonObject meta{ { QStringLiteral("version"), 9 } };
    const QJsonObject bad{
        { QStringLiteral("meta"), meta },
        { QStringLiteral("layers"), QJsonArray{ QStringLiteral("L") } },
        { QStringLiteral("layerData"), QJsonObject{ { QStringLiteral("L"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("hologram") } } } } },
    };
    EXPECT_FALSE(sync::mapFromDocJson(bad, &err));
    EXPECT_EQ(err, QStringLiteral("unknown layer type \"hologram\""));
    // Ids in the order without layer data are skipped, as the web does.
    const QJsonObject empty{ { QStringLiteral("meta"), meta }, { QStringLiteral("layers"), QJsonArray{ QStringLiteral("gone") } } };
    const auto map = sync::mapFromDocJson(empty, &err);
    ASSERT_TRUE(map);
    EXPECT_TRUE(map->layers().empty());
}

// Whole numbers too big for an int (any writer may put any number in the
// shared document) are clamped, not cast as they are (the fuzzer's find).
TEST(WebModel, ReadsWholeNumbersTooBigForAnIntSafely) {
    const QJsonObject doc{
        { QStringLiteral("meta"), QJsonObject{ { QStringLiteral("version"), 9 } } },
        { QStringLiteral("layers"), QJsonArray{ QStringLiteral("G"), QStringLiteral("B") } },
        { QStringLiteral("layerData"),
          QJsonObject{ { QStringLiteral("G"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("grid") },
                                                         { QStringLiteral("gridSizeInStud"), 1e300 },
                                                         { QStringLiteral("cellIndexCorner"),
                                                           QJsonObject{ { QStringLiteral("x"), -1e300 }, { QStringLiteral("y"), 5 } } } } },
                       { QStringLiteral("B"),
                         QJsonObject{ { QStringLiteral("type"), QStringLiteral("brick") },
                                      { QStringLiteral("bricks"),
                                        QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("b1") },
                                                                 { QStringLiteral("partNumber"), QStringLiteral("3001.1") },
                                                                 { QStringLiteral("activeConnectionPointIndex"), 4e9 } } } } } } } },
    };
    QString err;
    const auto map = sync::mapFromDocJson(doc, &err);
    ASSERT_TRUE(map) << err.toStdString();
    ASSERT_EQ(map->layers().size(), 2u);
    const auto& grid = static_cast<const core::LayerGrid&>(*map->layers()[0]);
    EXPECT_EQ(grid.gridSizeInStud, std::numeric_limits<int>::max());
    // A corner that far out is kept to a million cells (core/DrawnLimits.h).
    EXPECT_EQ(grid.cellIndexCorner, QPoint(-1000000, 5));
    const auto& bricks = static_cast<const core::LayerBrick&>(*map->layers()[1]);
    ASSERT_EQ(bricks.bricks.size(), 1u);
    EXPECT_EQ(bricks.bricks[0].activeConnectionPointIndex, std::numeric_limits<int>::max());
}

TEST(WebModel, ItemsKeepTheirDocumentIds) {
    // Bricks, groups and text cells carry the doc's ids: sync matches edits by them.
    QString err;
    const auto json = sync::docToJson(readFile(QStringLiteral("fixtures/sync/layers.ydoc")), &err);
    ASSERT_TRUE(json) << err.toStdString();
    const auto map = sync::mapFromDocJson(*json, &err);
    ASSERT_TRUE(map);
    const QJsonObject data = json->value(QStringLiteral("layerData")).toObject();
    int texts = 0, bricks = 0;
    for (const auto& layer : map->layers()) {
        const QJsonObject l = data.value(layer->guid).toObject();
        if (layer->kind() == core::LayerKind::Text) {
            const auto& cells = static_cast<const core::LayerText&>(*layer).textCells;
            const QJsonArray docCells = l.value(QStringLiteral("textCells")).toArray();
            ASSERT_EQ(cells.size(), static_cast<size_t>(docCells.size()));
            for (size_t i = 0; i < cells.size(); ++i) {
                EXPECT_FALSE(cells[i].guid.isEmpty());
                EXPECT_EQ(cells[i].guid, docCells[static_cast<qsizetype>(i)].toObject().value(QStringLiteral("id")).toString());
            }
            texts += static_cast<int>(cells.size());
        } else if (layer->kind() == core::LayerKind::Brick) {
            const auto& bl = static_cast<const core::LayerBrick&>(*layer);
            const QJsonArray docBricks = l.value(QStringLiteral("bricks")).toArray();
            for (size_t i = 0; i < bl.bricks.size(); ++i)
                EXPECT_EQ(bl.bricks[i].guid, docBricks[static_cast<qsizetype>(i)].toObject().value(QStringLiteral("id")).toString());
            bricks += static_cast<int>(bl.bricks.size());
        }
    }
    EXPECT_GT(texts, 0);
    EXPECT_GT(bricks, 0);
}

// The settings the map is drawn from (core/DrawnLimits.h) are in range
// whatever a live layout's document says: each one, set to NaN, Infinity,
// a negative and a huge number, reads finite, whole where it should be and
// in range, as from a .bbm (tests/saveload/DrawnLimitsTest.cpp).
namespace {

QJsonObject drawnSampleDoc() {
    core::Map m;
    auto grid = std::make_unique<core::LayerGrid>();
    grid->guid = QStringLiteral("1");
    grid->transparency = 37;
    grid->hull.thickness = 7;
    grid->gridSizeInStud = 33;
    grid->subDivisionNumber = 5;
    grid->cellIndexCorner = QPoint(41, 42);
    grid->cellIndexColumnType = core::CellIndexType::Numbers;
    grid->cellIndexRowType = core::CellIndexType::Letters;
    m.layers().push_back(std::move(grid));
    auto area = std::make_unique<core::LayerArea>();
    area->guid = QStringLiteral("2");
    area->areaCellSizeInStud = 17;
    m.layers().push_back(std::move(area));
    auto rulers = std::make_unique<core::LayerRuler>();
    rulers->guid = QStringLiteral("3");
    core::LayerRuler::AnyRuler linear;
    linear.linear.guid = QStringLiteral("4");
    linear.linear.guidelineDashPattern = { 6.0f, 7.0f };
    linear.linear.unit = 3;
    rulers->rulers.push_back(linear);
    core::LayerRuler::AnyRuler circular;
    circular.kind = core::RulerKind::Circular;
    circular.circular.guid = QStringLiteral("5");
    circular.circular.radius = 22.0f;
    rulers->rulers.push_back(circular);
    m.layers().push_back(std::move(rulers));
    return sync::docJsonFromMap(m);
}

// `doc` with the value at `path` (object keys, or array indexes as numbers) replaced.
QJsonValue withValue(const QJsonValue& at, const QStringList& path, const QJsonValue& value) {
    if (path.isEmpty()) return value;
    const QString key = path.first();
    const QStringList rest = path.mid(1);
    if (at.isArray()) {
        QJsonArray a = at.toArray();
        const int i = key.toInt();
        a[i] = withValue(a[i], rest, value);
        return a;
    }
    QJsonObject o = at.toObject();
    o[key] = withValue(o.value(key), rest, value);
    return o;
}

struct DocField {
    const char* name;
    QStringList path;
    std::function<double(const core::Map&)> value;
    double nan, infinity, negative, huge;
    bool asText = false;  // stored as the .bbm's integer text
};

const core::LayerGrid& docGrid(const core::Map& m) { return static_cast<const core::LayerGrid&>(*m.layers()[0]); }
const core::LayerArea& docArea(const core::Map& m) { return static_cast<const core::LayerArea&>(*m.layers()[1]); }
const core::LayerRuler& docRulers(const core::Map& m) { return static_cast<const core::LayerRuler&>(*m.layers()[2]); }
const core::LinearRuler& docLinear(const core::Map& m) { return docRulers(m).rulers[0].linear; }
const core::CircularRuler& docCircular(const core::Map& m) { return docRulers(m).rulers[1].circular; }

}  // namespace

TEST(WebModel, EveryDrawnSettingInALiveLayoutIsKeptInRange) {
    const double kIntMax = std::numeric_limits<int>::max();
    const QStringList grid{ QStringLiteral("layerData"), QStringLiteral("1") };
    const QStringList linear{ QStringLiteral("layerData"), QStringLiteral("3"), QStringLiteral("rulerItems"), QStringLiteral("0") };
    const QStringList circular{ QStringLiteral("layerData"), QStringLiteral("3"), QStringLiteral("rulerItems"), QStringLiteral("1") };
    const auto at = [](QStringList base, const QStringList& more) { return base + more; };
    const std::vector<DocField> fields{
        { "transparency", at(grid, { QStringLiteral("transparency") }), [](const core::Map& m) { return docGrid(m).transparency; }, 100, 100, 0, 100 },
        { "hullThickness", at(grid, { QStringLiteral("hullProperties"), QStringLiteral("hullThickness") }),
          [](const core::Map& m) { return docGrid(m).hull.thickness; }, 1, 1, 1, 100 },
        { "gridSizeInStud", at(grid, { QStringLiteral("gridSizeInStud") }), [](const core::Map& m) { return docGrid(m).gridSizeInStud; }, 32, 32, 1, kIntMax },
        { "subDivisionNumber", at(grid, { QStringLiteral("subDivisionNumber") }), [](const core::Map& m) { return docGrid(m).subDivisionNumber; }, 4, 4, 2, 1024 },
        { "gridThickness", at(grid, { QStringLiteral("gridThickness") }), [](const core::Map& m) { return docGrid(m).gridThickness; }, 2, 2, 0, 100 },
        { "subGridThickness", at(grid, { QStringLiteral("subGridThickness") }), [](const core::Map& m) { return docGrid(m).subGridThickness; }, 1, 1, 0, 100 },
        { "cellIndexFont.size", at(grid, { QStringLiteral("cellIndexFont"), QStringLiteral("size") }),
          [](const core::Map& m) { return docGrid(m).cellIndexFont.sizePt; }, 8.25, 8.25, 1, 500 },
        { "cellIndexCorner.x", at(grid, { QStringLiteral("cellIndexCorner"), QStringLiteral("x") }),
          [](const core::Map& m) { return docGrid(m).cellIndexCorner.x(); }, 0, 0, -5, 1000000 },
        { "cellIndexColumnType", at(grid, { QStringLiteral("cellIndexColumnType") }),
          [](const core::Map& m) { return static_cast<int>(docGrid(m).cellIndexColumnType); }, 0, 0, 0, 0, true },
        { "cellIndexRowType", at(grid, { QStringLiteral("cellIndexRowType") }),
          [](const core::Map& m) { return static_cast<int>(docGrid(m).cellIndexRowType); }, 1, 1, 1, 1, true },
        { "areaCellSize", { QStringLiteral("layerData"), QStringLiteral("2"), QStringLiteral("areaCellSize") },
          [](const core::Map& m) { return docArea(m).areaCellSizeInStud; }, 32, 32, 1, 1024 },
        { "lineThickness", at(linear, { QStringLiteral("lineThickness") }), [](const core::Map& m) { return docLinear(m).lineThickness; }, 1, 1, 0, 100 },
        { "guidelineThickness", at(linear, { QStringLiteral("guidelineThickness") }),
          [](const core::Map& m) { return docLinear(m).guidelineThickness; }, 1, 1, 0, 100 },
        { "guidelineDashPattern", at(linear, { QStringLiteral("guidelineDashPattern"), QStringLiteral("0") }),
          [](const core::Map& m) { return static_cast<double>(docLinear(m).guidelineDashPattern.size()); }, 0, 0, 0, 0 },
        { "unit", at(linear, { QStringLiteral("unit") }), [](const core::Map& m) { return docLinear(m).unit; }, 0, 0, 0, 0 },
        { "measureFont.size", at(linear, { QStringLiteral("measureFont"), QStringLiteral("size") }),
          [](const core::Map& m) { return docLinear(m).measureFont.sizePt; }, 8.25, 8.25, 1, 500 },
        { "point1.x", at(linear, { QStringLiteral("point1"), QStringLiteral("x") }), [](const core::Map& m) { return docLinear(m).point1.x(); }, 0, 0, -5, 1e7 },
        { "offsetDistance", at(linear, { QStringLiteral("offsetDistance") }), [](const core::Map& m) { return docLinear(m).offsetDistance; }, 0, 0, -5, 1e7 },
        { "center.x", at(circular, { QStringLiteral("center"), QStringLiteral("x") }), [](const core::Map& m) { return docCircular(m).center.x(); }, 0, 0, -5, 1e7 },
        { "radius", at(circular, { QStringLiteral("radius") }), [](const core::Map& m) { return docCircular(m).radius; }, 0, 0, 0, 1e7 },
    };
    const QJsonObject doc = drawnSampleDoc();
    {
        QString err;
        const auto map = sync::mapFromDocJson(doc, &err);
        ASSERT_TRUE(map) << err.toStdString();
        EXPECT_EQ(docGrid(*map).subDivisionNumber, 5);
        EXPECT_EQ(docGrid(*map).cellIndexColumnType, core::CellIndexType::Numbers);
        EXPECT_EQ(docLinear(*map).guidelineDashPattern.size(), 2u);
        EXPECT_EQ(docLinear(*map).unit, 3);
        EXPECT_EQ(docCircular(*map).radius, 22.0f);
    }
    for (const DocField& f : fields) {
        const std::array<std::pair<QJsonValue, double>, 4> numbers{ { { std::numeric_limits<double>::quiet_NaN(), f.nan },
                                                          { std::numeric_limits<double>::infinity(), f.infinity },
                                                          { -5.0, f.negative },
                                                          { 1e12, f.huge } } };
        const std::array<std::pair<QJsonValue, double>, 4> texts{ { { QStringLiteral("NaN"), f.nan },
                                                        { QStringLiteral("Infinity"), f.infinity },
                                                        { QStringLiteral("-5"), f.negative },
                                                        { QStringLiteral("1e12"), f.huge } } };
        for (const auto& [value, expected] : f.asText ? texts : numbers) {
            QString err;
            const auto map = sync::mapFromDocJson(withValue(doc, f.path, value).toObject(), &err);
            ASSERT_TRUE(map) << f.name << ": " << err.toStdString();
            EXPECT_DOUBLE_EQ(f.value(*map), expected) << f.name << " set to " << value.toVariant().toString().toStdString();
        }
    }
}
