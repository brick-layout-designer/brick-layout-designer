#pragma once

// The map's text, drawn the same way as the web draws it
// (apps/web/src/editor/render/mapText.ts): one bundled font (Liberation
// Sans, metric-compatible with Arial, so with the sizes BlueBrick's files
// store) whatever family a file names, so the text looks the same on every
// machine; lines 2355/2048 em apart (GDI+'s Arial line spacing, as
// BlueBrick draws them); each line's baseline where Konva puts it.
// fixtures/render-parity/text.json holds the numbers both apps are tested
// against.

#include <QFont>
#include <QPainterPath>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

namespace bld::core { struct TextCell; }

namespace bld::rendering {

// Line spacing in em (the font's ascent + descent + line gap).
inline constexpr double kMapLineHeight = 2355.0 / 2048.0;

// The bundled family (registered on first use).
QString mapFontFamily();
// The bundled font at `px` pixels, bold / italic when `style` says so
// ("Bold", "Italic", "Bold, Italic").
QFont mapFont(const QString& style, double px);

// A line's width in px at a font size.
using LineWidthAt = std::function<double(const QString& line, double fontPx)>;
// The bundled font's widths, measured.
LineWidthAt mapLineWidth(const QString& style);

struct TextCellLayout {
    double fontPx = 0;
    double width = 0;   // the text's box, before turning
    double height = 0;
    QPointF centre;     // scene px; the box turns `rotation`° about it
    double rotation = 0;
    struct Line { QString text; double x; double y; };  // each line's top-left in the box
    std::vector<Line> lines;
};

// A text cell's text, fitted into its display area (the bigger font that
// fits the area's short and long side), centred on it, lines aligned
// Near / Center / Far.
TextCellLayout textCellLayout(const core::TextCell& cell, const LineWidthAt& widthAt);

// `lines` as one outline, each line's top-left at (x, y) and lines
// `lineHeight` em apart, baselines where Konva puts them.
QPainterPath textPath(const QFont& font, const std::vector<TextCellLayout::Line>& lines, double lineHeight);

}  // namespace bld::rendering
