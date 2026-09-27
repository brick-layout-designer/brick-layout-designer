// Part list export laid out like BlueBrick's PartUsageView export.

#include "edit/Budget.h"
#include "edit/PartList.h"
#include "core/LayerBrick.h"
#include "core/Map.h"
#include "parts/ColorNames.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QDir>

using namespace bld;

namespace {

core::Map sampleMap() {
    core::Map map;
    map.author = QStringLiteral("Ada");
    map.date = QDate(2026, 9, 26);
    const auto add = [](core::LayerBrick& l, const char* part, int n) {
        for (int i = 0; i < n; ++i) { core::Brick b; b.partNumber = QLatin1String(part); l.bricks.push_back(b); }
    };
    auto tracks = std::make_unique<core::LayerBrick>();
    tracks->name = QStringLiteral("Tracks");
    add(*tracks, "2865.8", 3);
    add(*tracks, "3811.1", 1);
    auto hidden = std::make_unique<core::LayerBrick>();
    hidden->name = QStringLiteral("Hidden");
    hidden->visible = false;
    add(*hidden, "2865.8", 2);
    map.layers().push_back(std::move(tracks));
    map.layers().push_back(std::move(hidden));
    return map;
}

class PartListTest : public ::testing::Test {
protected:
    void SetUp() override {
        const QString root = QStringLiteral(BLD_SOURCE_DIR "/parts/BlueBrickParts/parts");
        if (QDir(root).exists()) { lib_.addSearchPath(root); lib_.scan(); }
    }
    parts::PartsLibrary lib_;
};

}  // namespace

TEST(ColorNames, FromBlueBricksTable) {
    EXPECT_EQ(parts::colorName(QStringLiteral("0")), QStringLiteral("Black"));
    EXPECT_EQ(parts::colorName(QStringLiteral("1")), QStringLiteral("Blue"));
    EXPECT_EQ(parts::colorName(QStringLiteral("1"), QStringLiteral("fr")), QStringLiteral("Bleu"));
    EXPECT_EQ(parts::colorName(QStringLiteral("1"), QStringLiteral("xx")), QStringLiteral("Blue"));
    EXPECT_EQ(parts::colorName(QStringLiteral("set")), QString());
}

TEST(PartList, PercentageBarLikeBlueBrick) {
    const QString full(QChar(0x2588)), empty(QChar(0x254C));
    EXPECT_EQ(edit::percentageBar(0), QString(QChar(0x258F)) + QString(9, QChar(0x254C)) + QStringLiteral(" 0%"));
    EXPECT_EQ(edit::percentageBar(45), QString(4, QChar(0x2588)) + QChar(0x258B) + QString(5, QChar(0x254C)) + QStringLiteral(" 45%"));
    EXPECT_EQ(edit::percentageBar(100), QString(10, QChar(0x2588)) + QStringLiteral(" 100%"));
    EXPECT_EQ(edit::percentageBar(1250), QString(10, QChar(0x2588)) + QStringLiteral(" 1,250%"));
}

TEST_F(PartListTest, RowsCountsAndBudget) {
    const core::Map map = sampleMap();
    edit::PartListOptions o;
    auto groups = edit::buildPartList(map, lib_, o);
    ASSERT_EQ(groups.size(), 1u);
    ASSERT_EQ(groups[0].rows.size(), 2u);
    EXPECT_EQ(groups[0].rows[0].part, QStringLiteral("2865"));
    EXPECT_EQ(groups[0].rows[0].count, 5);
    EXPECT_EQ(groups[0].rows[1].colorName, QStringLiteral("Blue"));
    EXPECT_EQ(groups[0].total.count, 6);

    o.includeHiddenLayers = false;
    EXPECT_EQ(edit::buildPartList(map, lib_, o)[0].rows[0].count, 3);

    edit::Budget budget;
    budget.setLimit(QStringLiteral("2865.8"), 4);
    o.includeHiddenLayers = true;
    o.budget = &budget;
    groups = edit::buildPartList(map, lib_, o);
    const auto& straight = groups[0].rows[0];
    EXPECT_EQ(straight.budget, 4);
    EXPECT_EQ(straight.missing, 1);
    EXPECT_DOUBLE_EQ(straight.usage, 125.0);
    EXPECT_LT(groups[0].rows[1].usage, 0) << "unbudgeted";
    EXPECT_EQ(groups[0].rows[1].missing, 1) << "unbudgeted parts are all missing";
    EXPECT_EQ(groups[0].total.budget, 4);
    EXPECT_EQ(groups[0].total.missing, 2);

    o.splitPerLayer = true;
    groups = edit::buildPartList(map, lib_, o);
    ASSERT_EQ(groups.size(), 2u);
    EXPECT_EQ(groups[1].name, QStringLiteral("Hidden"));
    EXPECT_EQ(groups[1].total.count, 2);
}

