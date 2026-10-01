#include "SavedViews.h"

#include "../core/AnchoredLabel.h"
#include "../core/Layer.h"
#include "../core/LayerArea.h"
#include "../core/LayerBrick.h"
#include "../core/LayerGrid.h"
#include "../core/LayerRuler.h"
#include "../core/LayerText.h"
#include "../core/Map.h"
#include "../core/Module.h"
#include "../rendering/SceneBuilder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFont>
#include <QGraphicsScene>
#include <QPainter>
#include <QPen>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace bld::ui::views {

namespace {

QString tr(const char* text) { return QCoreApplication::translate("SavedViews", text); }

// Grows a box point by point, like the web's contentBoundsStuds.
struct Bounds {
    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    void add(double x, double y, double w = 0, double h = 0) {
        if (!std::isfinite(x) || !std::isfinite(y)) return;
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x + w);
        maxY = std::max(maxY, y + h);
    }
    void add(const QRectF& r) { add(r.x(), r.y(), r.width(), r.height()); }
    bool empty() const { return !std::isfinite(minX); }
};

// The grid a picture with the grid on draws: the first grid layer that
// shows (the web's drawnGridLayer).
const core::LayerGrid* drawnGrid(const core::Map& map) {
    for (const auto& l : map.layers())
        if (l && l->kind() == core::LayerKind::Grid && l->visible) return static_cast<const core::LayerGrid*>(l.get());
    return nullptr;
}

// A grid's lines (sub-grid first, then the main grid) over `region`, one
// stud being `pxPerStud` output pixels; lines sit on whole multiples of the
// grid size, like the grid on screen (the web's paintGridLines).
void paintGrid(QPainter& p, const core::LayerGrid& g, const QRectF& region, double pxPerStudX, double pxPerStudY) {
    const auto pass = [&](double step, const QColor& color, double thickness) {
        if (!(step > 0)) return;
        if (region.width() / step > 4000 || region.height() / step > 4000) return;  // too fine to see anyway
        QPen pen(color);
        pen.setWidthF(std::max(1.0, thickness * std::min(pxPerStudX, pxPerStudY) / kPxPerStud));
        p.setPen(pen);
        const double w = region.width() * pxPerStudX, h = region.height() * pxPerStudY;
        // Counted in whole lines, so the steps don't drift.
        const auto first = [step](double from) { return static_cast<long long>(std::ceil(from / step)); };
        for (long long i = first(region.x()); static_cast<double>(i) * step <= region.right(); ++i) {
            const double px = (static_cast<double>(i) * step - region.x()) * pxPerStudX;
            p.drawLine(QPointF(px, 0), QPointF(px, h));
        }
        for (long long i = first(region.y()); static_cast<double>(i) * step <= region.bottom(); ++i) {
            const double py = (static_cast<double>(i) * step - region.y()) * pxPerStudY;
            p.drawLine(QPointF(0, py), QPointF(w, py));
        }
    };
    if (g.displaySubGrid)
        pass(static_cast<double>(g.gridSizeInStud) / std::max(2, g.subDivisionNumber), g.subGridColor.color,
             g.subGridThickness);
    if (g.displayGrid) pass(g.gridSizeInStud, g.gridColor.color, g.gridThickness);
}

}  // namespace

core::SavedView newView(const QString& id, const QString& name, bool grid) {
    core::SavedView v;
    v.id = id;
    v.name = name.trimmed().isEmpty() ? tr("View") : name.trimmed();
    v.fit = true;
    v.grid = grid;
    v.labels = true;
    return v;
}

core::SavedView wholeLayout() {
    core::SavedView v = newView(QStringLiteral("whole-layout"), tr("Whole layout"));
    v.grid = false;
    return v;
}

bool sheetShown(const core::Layer& layer, const std::optional<QStringList>& sheets) {
    return sheets ? sheets->contains(layer.guid) : layer.visible;
}

