#include "ModuleLabels.h"

#include "SceneBuilder.h"

#include "../core/Module.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <optional>

namespace bld::rendering {

namespace {
constexpr double kPad = 4.0;
constexpr double kGap = 16.0;
const QString kEllipsis = QStringLiteral("…");

struct Split { QStringList lines; double widest; };

// The two-line break that keeps the wider line narrowest (the first such
// break on a tie); none for one word.
std::optional<Split> balancedSplit(const QStringList& words, const std::function<double(const QString&)>& width) {
    std::optional<Split> best;
    for (qsizetype i = 1; i < words.size(); ++i) {
        const QString a = words.mid(0, i).join(QLatin1Char(' '));
        const QString b = words.mid(i).join(QLatin1Char(' '));
        const double widest = std::max(width(a), width(b));
        if (!best || widest < best->widest) best = Split{ { a, b }, widest };
    }
    return best;
}

// `s` shortened from the end until it fits with an ellipsis after it.
QString ellipsize(QString s, const std::function<bool(const QString&)>& fits) {
    const auto trimmedEnd = [](QString t) {
        while (!t.isEmpty() && t.back().isSpace()) t.chop(1);
        return t;
    };
    while (!s.isEmpty() && !fits(trimmedEnd(s) + kEllipsis)) s.chop(1);
    return trimmedEnd(s) + kEllipsis;
}

// `#rrggbb` (or `#rgb`) at an opacity; invalid for anything else.
QColor hexColor(const QString& hex, double alpha) {
    static const QRegularExpression six(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    static const QRegularExpression three(QStringLiteral("^#[0-9a-fA-F]{3}$"));
    if (!six.match(hex).hasMatch() && !three.match(hex).hasMatch()) return {};
    QColor c(hex);
    c.setAlphaF(static_cast<float>(alpha));
    return c;
}
}  // namespace

double moduleNameStrokePx(double fontPx) { return std::max(2.0, fontPx / 12.0); }

double moduleLabelFontPx(double frameW, double frameH, double percent) {
    const double pct = std::clamp(percent, 5.0, 100.0);
    return std::round(std::clamp(std::max(frameW, frameH) * (pct / 100.0), 16.0, 400.0));
}

ModuleNameFit fitModuleName(const QString& text, const NameWidthAt& widthAt, double fontPx, double side) {
    const auto fits = [&](const QStringList& lines, double f) {
        return std::all_of(lines.begin(), lines.end(), [&](const QString& l) { return widthAt(l, f) <= side; });
    };
    if (widthAt(text, fontPx) <= side) return { fontPx, { text }, false };

    const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const auto split = balancedSplit(words, [&](const QString& s) { return widthAt(s, fontPx); });
    if (split && split->widest <= side) return { fontPx, split->lines, false };

    // Shrink: the bigger of the one-line and two-line sizes.
    const double one = std::floor((fontPx * side) / widthAt(text, fontPx));
    const double two = split ? std::floor((fontPx * side) / split->widest) : 0.0;
    const QStringList lines = split && two > one ? split->lines : QStringList{ text };
    int f = static_cast<int>(std::min(fontPx, std::max(one, two)));
    if (f >= kModuleNameMinPx) {
        while (f > kModuleNameMinPx && !fits(lines, f)) --f;
        if (fits(lines, f)) return { static_cast<double>(f), lines, false };
    }

    // Last resort: the smallest size, cut short.
    const double small = kModuleNameMinPx;
    const auto cut = [&](const QString& s) {
        return ellipsize(s, [&](const QString& t) { return widthAt(t, small) <= side; });
    };
    qsizetype k = 0;
    while (k < words.size() && widthAt(words.mid(0, k + 1).join(QLatin1Char(' ')), small) <= side) ++k;
    bool truncated = false;
    QString first;
    if (k == 0) {
        first = cut(words.isEmpty() ? text : words.first());
        truncated = true;
        k = 1;
    } else {
        first = words.mid(0, k).join(QLatin1Char(' '));
    }
    const QString rest = words.mid(k).join(QLatin1Char(' '));
    if (rest.isEmpty()) return { small, { first }, truncated };
    if (widthAt(rest, small) <= side) return { small, { first, rest }, truncated };
    return { small, { first, cut(rest) }, true };
}

ModuleLook moduleLook(const core::Module& m) {
    ModuleLook look;
    const QColor frame = hexColor(m.outlineColor, kModuleFrameAlpha);
    if (frame.isValid()) look.frame = frame;
    const QColor name = hexColor(m.nameColor, kModuleNameAlpha);
    if (name.isValid()) look.nameFill = name;
    look.showName = m.showName;
    return look;
}

ModuleLabelLayout moduleLabelLayout(const QRectF& studs, const QString& name, double labelPercent,
                                    const NameWidthAt& widthAt, bool showName) {
    const double k = SceneBuilder::kPixelsPerStud;
    const double px = studs.x() * k, py = studs.y() * k, pw = studs.width() * k, ph = studs.height() * k;
    ModuleLabelLayout out;
    out.frame = QRectF(px - kPad, py - kPad, pw + kPad * 2, ph + kPad * 2);
    out.bounds = out.frame;
    if (!showName) return out;
    const bool portrait = ph > pw;
    // No wider than the side the name sits on.
    const double side = portrait ? out.frame.height() : out.frame.width();
    const ModuleNameFit fit = fitModuleName(name, widthAt, moduleLabelFontPx(pw, ph, labelPercent), side);
    out.hasName = true;
    out.fontPx = fit.fontPx;
    out.lines = fit.lines;
    out.truncated = fit.truncated;
    out.width = side;
    out.height = fit.lines.size() * fit.fontPx * kModuleNameLineHeight;
    QRectF nameBox;
    if (portrait) {
        out.textPos = QPointF(out.frame.x() - kGap - out.height, out.frame.bottom());
        out.rotation = -90;
        nameBox = QRectF(out.textPos.x(), out.textPos.y() - side, out.height, side);
    } else {
        out.textPos = QPointF(out.frame.x(), out.frame.y() - kGap - out.height);
        nameBox = QRectF(out.textPos, QSizeF(side, out.height));
    }
    out.bounds = out.frame.united(nameBox);
    return out;
}

ModuleFullNamePill moduleFullNamePill(const ModuleLabelLayout& at, const QString& name, const NameWidthAt& widthAt) {
    const double pad = std::round(at.fontPx / 4.0);
    const double w = std::ceil(widthAt(name, at.fontPx)) + pad * 2;
    const double h = at.fontPx + pad * 2;
    const QRectF box((at.width - w) / 2.0, (at.height - h) / 2.0, w, h);
    return { box, box.topLeft() + QPointF(pad, pad) };
}

}  // namespace bld::rendering
