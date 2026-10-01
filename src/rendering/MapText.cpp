#include "MapText.h"

#include "SceneBuilder.h"
#include "core/TextCell.h"

#include <QFontDatabase>
#include <QFontMetricsF>

#include <algorithm>
#include <cmath>

namespace bld::rendering {

QString mapFontFamily() {
    static const QString family = [] {
        QString name;
        for (const char* face : { "Regular", "Bold", "Italic", "BoldItalic" }) {
            const int id = QFontDatabase::addApplicationFont(
                QStringLiteral(":/bld/fonts/LiberationSans-%1.ttf").arg(QLatin1String(face)));
            const QStringList families = QFontDatabase::applicationFontFamilies(id);
            if (name.isEmpty() && !families.isEmpty()) name = families.first();
        }
        return name.isEmpty() ? QStringLiteral("Liberation Sans") : name;
    }();
    return family;
}

QFont mapFont(const QString& style, double px) {
    QFont f(mapFontFamily());
    f.setPixelSize(std::max(1, static_cast<int>(std::lround(px))));
    f.setBold(style.contains(QStringLiteral("Bold"), Qt::CaseInsensitive));
    f.setItalic(style.contains(QStringLiteral("Italic"), Qt::CaseInsensitive));
    f.setKerning(true);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

LineWidthAt mapLineWidth(const QString& style) {
    return [style](const QString& line, double fontPx) {
        return QFontMetricsF(mapFont(style, fontPx)).horizontalAdvance(line);
    };
}

TextCellLayout textCellLayout(const core::TextCell& cell, const LineWidthAt& widthAt) {
    constexpr double kProbe = 100;
    const double k = SceneBuilder::kPixelsPerStud;
    const QStringList lines = cell.text.split(QLatin1Char('\n'));
    const auto widest = [&](double px) {
        double w = 0;
        for (const QString& l : lines) w = std::max(w, widthAt(l, px));
        return w;
    };
    const auto n = static_cast<double>(lines.size());
    TextCellLayout out;
    out.rotation = cell.orientation;
    out.centre = QPointF(cell.displayArea.center().x() * k, cell.displayArea.center().y() * k);
    const double orient = std::fmod(std::fmod(cell.orientation, 360.0) + 360.0, 360.0);
    const bool rot90 = std::abs(orient - 90) < 1 || std::abs(orient - 270) < 1;
    const double boxW = (rot90 ? cell.displayArea.height() : cell.displayArea.width()) * k;
    const double boxH = (rot90 ? cell.displayArea.width() : cell.displayArea.height()) * k;
    const double probeW = widest(kProbe), probeH = n * kMapLineHeight * kProbe;
    out.fontPx = kProbe;
    if (probeW > 0 && probeH > 0)
        out.fontPx = std::max(1.0, std::floor(kProbe * std::min(boxW / probeW, boxH / probeH)));
    out.width = widest(out.fontPx);
    out.height = n * kMapLineHeight * out.fontPx;
    for (int i = 0; i < lines.size(); ++i) {
        const double lw = widthAt(lines[i], out.fontPx);
        double x = 0;
        if (cell.alignment == core::TextAlignment::Center) x = (out.width - lw) / 2;
        else if (cell.alignment == core::TextAlignment::Far) x = out.width - lw;
        out.lines.push_back({ lines[i], x, i * kMapLineHeight * out.fontPx });
    }
    return out;
}

QPainterPath textPath(const QFont& font, const std::vector<TextCellLayout::Line>& lines, double lineHeight) {
    const QFontMetricsF fm(font);
    const double linePx = lineHeight * font.pixelSize();
    QPainterPath path;
    for (const auto& l : lines)
        path.addText(l.x, l.y + linePx / 2 + (fm.ascent() - fm.descent()) / 2, font, l.text);
    return path;
}

}  // namespace bld::rendering