std::optional<QRectF> fitRegionStuds(const core::Map& map, const std::optional<QStringList>& sheets, bool labels,
                                     const rendering::SceneBuilder* drawn) {
    Bounds b;
    QSet<QString> shownBricks;  // for the module frames and names
    for (const auto& lp : map.layers()) {
        if (!lp || !sheetShown(*lp, sheets)) continue;
        switch (lp->kind()) {
        case core::LayerKind::Brick:
            for (const auto& br : static_cast<const core::LayerBrick&>(*lp).bricks) {
                b.add(br.displayArea);
                if (drawn) shownBricks.insert(br.guid);
            }
            break;
        case core::LayerKind::Text:
            for (const auto& c : static_cast<const core::LayerText&>(*lp).textCells) b.add(c.displayArea);
            break;
        case core::LayerKind::Ruler:
            for (const auto& r : static_cast<const core::LayerRuler&>(*lp).rulers)
                b.add(r.kind == core::RulerKind::Linear ? r.linear.displayArea : r.circular.displayArea);
            break;
        case core::LayerKind::Area: {
            const auto& area = static_cast<const core::LayerArea&>(*lp);
            const double s = area.areaCellSizeInStud;
            for (const auto& c : area.cells) b.add(c.x * s, c.y * s, s, s);
            break;
        }
        default:
            break;
        }
    }
    if (labels) {
        // World, Group and Module labels sit at their offset as a world
        // position; Brick labels ride on bricks already inside the box.
        for (const auto& l : map.sidecar.anchoredLabels)
            if (l.kind != core::AnchorKind::Brick) b.add(l.offset.x(), l.offset.y());
    }
    if (drawn) {
        // Module frames and names, which sit outside the modules.
        const double k = rendering::SceneBuilder::kPixelsPerStud;
        for (const QPair<QString, QRectF>& drawnModule : drawn->moduleAnnotationRects()) {
            const QRectF& px = drawnModule.second;
            bool shown = false;
            for (const core::Module& mod : map.sidecar.modules) {
                if (mod.id != drawnModule.first) continue;
                for (const QString& member : mod.memberIds) shown = shown || shownBricks.contains(member);
            }
            if (shown) b.add(px.x() / k, px.y() / k, px.width() / k, px.height() / k);
        }
    }
    if (b.empty()) return std::nullopt;
    const double m = kFitMarginStuds;
    return QRectF(b.minX - m, b.minY - m, b.maxX - b.minX + 2 * m, b.maxY - b.minY + 2 * m);
}

std::optional<QRectF> viewRegionStuds(const core::SavedView& view, const core::Map& map,
                                      const rendering::SceneBuilder* drawn) {
    if (!view.fit && view.rect && view.rect->width() > 0 && view.rect->height() > 0) return view.rect;
    return fitRegionStuds(map, view.sheets, view.labels, drawn);
}

std::optional<PictureSpec> viewPicture(const core::SavedView& view, const core::Map& map,
                                       const rendering::SceneBuilder* drawn) {
    const auto region = viewRegionStuds(view, map, drawn);
    if (!region) return std::nullopt;
    return PictureSpec{ *region, view.sheets, view.grid, view.labels };
}

QSize pictureSize(const QRectF& region, double scale) {
    int w = std::max(1, static_cast<int>(std::lround(region.width() * kPxPerStud * scale)));
    int h = std::max(1, static_cast<int>(std::lround(region.height() * kPxPerStud * scale)));
    const double side = static_cast<double>(kMaxPictureSide);
    const double k = std::min({ 1.0, side / std::max(w, h), std::sqrt(side * side / (static_cast<double>(w) * h)) });
    if (k < 1) {
        w = std::max(1, static_cast<int>(std::floor(w * k)));
        h = std::max(1, static_cast<int>(std::floor(h * k)));
    }
    return { w, h };
}

double scaleForSide(const QRectF& region, double maxSide, double most) {
    const double longest = std::max(region.width(), region.height()) * kPxPerStud;
    if (!(longest > 0)) return most;
    return std::min(most, maxSide / longest);
}

double shareScale(const QRectF& region, double preferred, double maxSide) {
    return scaleForSide(region, maxSide, preferred);
}

QString safeFileName(const QString& name) {
    static const QRegularExpression bad(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1F]+"));
    QString cleaned = name;
    cleaned.replace(bad, QStringLiteral("_"));
    cleaned = cleaned.trimmed();
    return cleaned.isEmpty() ? QStringLiteral("layout") : cleaned.left(80);
}

QString pictureFileName(const QString& layoutTitle, const QString& viewName) {
    return safeFileName(layoutTitle) + QStringLiteral(" - ")
         + safeFileName(viewName.isEmpty() ? tr("View") : viewName) + QStringLiteral(".png");
}

