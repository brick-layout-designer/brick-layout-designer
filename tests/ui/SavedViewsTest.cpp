// Saved views' maths and pictures (ui/SavedViews.h): the same areas the
// web works out (its apps/web/src/test/viewsFixture.test.ts documents them
// for fixtures/layouts/views.bld-layout), picture sizes and names, and
// Export all views writing one PNG per view.

#include "ui/SavedViews.h"

#include "core/AnchoredLabel.h"
#include "core/LayerArea.h"
#include "core/LayerBrick.h"
#include "core/LayerGrid.h"
#include "core/LayerRuler.h"
#include "core/LayerText.h"
#include "core/Map.h"
#include "import/LayoutFile.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

using namespace bld;
using namespace bld::ui::views;

namespace {

const QString kFixture = QStringLiteral(BLD_SOURCE_DIR "/fixtures/layouts/views.bld-layout");

std::unique_ptr<core::Map> fixture(QTemporaryDir& dir) {
    auto read = import::readLayoutFile(kFixture, dir.path());
    EXPECT_TRUE(read.ok()) << read.error.toStdString();
    return std::move(read.map);
}

core::LayerBrick& addBricks(core::Map& map, const QString& id, std::initializer_list<QRectF> areas) {
    auto layer = std::make_unique<core::LayerBrick>();
    layer->guid = id;
    layer->name = id;
    for (const QRectF& r : areas) {
        core::Brick b;
        b.guid = QStringLiteral("b%1").arg(layer->bricks.size());
        b.partNumber = QStringLiteral("NOPART.1");
        b.displayArea = r;
        layer->bricks.push_back(b);
    }
    auto& ref = *layer;
    map.layers().push_back(std::move(layer));
    return ref;
}

core::AnchoredLabel label(core::AnchorKind kind, QPointF at) {
    core::AnchoredLabel l;
    l.id = QStringLiteral("l%1").arg(static_cast<int>(kind));
    l.text = QStringLiteral("Label");
    l.kind = kind;
    l.offset = at;
    return l;
}

// How many pixels differ from the top-left one.
int marked(const QImage& img) {
    const QRgb bg = img.pixel(0, 0);
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) n += img.pixel(x, y) != bg;
    return n;
}

}  // namespace

// The areas the web's viewsFixture.test.ts documents for the shared file.
TEST(SavedViews, WorksOutTheFixturesDocumentedAreas) {
    QTemporaryDir dir;
    auto map = fixture(dir);
    ASSERT_TRUE(map);
    const auto& views = map->sidecar.views;
    ASSERT_EQ(views.size(), 2u);
    // Fit: both sheets and the label at (70, -20), plus 4 studs.
    EXPECT_EQ(viewRegionStuds(views[0], *map), QRectF(-4, -24, 128, 88));
    // The area as saved.
    EXPECT_EQ(viewRegionStuds(views[1], *map), QRectF(90, 40, 40, 30));
    // The area view, fitted instead, covers only its one sheet (no labels).
    core::SavedView fitted = views[1];
    fitted.fit = true;
    EXPECT_EQ(viewRegionStuds(fitted, *map), QRectF(96, 46, 28, 18));
}

