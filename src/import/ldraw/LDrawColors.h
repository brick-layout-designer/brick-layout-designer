#pragma once

// LDraw standard color code → RGB lookup. Sourced from the LDraw
// official color reference (https://www.ldraw.org/article/547.html)
// and matched against LDConfig.ldr distributed with LDraw 2021-04.
//
// The BlueBrick palette shares the same codes for the first 16 colors
// (1=blue, 4=red, 14=yellow, …) so re-using LDraw codes lets us write
// a single lookup that the import pipeline and the parts library both
// grok without a translation table.
//
// We omit the exotic metallic / pearl / speckle modifiers — this
// lookup gives the *diffuse* color only, which is what a top-down
// sprite needs. Transparency is carried separately via alpha.

#include <QColor>

namespace bld::import {

// Look up a standard LDraw color. Returns the "main" diffuse RGB.
// Unknown codes fall back to code 16 (main-color slot) — vanilla
// LDraw behaviour when a part references color 16 is "use the
// parent's current color", but top-level we want something
// visible, so we map 16 → light grey.
QColor ldrawColor(int code);

// True when the color code denotes a transparent material. Used
// to set the sprite's alpha so see-through parts like windscreens
// blend correctly when composited.
bool   ldrawColorIsTransparent(int code);

}  // namespace bld::import
