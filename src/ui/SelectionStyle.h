#pragma once

// How a selection looks, the same as the web (render/selectionStyle.ts):
// a part gets a black and gold double outline 1 px outside its picture
// with a see-through gold fill (green while a connection snap is live);
// text and labels glow gold; a ruler gets a see-through gold band along
// its line and round gold endpoint handles. fixtures/render-parity/
// selection.json holds the numbers both apps are tested against.

#include <QColor>

namespace bld::ui::selection {

inline constexpr double kPartPadPx = 1.0;
inline const QColor kPartOuter(0, 0, 0, 230);         // rgba(0,0,0,0.9)
inline constexpr double kPartOuterWidth = 5.0;        // screen px
inline const QColor kPartTint(255, 215, 0);           // #FFD700
inline constexpr double kPartInnerWidth = 2.5;        // screen px
inline constexpr int kPartFillAlpha = 0x4D;
inline const QColor kSnapStroke(80, 255, 120);
inline const QColor kSnapFill(80, 255, 120, 90);      // rgba(80,255,120,0.353)
inline const QColor kTextGlow(255, 204, 0);           // #ffcc00
inline constexpr double kTextGlowBlur = 8.0;
inline const QColor kRulerHalo(255, 215, 0, 128);     // rgba(255,215,0,0.5)
inline constexpr double kRulerHaloExtra = 4.0;        // scene px over the line
inline constexpr double kRulerHaloMin = 6.0;
inline constexpr double kHandleRadius = 6.0;          // scene px
inline const QColor kHandleFill(255, 215, 0);
inline const QColor kHandleStroke(20, 20, 20);
inline constexpr double kHandleStrokeWidth = 1.5;     // screen px

// The band's width for a ruler line `thickness` scene px wide.
inline double rulerHaloWidth(double thickness) {
    return thickness + kRulerHaloExtra > kRulerHaloMin ? thickness + kRulerHaloExtra : kRulerHaloMin;
}

// Connection-snap marks while dragging, in screen px at any zoom: a green
// ring (with a light halo, readable on dark and light backgrounds) on the
// target connection, an amber dot on the moving connection that joins.
// Same as the web (render/selectionStyle.ts SNAP_MARKS).
namespace snapmarks {
inline const QColor kRing(22, 163, 74);
inline constexpr double kRingRadius = 9.0;
inline constexpr double kRingWidth = 2.5;
inline const QColor kRingFill(34, 197, 94, 51);       // rgba(34,197,94,0.2)
inline const QColor kHalo(255, 255, 255, 204);        // rgba(255,255,255,0.8)
inline constexpr double kHaloWidth = 1.5;
inline const QColor kDot(245, 158, 11);
inline constexpr double kDotRadius = 4.0;
inline constexpr double kDotHaloWidth = 1.5;
}  // namespace snapmarks

}  // namespace bld::ui::selection
