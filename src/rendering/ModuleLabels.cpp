#include "ModuleLabels.h"

#include <QObject>

#include "SceneBuilder.h"

#include "../core/Module.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>

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

QString moduleHoverName(const QString& name, bool partlyHidden, bool truncated) {
    if (partlyHidden) return QObject::tr("%1 (partly hidden)").arg(name);
    return truncated ? name : QString();
}

double moduleNameStrokePx(double fontPx) { return std::max(2.0, fontPx / 12.0); }

double moduleLabelFontPx(double frameW, double frameH, double percent) {
    const double pct = std::clamp(percent, 5.0, 100.0);
    const double wanted =
        std::min(std::max(frameW, frameH) * (pct / 100.0), std::min(frameW, frameH) * kModuleNameShortShare);
    return std::round(std::clamp(wanted, 16.0, 400.0));
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

ModuleLook moduleLook(const core::Module& m, const QString& defaultHex) {
    ModuleLook look;
    const QColor fallbackFrame = hexColor(defaultHex, kModuleFrameAlpha);
    const QColor fallbackName = hexColor(defaultHex, kModuleNameAlpha);
    if (fallbackFrame.isValid()) look.frame = fallbackFrame;
    if (fallbackName.isValid()) look.nameFill = fallbackName;
    const QColor frame = hexColor(m.outlineColor, kModuleFrameAlpha);
    if (frame.isValid()) look.frame = frame;
    const QColor name = hexColor(m.nameColor, kModuleNameAlpha);
    if (name.isValid()) look.nameFill = name;
    look.showName = m.showName;
    return look;
}

quint32 moduleIdHash(const QString& id) {
    quint32 h = 0x811c9dc5u;
    for (const QChar c : id) {
        h ^= c.unicode();
        h *= 0x01000193u;
    }
    return h;
}

QHash<QString, QString> moduleColors(const std::vector<ModuleColorInput>& modules) {
    const int n = static_cast<int>(kModulePalette.size());
    const double d = kModuleNeighbourStuds;
    const auto near = [d](const QRectF& a, const QRectF& b) {
        return a.x() - d < b.right() && b.x() - d < a.right() && a.y() - d < b.bottom() && b.y() - d < a.bottom();
    };
    QHash<QString, int> index;
    QHash<QString, QString> out;
    for (const auto& m : modules) {
        QSet<int> taken;
        if (!m.box.isEmpty())
            for (const auto& o : modules) {
                if (&o == &m || o.box.isEmpty() || !index.contains(o.id)) continue;
                if (near(m.box, o.box)) taken.insert(index.value(o.id));
            }
        int k = static_cast<int>(moduleIdHash(m.id) % static_cast<quint32>(n));
        for (int t = 0; t < n && taken.contains(k); ++t) k = (k + 1) % n;
        index.insert(m.id, k);
        out.insert(m.id, kModulePalette[k]);
    }
    return out;
}

ModuleLabelLayout moduleLabelLayout(const QRectF& studs, const QString& name, double labelPercent,
                                    const NameWidthAt& widthAt, bool showName) {
    const double k = SceneBuilder::kPixelsPerStud;
    const QRectF part(studs.x() * k, studs.y() * k, studs.width() * k, studs.height() * k);
    return placeModuleNames({ ModuleNameInput{ QString(), name, studs, showName } }, { part }, labelPercent, widthAt).front();
}

namespace {

// The web's MODULE_NAME_SCORE (moduleLabels.ts).
constexpr double kScoreParts = 100, kScoreAnyPart = 20, kScoreNames = 100, kScoreAnyName = 50, kScoreFrames = 20,
                 kScoreInward = 4, kScoreInwardSolid = 1, kScoreShrink = 6, kScoreCut = 10, kScoreLevel = 3,
                 kScoreInside = 30;
constexpr int kLevels = 3;

double overlapArea(const QRectF& a, const QRectF& b) {
    const double w = std::min(a.x() + a.width(), b.x() + b.width()) - std::max(a.x(), b.x());
    const double h = std::min(a.y() + a.height(), b.y() + b.height()) - std::max(a.y(), b.y());
    return w > 0 && h > 0 ? w * h : 0.0;
}

// Part boxes bucketed on a grid, so each candidate only looks at parts near it.
class PartGrid {
public:
    explicit PartGrid(const std::vector<QRectF>& boxes) : boxes_(boxes) {
        for (int i = 0; i < static_cast<int>(boxes.size()); ++i) {
            const QRectF& b = boxes[i];
            for (long long cx = cell(b.x()); cx <= cell(b.x() + b.width()); ++cx)
                for (long long cy = cell(b.y()); cy <= cell(b.y() + b.height()); ++cy) cells_[{ cx, cy }].push_back(i);
        }
    }
    // The summed overlap of `q` with every part, in part order.
    double covered(const QRectF& q) const {
        std::set<int> seen;
        for (long long cx = cell(q.x()); cx <= cell(q.x() + q.width()); ++cx)
            for (long long cy = cell(q.y()); cy <= cell(q.y() + q.height()); ++cy) {
                const auto it = cells_.find({ cx, cy });
                if (it != cells_.end()) seen.insert(it->second.begin(), it->second.end());
            }
        double sum = 0;
        for (int i : seen) sum += overlapArea(q, boxes_[i]);
        return sum;
    }

private:
    static long long cell(double v) { return static_cast<long long>(std::floor(v / 512.0)); }
    const std::vector<QRectF>& boxes_;
    std::map<std::pair<long long, long long>, std::vector<int>> cells_;
};

struct Fit {
    double side = 0;
    ModuleNameFit fit;
    double w = 0;  // the widest line
    double h = 0;  // the lines
};

struct Candidate {
    QString slot;
    QString side;
    QPointF at;
    double rotation = 0;
    QRectF box;
    const Fit* fit = nullptr;
    double extra = 0;
};

}  // namespace

std::vector<ModuleLabelLayout> placeModuleNames(const std::vector<ModuleNameInput>& modules,
                                                const std::vector<QRectF>& partsPx, double labelPercent,
                                                const NameWidthAt& widthAt, const QHash<QString, QString>& keep) {
    const double k = SceneBuilder::kPixelsPerStud;
    std::vector<QRectF> frames;
    frames.reserve(modules.size());
    for (const auto& m : modules)
        frames.emplace_back(m.studs.x() * k - kPad, m.studs.y() * k - kPad, m.studs.width() * k + kPad * 2,
                            m.studs.height() * k + kPad * 2);
    // The layout's middle: the middle of all the modules together.
    double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
    for (const QRectF& f : frames) {
        x0 = std::min(x0, f.x());
        y0 = std::min(y0, f.y());
        x1 = std::max(x1, f.x() + f.width());
        y1 = std::max(y1, f.y() + f.height());
    }
    const QPointF mid((x0 + x1) / 2, (y0 + y1) / 2);
    const QPointF half(std::max((x1 - x0) / 2, 1.0), std::max((y1 - y0) / 2, 1.0));
    const PartGrid grid(partsPx);
    // A ring (a hole in the middle): nothing within a tenth of the layout's size of its middle.
    const double r = std::min(half.x(), half.y()) / 10;
    const bool ring = grid.covered(QRectF(mid.x() - r, mid.y() - r, 2 * r, 2 * r)) == 0;
    const double inward = ring ? kScoreInward : kScoreInwardSolid;
    std::vector<QRectF> names;
    std::vector<ModuleLabelLayout> out;
    out.reserve(modules.size());
    for (std::size_t mi = 0; mi < modules.size(); ++mi) {
        const ModuleNameInput& m = modules[mi];
        const QRectF frame = frames[mi];
        ModuleLabelLayout lay;
        lay.frame = frame;
        lay.bounds = frame;
        if (!m.showName) {
            out.push_back(lay);
            continue;
        }
        const double pw = m.studs.width() * k, ph = m.studs.height() * k;
        const bool portrait = ph > pw;
        const double f0 = moduleLabelFontPx(pw, ph, labelPercent);
        // Which way is out: from the layout's middle to the module's.
        const double dx = (frame.x() + frame.width() / 2 - mid.x()) / half.x();
        const double dy = (frame.y() + frame.height() / 2 - mid.y()) / half.y();
        const double len = std::hypot(dx, dy);
        const bool hasAway = len >= 0.2;
        const QPointF away = hasAway ? QPointF(dx / len, dy / len) : QPointF();
        const auto pref = [portrait](const QString& side) {
            if (side == QLatin1String("inside")) return 0.0;
            static const QStringList landscape{ QStringLiteral("top"), QStringLiteral("bottom"), QStringLiteral("left"), QStringLiteral("right") };
            static const QStringList tall{ QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("top"), QStringLiteral("bottom") };
            return (portrait ? tall : landscape).indexOf(side) * 0.5;
        };
        // Never longer than the module's edge (its parts, not the frame round them).
        const auto fitTo = [&](double side) {
            Fit f;
            f.side = side;
            f.fit = fitModuleName(m.name, widthAt, f0, side - 2 * kPad);
            for (const QString& l : f.fit.lines) f.w = std::max(f.w, widthAt(l, f.fit.fontPx));
            f.h = f.fit.lines.size() * f.fit.fontPx * kModuleNameLineHeight;
            return f;
        };
        const Fit across = fitTo(frame.width());
        const Fit along = fitTo(frame.height());
        std::vector<Candidate> cands;
        const double xMid = frame.x() + (frame.width() - across.w) / 2;
        const double yMid = frame.y() + (frame.height() - along.w) / 2;
        for (int level = 0; level < kLevels; ++level) {
            const double top = frame.y() - kGap - across.h - level * (across.h + kGap);
            const double bottom = frame.y() + frame.height() + kGap + level * (across.h + kGap);
            const double left = frame.x() - kGap - along.h - level * (along.h + kGap);
            const double right = frame.x() + frame.width() + kGap + along.h + level * (along.h + kGap);
            const double extra = level * kScoreLevel;
            const QString lv = QString::number(level);
            cands.push_back({ QStringLiteral("top:") + lv, QStringLiteral("top"), { frame.x(), top }, 0, QRectF(xMid, top, across.w, across.h), &across, extra });
            cands.push_back({ QStringLiteral("bottom:") + lv, QStringLiteral("bottom"), { frame.x(), bottom }, 0, QRectF(xMid, bottom, across.w, across.h), &across, extra });
            cands.push_back({ QStringLiteral("left:") + lv, QStringLiteral("left"), { left, frame.y() + frame.height() }, -90, QRectF(left, yMid, along.h, along.w), &along, extra });
            cands.push_back({ QStringLiteral("right:") + lv, QStringLiteral("right"), { right, frame.y() }, 90, QRectF(right - along.h, yMid, along.h, along.w), &along, extra });
        }
        // Inside, as a last resort: the emptiest of three places across the module.
        const Fit& inner = portrait ? along : across;
        for (int i = 0; i < 3; ++i) {
            if (portrait) {
                const double space = frame.width() - 2 * kPad - inner.h;
                const double x = frame.x() + kPad + (space > 0 ? (i * space) / 2 : space / 2);
                cands.push_back({ QStringLiteral("inside:%1").arg(i), QStringLiteral("inside"), { x, frame.y() + frame.height() }, -90,
                                  QRectF(x, frame.y() + (frame.height() - inner.w) / 2, inner.h, inner.w), &inner, kScoreInside });
            } else {
                const double space = frame.height() - 2 * kPad - inner.h;
                const double y = frame.y() + kPad + (space > 0 ? (i * space) / 2 : space / 2);
                cands.push_back({ QStringLiteral("inside:%1").arg(i), QStringLiteral("inside"), { frame.x(), y }, 0,
                                  QRectF(frame.x() + (frame.width() - inner.w) / 2, y, inner.w, inner.h), &inner, kScoreInside });
            }
        }
        const Candidate* best = &cands.front();
        double bestScore = INFINITY;
        const QString kept = keep.value(m.id);
        const bool keeping = !kept.isEmpty() && std::any_of(cands.begin(), cands.end(), [&](const Candidate& c) { return c.slot == kept; });
        for (const Candidate& c : cands) {
            if (keeping && c.slot != kept) continue;
            const double area = std::max(c.box.width() * c.box.height(), 1.0);
            double score = c.extra + pref(c.side);
            const double onParts = grid.covered(c.box);
            score += kScoreParts * onParts / area + (onParts > 0 ? kScoreAnyPart : 0);
            double onNames = 0;
            for (const QRectF& n : names) onNames += overlapArea(c.box, n);
            score += kScoreNames * onNames / area + (onNames > 0 ? kScoreAnyName : 0);
            for (std::size_t fi = 0; fi < frames.size(); ++fi)
                if (fi != mi) score += kScoreFrames * overlapArea(c.box, frames[fi]) / area;
            if (c.side != QLatin1String("inside") && hasAway) {
                const QPointF n = c.side == QLatin1String("top")      ? QPointF(0, -1)
                                  : c.side == QLatin1String("bottom") ? QPointF(0, 1)
                                  : c.side == QLatin1String("left")   ? QPointF(-1, 0)
                                                                      : QPointF(1, 0);
                score += inward * (1 - (n.x() * away.x() + n.y() * away.y()));
            }
            score += kScoreShrink * (1 - c.fit->fit.fontPx / f0) + (c.fit->fit.truncated ? kScoreCut : 0);
            if (score < bestScore - 1e-9) {
                bestScore = score;
                best = &c;
            }
        }
        names.push_back(best->box);
        const Fit& f = *best->fit;
        lay.hasName = true;
        lay.textPos = best->at;
        lay.rotation = best->rotation;
        lay.width = f.side;
        lay.height = f.h;
        lay.fontPx = f.fit.fontPx;
        lay.lines = f.fit.lines;
        lay.truncated = f.fit.truncated;
        lay.side = best->side;
        lay.slot = best->slot;
        // The name's whole box along its side (where it may be drawn).
        const QRectF nameBox = best->rotation == -90 ? QRectF(best->at.x(), best->at.y() - f.side, f.h, f.side)
                               : best->rotation == 90 ? QRectF(best->at.x() - f.h, best->at.y(), f.h, f.side)
                                                      : QRectF(best->at, QSizeF(f.side, f.h));
        const double bx0 = std::min(frame.x(), nameBox.x());
        const double by0 = std::min(frame.y(), nameBox.y());
        const double bx1 = std::max(frame.x() + frame.width(), nameBox.x() + nameBox.width());
        const double by1 = std::max(frame.y() + frame.height(), nameBox.y() + nameBox.height());
        lay.bounds = QRectF(bx0, by0, bx1 - bx0, by1 - by0);
        out.push_back(lay);
    }
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
