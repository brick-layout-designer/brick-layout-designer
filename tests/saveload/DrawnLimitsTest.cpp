// The settings the map is drawn from (core/DrawnLimits.h) are in range
// whatever a .bbm says: each one, read as NaN, Infinity, a negative and a
// huge number, comes out finite, whole where it should be, and in range.

#include "saveload/BbmReader.h"
#include "saveload/BbmWriter.h"

#include "core/DrawnLimits.h"
#include "core/LayerArea.h"
#include "core/LayerGrid.h"
#include "core/LayerRuler.h"
#include "core/Map.h"

#include <gtest/gtest.h>

#include <QBuffer>

#include <functional>
#include <limits>
#include <memory>

using namespace bld;

namespace {

const core::LayerGrid& gridOf(const core::Map& m) { return static_cast<const core::LayerGrid&>(*m.layers()[0]); }
const core::LayerArea& areaOf(const core::Map& m) { return static_cast<const core::LayerArea&>(*m.layers()[1]); }
const core::LayerRuler& rulersOf(const core::Map& m) { return static_cast<const core::LayerRuler&>(*m.layers()[2]); }
const core::LinearRuler& linearOf(const core::Map& m) { return rulersOf(m).rulers[0].linear; }
const core::CircularRuler& circularOf(const core::Map& m) { return rulersOf(m).rulers[1].circular; }

// A grid, an area and two rulers, each setting at a value of its own (so
// its text in the file can be found and changed).
QByteArray sampleBbm() {
    core::Map m;
    auto grid = std::make_unique<core::LayerGrid>();
    grid->guid = QStringLiteral("1");
    grid->transparency = 37;
    grid->hull.thickness = 7;
    grid->gridSizeInStud = 33;
    grid->subDivisionNumber = 5;
    grid->gridThickness = 3.0f;
    grid->subGridThickness = 6.0f;
    grid->cellIndexFont.sizePt = 11.0f;
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
    linear.kind = core::RulerKind::Linear;
    linear.linear.guid = QStringLiteral("4");
    linear.linear.lineThickness = 9.0f;
    linear.linear.guidelineThickness = 8.0f;
    linear.linear.guidelineDashPattern = { 6.0f, 7.0f };
    linear.linear.unit = 3;
    linear.linear.measureFont.sizePt = 13.0f;
    linear.linear.point1 = QPointF(1234, 0);
    linear.linear.point2 = QPointF(1300, 0);
    linear.linear.offsetDistance = 21.0f;
    rulers->rulers.push_back(linear);
    core::LayerRuler::AnyRuler circular;
    circular.kind = core::RulerKind::Circular;
    circular.circular.guid = QStringLiteral("5");
    circular.circular.lineThickness = 10.0f;
    circular.circular.measureFont.sizePt = 14.0f;
    circular.circular.center = QPointF(2345, 0);
    circular.circular.radius = 22.0f;
    rulers->rulers.push_back(circular);
    m.layers().push_back(std::move(rulers));
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    EXPECT_TRUE(saveload::writeBbm(m, buf).ok);
    return buf.data();
}

struct Field {
    const char* name;
    const char* text;  // the setting's element as written, to change
    std::function<double(const core::Map&)> value;
    // What reads back for NaN, Infinity, -5 and 1e12.
    double nan, infinity, negative, huge;
};

constexpr double kIntMax = std::numeric_limits<int>::max();

const Field kFields[] = {
    { "transparency", "<Transparency>37</Transparency>", [](const core::Map& m) { return gridOf(m).transparency; }, 100, 100, 0, 100 },
    { "hullThickness", "<hullThickness>7</hullThickness>", [](const core::Map& m) { return gridOf(m).hull.thickness; }, 1, 1, 1, 100 },
    { "gridSizeInStud", "<GridSizeInStud>33</GridSizeInStud>", [](const core::Map& m) { return gridOf(m).gridSizeInStud; }, 32, 32, 1, kIntMax },
    { "subDivisionNumber", "<SubDivisionNumber>5</SubDivisionNumber>", [](const core::Map& m) { return gridOf(m).subDivisionNumber; }, 4, 4, 2, 1024 },
    { "gridThickness", "<GridThickness>3</GridThickness>", [](const core::Map& m) { return gridOf(m).gridThickness; }, 2, 2, 0, 100 },
    { "subGridThickness", "<SubGridThickness>6</SubGridThickness>", [](const core::Map& m) { return gridOf(m).subGridThickness; }, 1, 1, 0, 100 },
    { "cellIndexFont.size", "<Size>11</Size>", [](const core::Map& m) { return gridOf(m).cellIndexFont.sizePt; }, 8.25, 8.25, 1, 500 },
    { "cellIndexCorner.x", "<X>41</X>", [](const core::Map& m) { return gridOf(m).cellIndexCorner.x(); }, 0, 0, -5, 1000000 },
    { "cellIndexColumnType", "<CellIndexColumnType>1</CellIndexColumnType>",
      [](const core::Map& m) { return static_cast<int>(gridOf(m).cellIndexColumnType); }, 0, 0, 0, 0 },
    { "cellIndexRowType", "<CellIndexRowType>0</CellIndexRowType>",
      [](const core::Map& m) { return static_cast<int>(gridOf(m).cellIndexRowType); }, 1, 1, 1, 1 },
    { "areaCellSize", "<AreaCellSize>17</AreaCellSize>", [](const core::Map& m) { return areaOf(m).areaCellSizeInStud; }, 32, 32, 1, 1024 },
    { "lineThickness", "<LineThickness>9</LineThickness>", [](const core::Map& m) { return linearOf(m).lineThickness; }, 1, 1, 0, 100 },
    { "guidelineThickness", "<GuidelineThickness>8</GuidelineThickness>", [](const core::Map& m) { return linearOf(m).guidelineThickness; }, 1, 1, 0, 100 },
    // A dash pattern with a value out of range is dropped (a solid line).
    { "guidelineDashPattern", "<value>6</value>",
      [](const core::Map& m) { return static_cast<double>(linearOf(m).guidelineDashPattern.size()); }, 0, 0, 0, 0 },
    { "unit", "<Unit>3</Unit>", [](const core::Map& m) { return linearOf(m).unit; }, 0, 0, 0, 0 },
    { "measureFont.size", "<Size>13</Size>", [](const core::Map& m) { return linearOf(m).measureFont.sizePt; }, 8.25, 8.25, 1, 500 },
    { "point1.x", "<X>1234</X>", [](const core::Map& m) { return linearOf(m).point1.x(); }, 0, 0, -5, 1e7 },
    { "offsetDistance", "<OffsetDistance>21</OffsetDistance>", [](const core::Map& m) { return linearOf(m).offsetDistance; }, 0, 0, -5, 1e7 },
    { "center.x", "<X>2345</X>", [](const core::Map& m) { return circularOf(m).center.x(); }, 0, 0, -5, 1e7 },
    { "radius", "<Radius>22</Radius>", [](const core::Map& m) { return circularOf(m).radius; }, 0, 0, 0, 1e7 },
};

std::unique_ptr<core::Map> readWith(const QByteArray& bbm, const char* text, const QString& value) {
    QByteArray changed = bbm;
    const QByteArray from(text);
    const QByteArray tag = from.left(from.indexOf('>') + 1);
    QByteArray to = tag + value.toUtf8() + "</" + tag.mid(1);
    changed.replace(from, to);
    QBuffer in(&changed);
    in.open(QIODevice::ReadOnly);
    auto res = saveload::readBbm(in);
    EXPECT_TRUE(res.map) << res.error.toStdString();
    return std::move(res.map);
}

}  // namespace

