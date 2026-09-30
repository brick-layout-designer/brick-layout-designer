#pragma once

// Simple line icons for the toolbar, drawn in code (no image files, no SVG
// module) in the current palette's colours: muted normally, ink when
// hovered or checked. Call again after a palette change.

#include <QIcon>
#include <QPalette>
#include <QString>

namespace bld::ui::theme {

// select, measure, circle, paint, erase, undo, redo, new, open, save,
// delete, turnLeft, turnRight, snap, angle. An unknown name draws nothing.
QIcon lineIcon(const QString& name, const QPalette& palette);

}  // namespace bld::ui::theme