TEST(SavedViews, FitCountsWhatIsDrawnOnTheShownSheetsOnly) {
    core::Map map;
    auto& track = addBricks(map, QStringLiteral("track"), { QRectF(0, 0, 10, 10) });
    addBricks(map, QStringLiteral("town"), { QRectF(100, 100, 10, 10) });
    auto text = std::make_unique<core::LayerText>();
    text->guid = QStringLiteral("text");
    core::TextCell cell;
    cell.displayArea = QRectF(-50, 0, 5, 5);
    text->textCells.push_back(cell);
    map.layers().push_back(std::move(text));
    auto area = std::make_unique<core::LayerArea>();
    area->guid = QStringLiteral("area");
    area->areaCellSizeInStud = 16;
    area->cells.push_back({ 2, -3, Qt::red });  // studs (32, -48) to (48, -32)
    map.layers().push_back(std::move(area));
    auto ruler = std::make_unique<core::LayerRuler>();
    ruler->guid = QStringLiteral("ruler");
    core::LayerRuler::AnyRuler r;
    r.kind = core::RulerKind::Circular;
    r.circular.displayArea = QRectF(0, 200, 20, 20);
    ruler->rulers.push_back(r);
    map.layers().push_back(std::move(ruler));
    auto grid = std::make_unique<core::LayerGrid>();
    grid->guid = QStringLiteral("grid");
    map.layers().push_back(std::move(grid));

    const double m = kFitMarginStuds;
    EXPECT_EQ(fitRegionStuds(map, std::nullopt, true), QRectF(-50 - m, -48 - m, 160 + 2 * m, 268 + 2 * m));
    // Exactly the listed sheets.
    EXPECT_EQ(fitRegionStuds(map, QStringList{ QStringLiteral("town") }, true), QRectF(100 - m, 100 - m, 10 + 2 * m, 10 + 2 * m));
    // A sheet that is off doesn't count, unless the view lists it.
    for (auto& l : map.layers())
        if (l->guid != QLatin1String("track")) l->visible = false;
    EXPECT_EQ(fitRegionStuds(map, std::nullopt, true), QRectF(-m, -m, 10 + 2 * m, 10 + 2 * m));
    EXPECT_EQ(fitRegionStuds(map, QStringList{ QStringLiteral("area") }, true), QRectF(32 - m, -48 - m, 16 + 2 * m, 16 + 2 * m));

    // Labels: World, Group and Module ones at their offset; Brick labels and
    // labels turned off don't count. The room doesn't count.
    map.sidecar.anchoredLabels = { label(core::AnchorKind::World, QPointF(-30, 5)),
                                   label(core::AnchorKind::Brick, QPointF(500, 500)),
                                   label(core::AnchorKind::Module, QPointF(5, 40)) };
    map.sidecar.venue = core::Venue{};
    map.sidecar.venue->edges.push_back({ { QPointF(-900, -900), QPointF(900, 900) }, core::EdgeKind::Wall, 0.0, {} });
    EXPECT_EQ(fitRegionStuds(map, std::nullopt, true), QRectF(-30 - m, -m, 40 + 2 * m, 40 + 2 * m));
    EXPECT_EQ(fitRegionStuds(map, std::nullopt, false), QRectF(-m, -m, 10 + 2 * m, 10 + 2 * m));

    // Nothing drawn: no picture.
    track.bricks.clear();
    EXPECT_FALSE(fitRegionStuds(map, std::nullopt, false));
    core::SavedView v = newView(QStringLiteral("v"), QStringLiteral("V"));
    v.labels = false;
    EXPECT_FALSE(viewPicture(v, map));
    // "Use this area" with an empty area fits instead.
    v.labels = true;
    v.fit = false;
    v.rect = QRectF(0, 0, 0, 10);
    EXPECT_EQ(viewRegionStuds(v, map), fitRegionStuds(map, std::nullopt, true));
}

TEST(SavedViews, PictureSizesAndNames) {
    EXPECT_EQ(pictureSize(QRectF(0, 0, 128, 88), 1), QSize(1024, 704));
    EXPECT_EQ(pictureSize(QRectF(0, 0, 40, 30), 4), QSize(1280, 960));
    EXPECT_EQ(pictureSize(QRectF(0, 0, 10.06, 3), 1), QSize(80, 24));
    // Kept inside the largest picture, in proportion.
    EXPECT_EQ(pictureSize(QRectF(0, 0, 8192, 1024), 1), QSize(16384, 2048));
    EXPECT_EQ(shareScale(QRectF(0, 0, 100, 50)), 2.0);
    EXPECT_DOUBLE_EQ(shareScale(QRectF(0, 0, 1024, 50)), 0.5);  // 4096 px at most

    EXPECT_EQ(pictureFileName(QStringLiteral("Show 2026"), QStringLiteral("Station")), QStringLiteral("Show 2026 - Station.png"));
    EXPECT_EQ(pictureFileName(QStringLiteral("a/b:c"), QStringLiteral("x?*y")), QStringLiteral("a_b_c - x_y.png"));
    EXPECT_EQ(pictureFileName(QStringLiteral("  "), QString()), QStringLiteral("layout - View.png"));
    EXPECT_EQ(safeFileName(QString(100, QLatin1Char('x'))).size(), 80);

    core::SavedView v = newView(QStringLiteral("v"), QStringLiteral("  Yard "));
    EXPECT_EQ(v.name, QStringLiteral("Yard"));
    EXPECT_TRUE(v.fit);
    EXPECT_EQ(viewSummary(v, 3), QStringLiteral("Whole layout · all sheets"));
    v.fit = false;
    v.sheets = QStringList{ QStringLiteral("a") };
    EXPECT_EQ(viewSummary(v, 3), QStringLiteral("One area · 1 sheet"));
    v.sheets = QStringList{ QStringLiteral("a"), QStringLiteral("b") };
    EXPECT_EQ(viewSummary(v, 3), QStringLiteral("One area · 2 of 3 sheets"));
}