TEST_F(PartListTest, TextAndCsvLayout) {
    const core::Map map = sampleMap();
    edit::Budget budget;
    budget.setLimit(QStringLiteral("2865.8"), 10);
    edit::PartListOptions o;
    o.budget = &budget;
    const auto groups = edit::buildPartList(map, lib_, o);

    const QStringList text = edit::partListText(map, groups, QStringLiteral("Part List"), true).split(QLatin1Char('\n'));
    EXPECT_EQ(text[0], QString(20, QLatin1Char(' ')) + QStringLiteral("+===========+"));
    EXPECT_EQ(text[1], QString(20, QLatin1Char(' ')) + QStringLiteral("| Part List |"));
    EXPECT_EQ(text[4], QStringLiteral("Author: Ada"));
    EXPECT_EQ(text[7], QStringLiteral("Date: Saturday, September 26, 2026"));
    // Every table line has the same width.
    int width = -1;
    for (const QString& l : text) {
        if (!l.startsWith(QLatin1Char('+')) && !l.startsWith(QStringLiteral("| "))) continue;
        if (l.startsWith(QStringLiteral("+=")) ) continue;
        if (width < 0) width = l.size();
        EXPECT_EQ(l.size(), width) << l.toStdString();
    }
    EXPECT_TRUE(text.contains(QStringLiteral("| Part  | In Use | Color     | Description       | Budgeted   | Missing | Part Usage %   |")));
    EXPECT_TRUE(text.contains(QStringLiteral("| 3811  | 1      | Blue      | Baseplate 32 x 32 | Unbudgeted | 1       | Unbudgeted     |")));

    const QStringList csv = edit::partListCsv(groups, true).split(QLatin1Char('\n'));
    EXPECT_EQ(csv[0], QStringLiteral("Part,In Use,Color,Description,Budgeted,Missing,Part Usage %"));
    EXPECT_TRUE(csv[1].startsWith(QStringLiteral("2865,5,")));
    EXPECT_TRUE(csv[1].endsWith(QStringLiteral(",10,0,50"))) << csv[1].toStdString();
    EXPECT_TRUE(csv[2].endsWith(QStringLiteral(",Unbudgeted,1,Unbudgeted"))) << csv[2].toStdString();
    EXPECT_TRUE(csv[3].startsWith(QStringLiteral("Total,6,,,10,1,50"))) << csv[3].toStdString();
}

TEST_F(PartListTest, HtmlEmbedsPicturesAndEscapes) {
    core::Map map = sampleMap();
    map.comment = QStringLiteral("a < b\nnext");
    const auto groups = edit::buildPartList(map, lib_, {});
    QImage img(4, 4, QImage::Format_ARGB32);
    img.fill(Qt::red);
    const QString html = edit::partListHtml(map, groups, QStringLiteral("T"), false, [&](const QString&) { return img; });
    EXPECT_TRUE(html.contains(QStringLiteral("src=\"data:image/png;base64,")));
    EXPECT_TRUE(html.contains(QStringLiteral("a &lt; b<br/>next")));
    EXPECT_TRUE(html.contains(QStringLiteral("<td class=\"budget\">N/A</td>")));
    EXPECT_TRUE(html.contains(QStringLiteral("<tr class=\"total\">")));
}
