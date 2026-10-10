#include "LDrawColors.h"

#include <QHash>

namespace bld::import {

namespace {

// { code, rgb, isTransparent }
struct Entry {
    int    code;
    QRgb   rgb;
    bool   transparent;
};

// Subset of LDraw's standard colors — covers every code used by
// mainstream parts authored in LDraw, Studio, and LDD exports. The
// metallic / pearl / chrome variants all share their RGB with a
// base color and are best rendered as the base color + a tint in
// a more sophisticated pipeline, so we just collapse them here.
constexpr Entry kColors[] = {
    {  0, qRgb( 27,  42,  52), false },  // Black
    {  1, qRgb(  0,  85, 191), false },  // Blue
    {  2, qRgb(  0, 124,  64), false },  // Green
    {  3, qRgb(  6, 125, 156), false },  // Teal
    {  4, qRgb(201,  26,   9), false },  // Red
    {  5, qRgb(211,  83, 156), false },  // Dark pink
    {  6, qRgb( 84,  51,  36), false },  // Brown
    {  7, qRgb(138, 146, 141), false },  // Light grey
    {  8, qRgb(100, 103,  97), false },  // Dark grey
    {  9, qRgb(180, 210, 227), false },  // Light blue
    { 10, qRgb(120, 252, 120), false },  // Bright green
    { 11, qRgb(  8, 153, 157), false },  // Turquoise
    { 12, qRgb(246, 108,  64), false },  // Salmon
    { 13, qRgb(253, 195, 175), false },  // Pink
    { 14, qRgb(245, 205,  47), false },  // Yellow
    { 15, qRgb(255, 255, 255), false },  // White
    { 16, qRgb(180, 180, 180), false },  // "Main color" — sentinel, we render as grey
    { 17, qRgb(197, 225, 204), false },  // Light green
    { 18, qRgb(253, 232, 158), false },  // Light yellow
    { 19, qRgb(215, 197, 153), false },  // Tan
    { 20, qRgb(193, 202, 223), false },  // Light violet
    { 22, qRgb(129,  68, 138), false },  // Purple
    { 23, qRgb( 13, 105, 171), false },  // Dark blue
    { 25, qRgb(214, 121,  35), false },  // Orange
    { 26, qRgb(146,  57, 120), false },  // Magenta
    { 27, qRgb(164, 189,  70), false },  // Lime
    { 28, qRgb(149, 138, 115), false },  // Dark tan
    { 29, qRgb(253, 217, 228), false },  // Bright pink
    { 30, qRgb(160, 110, 185), false },  // Medium lavender
    { 31, qRgb(205, 164, 222), false },  // Lavender

    // Transparent variants. Standard LDraw uses 3x-4x code ranges for
    // these; RGB is the paint color, and the transparency flag hints
    // downstream (we blend with alpha when rasterizing).
    { 32, qRgb(  0,  0,   0),  true  },  // Trans-black (glass)
    { 33, qRgb(  0, 32, 160),  true  },  // Trans-dark-blue
    { 34, qRgb( 35, 120,  65),  true  },  // Trans-green
    { 35, qRgb( 86, 230, 70),  true  },  // Trans-bright-green
    { 36, qRgb(201,  26,  9),  true  },  // Trans-red
    { 37, qRgb(100, 20, 100),  true  },  // Trans-purple
    { 38, qRgb(255, 128,   0),  true  },  // Trans-orange
    { 40, qRgb(99,  95,  82),  true  },  // Trans-black (smoky)
    { 41, qRgb(174, 239, 236),  true  },  // Trans-light-blue
    { 42, qRgb(192, 255,   0),  true  },  // Trans-neon-green
    { 43, qRgb(174, 255, 255),  true  },  // Trans-very-light-blue
    { 44, qRgb(192,  80, 192),  true  },  // Trans-violet
    { 45, qRgb(252, 151, 172),  true  },  // Trans-pink
    { 46, qRgb(240, 196,   0),  true  },  // Trans-yellow
    { 47, qRgb(252, 252, 252),  true  },  // Trans-clear
    { 54, qRgb(218, 176,  10),  true  },  // Trans-neon-yellow
    { 57, qRgb(247, 133,   0),  true  },  // Trans-neon-orange

    // Metallic (treated as their base color for rendering).
    { 70, qRgb(105,  64,  39), false },  // Reddish-brown
    { 71, qRgb(163, 162, 164), false },  // Light bluish grey
    { 72, qRgb( 99,  95,  98), false },  // Dark bluish grey
    { 73, qRgb(110, 153, 201), false },  // Medium blue
    { 74, qRgb(192, 255, 147), false },  // Medium green
    { 77, qRgb(254, 204, 204), false },  // Light pink
    { 78, qRgb(246, 215, 179), false },  // Flesh
    { 84, qRgb(160, 110,  60), false },  // Medium dark flesh
    { 85, qRgb( 68,  26, 145), false },  // Dark purple
    { 86, qRgb(123, 93,  65), false },  // Dark flesh
    { 89, qRgb( 28,  88, 167), false },  // Blue-violet
    { 92, qRgb(204, 142, 105), false },  // Flesh
    { 100, qRgb(238, 196, 182), false }, // Light salmon
    { 110, qRgb( 38,  51, 140), false }, // Violet

    // Common Studio-exported codes above the core palette.
    { 151, qRgb(204, 202, 200), false }, // Very light bluish grey
    { 176, qRgb(135,  84, 112), false }, // Magenta
    { 191, qRgb(248, 187,  61), false }, // Bright light orange
    { 212, qRgb(156, 209, 223), false }, // Bright light blue
    { 216, qRgb(105,  49,  39), false }, // Rust
    { 226, qRgb(255, 242, 171), false }, // Bright light yellow
    { 272, qRgb( 32,  58,  86), false }, // Dark blue
    { 288, qRgb( 39,  70,  44), false }, // Dark green
    { 308, qRgb( 53,  33,   0), false }, // Dark brown
    { 320, qRgb(114,  14,  14), false }, // Dark red
    { 321, qRgb( 70, 155, 195), false }, // Dark azure
    { 322, qRgb(104, 195, 226), false }, // Medium azure
    { 323, qRgb(189, 199, 180), false }, // Light aqua
    { 326, qRgb(226, 249, 154), false }, // Yellowish-green
    { 330, qRgb(119, 119,  78), false }, // Olive green
};

const QHash<int, const Entry*>& table() {
    static const QHash<int, const Entry*> t = []{
        QHash<int, const Entry*> m;
        for (const auto& e : kColors) m.insert(e.code, &e);
        return m;
    }();
    return t;
}

}  // namespace

QColor ldrawColor(int code) {
    const auto& t = table();
    auto it = t.constFind(code);
    if (it != t.constEnd()) {
        QColor c = QColor::fromRgb((*it)->rgb);
        if ((*it)->transparent) c.setAlpha(160);
        return c;
    }
    // Unknown code: return light grey so the user sees *something*
    // instead of pure magenta-error. The top-level import flow
    // logs the unknown code via qDebug once we have a logger.
    return QColor::fromRgb(qRgb(180, 180, 180));
}

bool ldrawColorIsTransparent(int code) {
    const auto& t = table();
    auto it = t.constFind(code);
    return it != t.constEnd() && (*it)->transparent;
}

}  // namespace bld::import