// A picture shows its sheets, its labels and (when on) the grid.
TEST(SavedViews, PicturesShowTheirSheetsAndGrid) {
    core::Map map;
    map.backgroundColor = core::ColorSpec::fromArgb(QColor(Qt::white));
    auto grid = std::make_unique<core::LayerGrid>();
    grid->guid = QStringLiteral("grid");
    grid->gridSizeInStud = 8;
    grid->displaySubGrid = false;
    map.layers().push_back(std::move(grid));
    addBricks(map, QStringLiteral("track"), { QRectF(2, 2, 12, 12) });
    parts::PartsLibrary parts;
    PictureRenderer renderer(map, parts);
    const QRectF region(0, 0, 16, 16);
    const QSize size = pictureSize(region, 1);
    const QImage all = renderer.render({ region, std::nullopt, false, true }, size);
    ASSERT_EQ(all.size(), QSize(128, 128));
    EXPECT_GT(marked(all), 0);  // the brick (a placeholder: no such part)
    const QImage none = renderer.render({ region, QStringList{}, false, true }, size);
    EXPECT_EQ(marked(none), 0);
    const QImage gridOnly = renderer.render({ region, QStringList{}, true, true }, size);
    EXPECT_GT(marked(gridOnly), 0);
    // The grid line at 8 studs is dark; a cell's middle stays white.
    EXPECT_NE(gridOnly.pixel(64, 20), gridOnly.pixel(30, 20));
}

TEST(SavedViews, ExportAllMakesOnePictureOfEachViewAtItsSize) {
    QTemporaryDir dir;
    auto map = fixture(dir);
    ASSERT_TRUE(map);
    parts::PartsLibrary parts;
    const QString out = dir.filePath(QStringLiteral("pictures"));
    ASSERT_TRUE(QDir().mkpath(out));

    auto r = exportAllViews(*map, parts, QStringLiteral("Show"), 1, out);
    ASSERT_EQ(r.files, (QStringList{ QStringLiteral("Show - Whole layout.png"), QStringLiteral("Show - Station.png") }));
    EXPECT_TRUE(r.skipped.isEmpty());
    EXPECT_TRUE(r.failed.isEmpty());
    EXPECT_EQ(QImage(QDir(out).filePath(r.files[0])).size(), QSize(128 * 8, 88 * 8));
    EXPECT_EQ(QImage(QDir(out).filePath(r.files[1])).size(), QSize(40 * 8, 30 * 8));
    EXPECT_EQ(QDir(out).entryList(QDir::Files).size(), 2);

    // Again at Large after a change: the same names, overwritten.
    map->sidecar.views[1].rect = QRectF(90, 40, 20, 10);
    r = exportAllViews(*map, parts, QStringLiteral("Show"), 4, out);
    EXPECT_EQ(r.files.size(), 2);
    EXPECT_EQ(QImage(QDir(out).filePath(QStringLiteral("Show - Station.png"))).size(), QSize(20 * 32, 10 * 32));
    EXPECT_EQ(QDir(out).entryList(QDir::Files).size(), 2);

    // Two views of one name don't overwrite each other.
    map->sidecar.views[1].name = QStringLiteral("whole layout");
    r = exportAllViews(*map, parts, QStringLiteral("Show"), 1, out);
    EXPECT_EQ(r.files, (QStringList{ QStringLiteral("Show - Whole layout.png"), QStringLiteral("Show - whole layout (2).png") }));

    // No saved views: one "Whole layout" picture.
    map->sidecar.views.clear();
    const QString empty = dir.filePath(QStringLiteral("none"));
    ASSERT_TRUE(QDir().mkpath(empty));
    r = exportAllViews(*map, parts, QStringLiteral("Show"), 2, empty);
    ASSERT_EQ(r.files, QStringList{ QStringLiteral("Show - Whole layout.png") });
    EXPECT_EQ(QImage(QDir(empty).filePath(r.files[0])).size(), QSize(128 * 16, 88 * 16));
}
