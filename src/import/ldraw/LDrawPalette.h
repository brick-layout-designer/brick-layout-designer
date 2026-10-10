#pragma once

#include <QColor>
#include <QHash>
#include <QString>

namespace bld::import {

// LDraw color code → RGB(A) lookup, populated from LDConfig.ldr in a
// real LDraw distribution.
//
// LDConfig.ldr lines look like:
//   0 !COLOUR <Name> CODE <c> VALUE #RRGGBB EDGE #EEEEEE [ALPHA <a>] [optional flags]
//
// We capture name, RGB, and alpha. ALPHA gives us transparent parts
// (windscreens, lampshades, transparent baseplates). Flags after ALPHA
// (METAL, RUBBER, PEARLESCENT, MATERIAL) describe surface properties
// and are ignored for top-down sprite rendering — they all collapse
// to the diffuse RGB.
//
// When no palette is loaded, lookups fall back to the bundled
// LDrawColors table (covers the first ~512 codes everyone authors
// against). Pointed at a real LDraw root the bundled fallback is
// overridden entry-by-entry.
class LDrawPalette {
public:
    // Construct an empty palette. Lookups will fall back to the bundled
    // LDrawColors data until loadFromLDConfig is called.
    LDrawPalette() = default;

    // Parse `LDConfig.ldr` from the given path. Returns true on
    // successful read (any number of `!COLOUR` lines parsed); false
    // on file-open failure. Failed parses leave the palette unchanged.
    bool loadFromLDConfig(const QString& ldconfigPath);

    // Resolve a code to an RGBA QColor. Falls back to the bundled
    // LDrawColors palette when the code isn't in the loaded LDConfig
    // (or when loadFromLDConfig was never called). Code 16 / 24 are
    // sentinels for "inherit parent color"; the renderer needs to
    // resolve them earlier in the pipeline.
    QColor color(int code) const;

    bool isTransparent(int code) const;

    int  size() const { return entries_.size(); }
    bool isEmpty() const { return entries_.isEmpty(); }

private:
    struct Entry {
        QColor color;
        bool   transparent = false;
    };
    QHash<int, Entry> entries_;
};

}  // namespace bld::import
