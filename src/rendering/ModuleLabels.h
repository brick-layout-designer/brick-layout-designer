#pragma once

// Module frames and names, drawn as the web draws them
// (apps/web/src/editor/render/moduleLabels.ts + ModuleOverlay.tsx): a
// dashed light-blue frame 4 px outside the module's pieces, and the name
// in bold light blue with a dark outline, 16 px outside the frame, centred
// above it (or turned along the left side of a tall module), shrunk so the
// whole name shows. fixtures/render-parity/modules.json holds the numbers
// both apps are tested against.

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <functional>

namespace bld::rendering {

inline const QColor kModuleFrameColor(100, 180, 255, 204);  // rgba(100,180,255,0.8)
inline const QColor kModuleNameFill(100, 180, 255, 230);    // rgba(100,180,255,0.9)
inline const QColor kModuleNameStroke(0, 0, 0, 153);        // rgba(0,0,0,0.6)
// The frame's dash and gap, in screen px.
inline constexpr double kModuleFrameDash[2] = { 6.0, 4.0 };
// QGraphicsItem data role naming a module drawing: "frame" or "name".
inline constexpr int kModuleAnnotationRole = 3;

// A bold name's width in px at a font size.
using TextWidthAt = std::function<double(double fontPx)>;

// The name's outline width: fontPx / 12, at least 2 px.
double moduleNameStrokePx(double fontPx);
// `percent`% of the frame's long axis, clamped to 16..400 px.
double moduleLabelFontPx(double frameW, double frameH, double percent);

struct ModuleLabelLayout {
    QRectF frame;     // scene px
    QPointF textPos;  // the name's anchor; a portrait name turns -90° about it
    double rotation = 0;
    double width = 0;  // the name's box, along its reading direction
    double fontPx = 0;
    QRectF bounds;  // frame and name together
};

// Where one module's frame and name go, from its pieces' bounds in studs.
ModuleLabelLayout moduleLabelLayout(const QRectF& studs, const QString& name, double labelPercent,
                                    const TextWidthAt& widthAt);

}  // namespace bld::rendering