QString viewSummary(const core::SavedView& view, int sheetCount) {
    const QString area = view.fit ? tr("Whole layout") : tr("One area");
    QString sheets;
    if (!view.sheets) sheets = tr("all sheets");
    else if (view.sheets->size() == 1) sheets = tr("1 sheet");
    else sheets = tr("%1 of %2 sheets").arg(view.sheets->size()).arg(sheetCount);
    return area + QStringLiteral(" · ") + sheets;
}

std::vector<ExportSize> exportSizes() {
    return { { 1280, tr("Small") }, { kDefaultExportMaxSide, tr("Medium") }, { 5120, tr("Large") } };
}

QImage renderSceneImage(QGraphicsScene& scene, const QRectF& source, QSize size, const SceneImageOptions& o) {
    QImage img(size, o.transparent ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    img.fill(o.transparent ? QColor(Qt::transparent) : o.background);
    QPainter p(&img);
    if (o.antialias) {
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
    }
    if (o.underlay) {
        p.save();
        o.underlay(p);
        p.restore();
    }
    scene.render(&p, QRectF(0, 0, size.width(), size.height()), source, o.aspect);
    if (!o.watermark.isEmpty()) {
        QFont f;
        f.setPointSize(std::max(8, size.height() / 60));
        p.setFont(f);
        p.setPen(QColor(0, 0, 0, 140));
        p.drawText(QRectF(0, 0, size.width(), size.height()).adjusted(10, 0, -10, -10), Qt::AlignRight | Qt::AlignBottom,
                   o.watermark);
    }
    return img;
}

struct PictureRenderer::Impl {
    const core::Map& map;
    QGraphicsScene scene;
    rendering::SceneBuilder builder;
    Impl(const core::Map& m, parts::PartsLibrary& parts) : map(m), builder(scene, parts) { builder.build(map); }
};

PictureRenderer::PictureRenderer(const core::Map& map, parts::PartsLibrary& parts)
    : d_(std::make_unique<Impl>(map, parts)) {}

PictureRenderer::~PictureRenderer() = default;

const rendering::SceneBuilder& PictureRenderer::builder() const { return d_->builder; }

QImage PictureRenderer::render(const PictureSpec& spec, QSize size) {
    const auto& layers = d_->map.layers();
    for (size_t i = 0; i < layers.size(); ++i)
        if (layers[i] && layers[i]->kind() != core::LayerKind::Grid)
            d_->builder.setLayerVisible(static_cast<int>(i), sheetShown(*layers[i], spec.sheets));
    d_->builder.setLabelsVisible(spec.labels);

    SceneImageOptions o;
    o.background = d_->map.backgroundColor.color;
    const core::LayerGrid* grid = spec.grid ? drawnGrid(d_->map) : nullptr;
    if (grid) {
        const QRectF region = spec.region;
        o.underlay = [grid, region, size](QPainter& p) {
            paintGrid(p, *grid, region, size.width() / region.width(), size.height() / region.height());
        };
    }
    const QRectF px(spec.region.x() * kPxPerStud, spec.region.y() * kPxPerStud, spec.region.width() * kPxPerStud,
                    spec.region.height() * kPxPerStud);
    return renderSceneImage(d_->scene, px, size, o);
}

ExportAllResult exportAllViews(const core::Map& map, parts::PartsLibrary& parts, const QString& layoutTitle,
                               int maxSide, const QString& folder) {
    ExportAllResult out;
    std::vector<core::SavedView> list = map.sidecar.views;
    if (list.empty()) list.push_back(wholeLayout());
    PictureRenderer renderer(map, parts);
    QSet<QString> used;
    const QDir dir(folder);
    for (const auto& view : list) {
        const std::optional<PictureSpec> found = viewPicture(view, map, &renderer.builder());
        if (!found.has_value()) {
            out.skipped << view.name;
            continue;
        }
        const PictureSpec& spec = found.value();
        QString name = pictureFileName(layoutTitle, view.name);
        for (int n = 2; used.contains(name.toLower()); ++n)
            name = pictureFileName(layoutTitle, QStringLiteral("%1 (%2)").arg(view.name).arg(n));
        used.insert(name.toLower());
        const QImage img = renderer.render(spec, pictureSize(spec.region, scaleForSide(spec.region, maxSide)));
        QSaveFile f(dir.filePath(name));
        if (img.isNull() || !f.open(QIODevice::WriteOnly) || !img.save(&f, "PNG") || !f.commit()) {
            out.failed << name;
            continue;
        }
        out.files << name;
    }
    return out;
}

}  // namespace bld::ui::views
