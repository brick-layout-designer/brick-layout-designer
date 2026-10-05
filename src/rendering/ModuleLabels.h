#pragma once

// Module frames and names, drawn as the web draws them
// (apps/web/src/editor/render/moduleLabels.ts + ModuleOverlay.tsx): a
// dashed frame 4 px outside the module's pieces, and the name in bold with
// a dark outline, 16 px outside the frame, centred above it (or turned
// along the left side of a tall module, reading bottom to top). A name is
// never longer than the side it sits on: it wraps to two lines, then
// shrinks, and only as a last resort is cut short with an ellipsis (the
// whole name shows on hover or select). Each module can have its own
// outline and name colours and hide its name.
// fixtures/render-parity/modules.json holds the numbers both apps are
// tested against.

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <functional>

namespace bld::core { struct Module; }

namespace bld::rendering {

inline const QColor kModuleFrameColor(100, 180, 255, 204);  // rgba(100,180,255,0.8)
inline const QColor kModuleNameFill(100, 180, 255, 230);    // rgba(100,180,255,0.9)
inline const QColor kModuleNameStroke(0, 0, 0, 153);        // rgba(0,0,0,0.6)
// A chosen colour is drawn at the default look's opacity.
inline constexpr double kModuleFrameAlpha = 0.8;
inline constexpr double kModuleNameAlpha = 0.9;
// The pill behind a shortened name's whole name: rgba(0,0,0,0.75).
inline const QColor kModuleFullNameBackground(0, 0, 0, 191);
// The smallest a name gets before it is cut short, in scene px (2 studs).
inline constexpr double kModuleNameMinPx = 16.0;
// Lines of a two-line name are one font size apart.
inline constexpr double kModuleNameLineHeight = 1.0;
// The frame's dash and gap, in screen px.
inline constexpr double kModuleFrameDash[2] = { 6.0, 4.0 };
// QGraphicsItem data role naming a module drawing: "frame" or "name".
inline constexpr int kModuleAnnotationRole = 3;
// QGraphicsItem data role holding the module id on its frame and name.
inline constexpr int kModuleIdRole = 4;

// A bold line's width in px at a font size.
using NameWidthAt = std::function<double(const QString& text, double fontPx)>;

// The name's outline width: fontPx / 12, at least 2 px.
double moduleNameStrokePx(double fontPx);
// `percent`% of the frame's long axis, clamped to 16..400 px.
double moduleLabelFontPx(double frameW, double frameH, double percent);

// A name fitted to its side: one or two lines, never wider than the side.
struct ModuleNameFit {
    double fontPx = 0;
    QStringList lines;
    bool truncated = false;  // cut short with an ellipsis
};
// The web's fitModuleName, step for step: one line; else two lines
// (broken between words, as evenly as it goes); else smaller (one line or
// two, whichever stays bigger) down to kModuleNameMinPx; else at that size
// as many words as fit on the first line and the rest, cut short, on the
// second.
ModuleNameFit fitModuleName(const QString& text, const NameWidthAt& widthAt, double fontPx, double side);

// How one placed module is drawn: its colours (the default look unless
// chosen) and whether its name shows.
struct ModuleLook {
    QColor frame = kModuleFrameColor;
    QColor nameFill = kModuleNameFill;
    bool showName = true;
};
ModuleLook moduleLook(const core::Module& m);

struct ModuleLabelLayout {
    QRectF frame;     // scene px
    bool hasName = false;  // false when the module's name is hidden
    QPointF textPos;  // the name's anchor; a portrait name turns -90° about it
    double rotation = 0;
    double width = 0;   // the name's box, along the side it sits on
    double height = 0;  // the name's box, across it (its lines)
    double fontPx = 0;
    QStringList lines;
    bool truncated = false;
    QRectF bounds;  // frame and name together
};

// Where one module's frame and name go, from its pieces' bounds in studs.
ModuleLabelLayout moduleLabelLayout(const QRectF& studs, const QString& name, double labelPercent,
                                    const NameWidthAt& widthAt, bool showName = true);

// The pill with the whole name over a shortened one, in the name's own
// turned frame (x along the side, y across it): one line at the name's
// size, centred on the name's box, with a quarter-font margin.
struct ModuleFullNamePill {
    QRectF box;
    QPointF textTopLeft;
};
ModuleFullNamePill moduleFullNamePill(const ModuleLabelLayout& at, const QString& name, const NameWidthAt& widthAt);

}  // namespace bld::rendering
