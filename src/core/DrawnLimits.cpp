#include "DrawnLimits.h"

#include "LayerArea.h"
#include "LayerGrid.h"
#include "LayerRuler.h"
#include "Map.h"

#include <QPointF>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>
#include <type_traits>

namespace bld::core {

namespace {

template <typename T> void keep(T& v, const limits::Range<T>& r) {
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(v)) {
            v = r.fallback;
            return;
        }
    }
    v = std::clamp(v, r.min, r.max);
}

void keep(QPointF& p, const limits::Range<double>& r) {
    double x = p.x(), y = p.y();
    keep(x, r);
    keep(y, r);
    p = QPointF(x, y);
}

void keepFloatCoordinate(float& v) {
    double d = v;
    keep(d, limits::kRulerCoordinate);
    v = static_cast<float>(d);
}

void keep(CellIndexType& t, CellIndexType fallback) {
    if (t != CellIndexType::Letters && t != CellIndexType::Numbers) t = fallback;
}

void keepRuler(RulerItemBase& r) {
    keep(r.lineThickness, limits::kLineThickness);
    keep(r.guidelineThickness, limits::kGuidelineThickness);
    keep(r.measureFont.sizePt, limits::kFontSize);
    if (r.unit < limits::kRulerUnit.min || r.unit > limits::kRulerUnit.max)
        r.unit = limits::kRulerUnit.fallback;
    auto& dash = r.guidelineDashPattern;
    const bool inRange =
        std::ssize(dash) <= limits::kMaxDashEntries && std::all_of(dash.cbegin(), dash.cend(), [](float d) {
            return std::isfinite(d) && d >= 0.0f && d <= limits::kMaxDash;
        });
    if (!inRange || (!dash.empty() && std::accumulate(dash.cbegin(), dash.cend(), 0.0) < limits::kMinDashTotal))
        dash.clear();
}

} // namespace

void keepDrawnSettingsInRange(Map& map) {
    for (auto& layer : map.layers()) {
        if (!layer) continue;
        keep(layer->transparency, limits::kTransparency);
        keep(layer->hull.thickness, limits::kHullThickness);
        switch (layer->kind()) {
        case LayerKind::Grid: {
            auto& g = static_cast<LayerGrid&>(*layer);
            keep(g.gridSizeInStud, limits::kGridSizeInStud);
            keep(g.subDivisionNumber, limits::kSubDivisionNumber);
            keep(g.gridThickness, limits::kGridThickness);
            keep(g.subGridThickness, limits::kSubGridThickness);
            keep(g.cellIndexFont.sizePt, limits::kFontSize);
            int x = g.cellIndexCorner.x(), y = g.cellIndexCorner.y();
            keep(x, limits::kCellIndexCorner);
            keep(y, limits::kCellIndexCorner);
            g.cellIndexCorner = QPoint(x, y);
            keep(g.cellIndexColumnType, CellIndexType::Letters);
            keep(g.cellIndexRowType, CellIndexType::Numbers);
            break;
        }
        case LayerKind::Area:
            keep(static_cast<LayerArea&>(*layer).areaCellSizeInStud, limits::kAreaCellSizeInStud);
            break;
        case LayerKind::Ruler:
            for (auto& any : static_cast<LayerRuler&>(*layer).rulers) {
                keepRuler(any.linear);
                keepRuler(any.circular);
                keep(any.linear.point1, limits::kRulerCoordinate);
                keep(any.linear.point2, limits::kRulerCoordinate);
                keepFloatCoordinate(any.linear.offsetDistance);
                keep(any.circular.center, limits::kRulerCoordinate);
                keep(any.circular.radius, limits::kRulerRadius);
            }
            break;
        default: break;
        }
    }
}

std::vector<float> dashPatternInRange(const std::vector<float>& typed) {
    std::vector<float> out;
    for (const float d : typed) {
        if (std::ssize(out) >= limits::kMaxDashEntries) break;
        if (std::isfinite(d)) out.push_back(std::clamp(d, 0.0f, limits::kMaxDash));
    }
    if (std::accumulate(out.cbegin(), out.cend(), 0.0) < limits::kMinDashTotal) out.clear();
    return out;
}

} // namespace bld::core
