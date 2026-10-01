#include "ModuleLabels.h"

#include "SceneBuilder.h"

#include <algorithm>
#include <cmath>

namespace bld::rendering {

namespace {
constexpr double kPad = 4.0;
constexpr double kGap = 16.0;

// The web's fitModuleLabel: a name wider than its side shrinks (to 60% of
// its size, never under 16 px), then grows past the frame, centred.
struct Fit { double fontPx; double width; };
Fit fitModuleLabel(const TextWidthAt& widthAt, double fontPx, double side) {
    const double natural = widthAt(fontPx);
    if (natural <= side) return { fontPx, side };
    const double smallest = std::max(16.0, std::round(fontPx * 0.6));
    const double fitted = std::max(smallest, std::floor((fontPx * side) / natural));
    return { fitted, std::max(side, std::ceil(widthAt(fitted)) + 2) };
}
}  // namespace

double moduleNameStrokePx(double fontPx) { return std::max(2.0, fontPx / 12.0); }

double moduleLabelFontPx(double frameW, double frameH, double percent) {
    const double pct = std::clamp(percent, 5.0, 100.0);
    return std::round(std::clamp(std::max(frameW, frameH) * (pct / 100.0), 16.0, 400.0));
}

ModuleLabelLayout moduleLabelLayout(const QRectF& studs, const QString& name, double labelPercent,
                                    const TextWidthAt& widthAt) {
    const double k = SceneBuilder::kPixelsPerStud;
    const double px = studs.x() * k, py = studs.y() * k, pw = studs.width() * k, ph = studs.height() * k;
    const bool portrait = ph > pw;
    const double side = portrait ? ph + kPad * 2 : pw + kPad * 2;
    const Fit fit = fitModuleLabel(widthAt, moduleLabelFontPx(pw, ph, labelPercent), side);
    const double over = (fit.width - side) / 2;
    ModuleLabelLayout out;
    out.frame = QRectF(px - kPad, py - kPad, pw + kPad * 2, ph + kPad * 2);
    out.fontPx = fit.fontPx;
    out.width = fit.width;
    QRectF nameBox;
    if (portrait) {
        out.textPos = QPointF(px - kPad - kGap - fit.fontPx, py + ph + kPad + over);
        out.rotation = -90;
        nameBox = QRectF(out.textPos.x(), out.textPos.y() - fit.width, fit.fontPx, fit.width);
    } else {
        out.textPos = QPointF(px - kPad - over, py - kPad - kGap - fit.fontPx);
        nameBox = QRectF(out.textPos, QSizeF(fit.width, fit.fontPx));
    }
    out.bounds = out.frame.united(nameBox);
    return out;
}

}  // namespace bld::rendering
