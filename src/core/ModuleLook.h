#pragma once

// A placed module's own look: whether its name shows, its outline and name
// colors, and the "Same color" link that keeps the two in step. Pure
// functions over a Module; edit::UpdateModuleCommand writes the result.
// The web's apps/web/src/editor/moduleLook.ts does the same.

#include "Module.h"

#include <QString>

namespace bld::core {

enum class ModuleColorPart { Outline, Name };

// The default look's color, as a color picker shows it (the light blue
// rgba(100,180,255) without its opacity).
inline const QString kModuleDefaultColor = QStringLiteral("#64b4ff");

inline bool colorsLinked(const Module& m) { return m.sameColor; }

// The chosen color, else `fallback` (the module's own default color, rendering::moduleColors).
inline QString moduleColor(const Module& m, ModuleColorPart part, const QString& fallback = kModuleDefaultColor) {
    const QString& c = part == ModuleColorPart::Outline ? m.outlineColor : m.nameColor;
    return c.isEmpty() ? fallback.toLower() : c;
}

inline bool hasCustomColors(const Module& m) {
    return !m.outlineColor.isEmpty() || !m.nameColor.isEmpty() || !m.sameColor;
}

// Sets one part's color; with "Same color" on, the other part follows.
inline Module withColor(Module m, ModuleColorPart part, const QString& hex) {
    const QString c = hex.toLower();
    if (m.sameColor) {
        m.outlineColor = c;
        m.nameColor = c;
    } else if (part == ModuleColorPart::Outline) {
        m.outlineColor = c;
    } else {
        m.nameColor = c;
    }
    return m;
}

// Turns "Same color" on or off. Turning it on gives the name the
// outline's color (or both the default, if the outline has none).
inline Module withSameColor(Module m, bool on) {
    m.sameColor = on;
    if (on) m.nameColor = m.outlineColor;
    return m;
}

// Back to the default look: both colors the default light blue, linked.
inline Module withDefaultColors(Module m) {
    m.outlineColor.clear();
    m.nameColor.clear();
    m.sameColor = true;
    return m;
}

inline Module withShowName(Module m, bool show) {
    m.showName = show;
    return m;
}

}  // namespace bld::core
