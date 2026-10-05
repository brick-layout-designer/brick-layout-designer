#pragma once

// A placed module's own look: whether its name shows, its outline and name
// colours, and the "Same colour" link that keeps the two in step. Pure
// functions over a Module; edit::UpdateModuleCommand writes the result.
// The web's apps/web/src/editor/moduleLook.ts does the same.

#include "Module.h"

#include <QString>

namespace bld::core {

enum class ModuleColourPart { Outline, Name };

// The default look's colour, as a colour picker shows it (the light blue
// rgba(100,180,255) without its opacity).
inline const QString kModuleDefaultColour = QStringLiteral("#64b4ff");

inline bool coloursLinked(const Module& m) { return m.sameColor; }

inline QString moduleColour(const Module& m, ModuleColourPart part) {
    const QString& c = part == ModuleColourPart::Outline ? m.outlineColor : m.nameColor;
    return c.isEmpty() ? kModuleDefaultColour : c;
}

inline bool hasCustomColours(const Module& m) {
    return !m.outlineColor.isEmpty() || !m.nameColor.isEmpty() || !m.sameColor;
}

// Sets one part's colour; with "Same colour" on, the other part follows.
inline Module withColour(Module m, ModuleColourPart part, const QString& hex) {
    const QString c = hex.toLower();
    if (m.sameColor) {
        m.outlineColor = c;
        m.nameColor = c;
    } else if (part == ModuleColourPart::Outline) {
        m.outlineColor = c;
    } else {
        m.nameColor = c;
    }
    return m;
}

// Turns "Same colour" on or off. Turning it on gives the name the
// outline's colour (or both the default, if the outline has none).
inline Module withSameColour(Module m, bool on) {
    m.sameColor = on;
    if (on) m.nameColor = m.outlineColor;
    return m;
}

// Back to the default look: both colours the default light blue, linked.
inline Module withDefaultColours(Module m) {
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
