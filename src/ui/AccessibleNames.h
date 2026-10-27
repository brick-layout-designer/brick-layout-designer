#pragma once

// Names for screen readers that Qt leaves out. A form's label names the
// field beside it (QLabel::setBuddy, as QFormLayout does), and Qt reads
// that name for line edits and spin boxes, but not for drop-down lists:
// those are said by their current choice (Linux) or not at all (Windows).
// install() fills in the name of every such list from its label as it is
// shown, all over the app.

class QString;
class QWidget;

namespace bld::ui::a11y {

// Once, on the application (main.cpp; tests that check names call it too).
void install();

// The name the label beside `w` gives it, without '&', tags or the colon;
// empty when no label names it.
QString labelNameFor(const QWidget* w);

}  // namespace bld::ui::a11y