TEST(DrawnLimits, EachSettingIsWrittenOnceInTheSample) {
    const QByteArray bbm = sampleBbm();
    for (const Field& f : kFields) EXPECT_EQ(bbm.count(QByteArray(f.text)), 1) << f.name;
}

TEST(DrawnLimits, ASettingInRangeReadsAsItWas) {
    const QByteArray bbm = sampleBbm();
    QBuffer in;
    in.setData(bbm);
    in.open(QIODevice::ReadOnly);
    const auto res = saveload::readBbm(in);
    ASSERT_TRUE(res.map);
    const core::Map& m = *res.map;
    EXPECT_EQ(gridOf(m).transparency, 37);
    EXPECT_EQ(gridOf(m).subDivisionNumber, 5);
    EXPECT_EQ(gridOf(m).cellIndexCorner, QPoint(41, 42));
    EXPECT_EQ(areaOf(m).areaCellSizeInStud, 17);
    EXPECT_EQ(linearOf(m).guidelineDashPattern, (std::vector<float>{ 6.0f, 7.0f }));
    EXPECT_EQ(linearOf(m).unit, 3);
    EXPECT_EQ(circularOf(m).radius, 22.0f);
    // And writes back to the same bytes.
    QBuffer out;
    out.open(QIODevice::WriteOnly);
    ASSERT_TRUE(saveload::writeBbm(m, out).ok);
    EXPECT_EQ(out.data(), bbm);
}

TEST(DrawnLimits, EverySettingFromAFileIsKeptInRange) {
    const QByteArray bbm = sampleBbm();
    for (const Field& f : kFields) {
        const std::pair<QString, double> cases[] = { { QStringLiteral("NaN"), f.nan },
                                                     { QStringLiteral("Infinity"), f.infinity },
                                                     { QStringLiteral("-5"), f.negative },
                                                     { QStringLiteral("1e12"), f.huge } };
        for (const auto& [text, expected] : cases) {
            const auto map = readWith(bbm, f.text, text);
            ASSERT_TRUE(map) << f.name;
            EXPECT_DOUBLE_EQ(f.value(*map), expected) << f.name << " read as " << text.toStdString();
        }
    }
}

TEST(DrawnLimits, AMapIsBroughtIntoRangeWhateverItHolds) {
    core::Map m;
    auto grid = std::make_unique<core::LayerGrid>();
    grid->gridThickness = std::numeric_limits<float>::quiet_NaN();
    grid->subGridThickness = -std::numeric_limits<float>::infinity();
    grid->gridSizeInStud = std::numeric_limits<int>::min();
    grid->cellIndexColumnType = static_cast<core::CellIndexType>(7);
    m.layers().push_back(std::move(grid));
    auto rulers = std::make_unique<core::LayerRuler>();
    core::LayerRuler::AnyRuler r;
    r.linear.point2 = QPointF(std::numeric_limits<double>::infinity(), -1e300);
    r.linear.guidelineDashPattern = std::vector<float>(65, 2.0f);
    rulers->rulers.push_back(r);
    m.layers().push_back(std::move(rulers));
    core::keepDrawnSettingsInRange(m);
    const auto& g = static_cast<const core::LayerGrid&>(*m.layers()[0]);
    EXPECT_EQ(g.gridThickness, 2.0f);
    EXPECT_EQ(g.subGridThickness, 1.0f);
    EXPECT_EQ(g.gridSizeInStud, 1);
    EXPECT_EQ(g.cellIndexColumnType, core::CellIndexType::Letters);
    const auto& lin = static_cast<const core::LayerRuler&>(*m.layers()[1]).rulers[0].linear;
    EXPECT_EQ(lin.point2, QPointF(0, -1e7));
    EXPECT_TRUE(lin.guidelineDashPattern.empty());
}
