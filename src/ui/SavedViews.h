#pragma once

// Saved views and pictures of the layout (the web's references/
// LAYOUT-FILE.md "Saved views", apps/web/src/editor/savedViews.ts). A
// saved view is a named picture of the layout: which part of it (the whole
// layout, or a fixed area), which sheets, and whether the grid and the
// labels show.
//
// "Fit the whole layout" works the area out each time a picture is made,
// from what's drawn on the view's sheets, so after the layout changes one
// click makes every picture again.

#include "../core/SavedView.h"

#include <QColor>
#include <QImage>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QGraphicsScene;

namespace bld::core { class Layer; class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::ui::views {

// Space left around the layout when a view fits the whole layout, in studs.
inline constexpr double kFitMarginStuds = 4.0;
// Output pixels per stud at scale 1 (the map's own 8 px per stud).
inline constexpr int kPxPerStud = 8;
// The largest picture side and area, as on the web (its canvas limits).
inline constexpr int kMaxPictureSide = 16384;

// What a picture shows.
struct PictureSpec {
    QRectF region;  // the area, in studs
    std::optional<QStringList> sheets;  // layer ids shown, or unset for the layout's own on/off
    bool grid = false;
    bool labels = true;
};

// A new view: fits the whole layout, every sheet, labels on.
core::SavedView newView(const QString& id, const QString& name, bool grid = false);
// The picture "Share picture" and "Export all views" make when there are
// no saved views: fit, every sheet, no grid, labels on.
core::SavedView wholeLayout();

// Whether `layer` shows under `sheets` (unset: its own on/off; a list:
// exactly the sheets in it).
bool sheetShown(const core::Layer& layer, const std::optional<QStringList>& sheets);

// "Fit the whole layout": the bounds of what's drawn on the shown sheets
// (bricks, text, rulers, painted areas, plus the World, Group and Module
// labels when `labels`), grown by kFitMarginStuds on every side. Unset when
// nothing is drawn. The room and the background picture don't count.
std::optional<QRectF> fitRegionStuds(const core::Map& map, const std::optional<QStringList>& sheets, bool labels);
// The area a view's picture covers, in studs, or unset when there's nothing to show.
std::optional<QRectF> viewRegionStuds(const core::SavedView& view, const core::Map& map);
// The picture a view makes, or unset when there's nothing to show.
std::optional<PictureSpec> viewPicture(const core::SavedView& view, const core::Map& map);

// Output size in pixels of `region` at `scale` (1 = 8 px per stud),
// rounded to whole pixels and kept inside kMaxPictureSide.
QSize pictureSize(const QRectF& region, double scale);
// A picture's scale for sharing: `preferred`, made smaller when the
// longest side would pass `maxSide`.
double shareScale(const QRectF& region, double preferred = 2.0, double maxSide = 4096.0);

// Safe as a file name: no \ / : * ? " < > | or control characters, at
// most 80 characters; empty becomes "layout" (the web's sanitizeFilename).
QString safeFileName(const QString& name);
// "<layout> - <view>.png", safe as a file name.
QString pictureFileName(const QString& layoutTitle, const QString& viewName);

// "Whole layout · all sheets", for the Views panel.
QString viewSummary(const core::SavedView& view, int sheetCount);

// Picture sizes for Export all views: Small 1×, Medium 2×, Large 4×.
struct ExportSize {
    double scale;
    QString label;
};
std::vector<ExportSize> exportSizes();

// The map drawn from `source` (scene pixels) into a `size` image: the
// renderer File > Export as Image uses, and every picture. `background`
// fills the image unless `transparent`; `watermark`, when set, is stamped
// bottom right.
struct SceneImageOptions {
    QColor background = Qt::white;
    bool transparent = false;
    bool antialias = true;
    Qt::AspectRatioMode aspect = Qt::IgnoreAspectRatio;
    QString watermark;
    // Painted after the background and before the map (a picture's grid).
    std::function<void(class QPainter&)> underlay;
};
QImage renderSceneImage(QGraphicsScene& scene, const QRectF& source, QSize size, const SceneImageOptions& options);

// Draws pictures of one map. The scene is built once, so making many
// pictures (Export all views) doesn't rebuild it for each.
class PictureRenderer {
public:
    PictureRenderer(const core::Map& map, parts::PartsLibrary& parts);
    ~PictureRenderer();
    PictureRenderer(const PictureRenderer&) = delete;
    PictureRenderer& operator=(const PictureRenderer&) = delete;

    // The picture at exactly `size` pixels: its sheets, its labels and
    // (when on) the grid, over the layout's background colour.
    QImage render(const PictureSpec& spec, QSize size);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

struct ExportAllResult {
    QStringList files;    // file names written, in view order
    QStringList skipped;  // views with nothing to show
    QStringList failed;   // file names that couldn't be written
};

// "Export all views": one PNG per saved view (or one "Whole layout"
// picture when there are none), named "<layout> - <view>.png", written
// into `folder` over any earlier ones. Two views of one name get " (2)".
ExportAllResult exportAllViews(const core::Map& map, parts::PartsLibrary& parts, const QString& layoutTitle,
                               double scale, const QString& folder);

}  // namespace bld::ui::views
