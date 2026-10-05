#pragma once

// "Source on GitHub": where the desktop app's and the web app's code live,
// as a quiet line (the sign-in screen) and in Help › About. Links open in
// the system browser.

#include <QString>

class QLabel;
class QWidget;

namespace bld::ui::help {

inline constexpr const char* kDesktopSourceUrl = "https://github.com/brick-layout-designer/brick-layout-designer";
inline constexpr const char* kWebSourceUrl =
    "https://github.com/brick-layout-designer/collaborative-brick-layout-designer";

// "Source on GitHub: desktop app · web app", as rich text with both links.
QString sourceLinksHtml();

// That line as a small, muted label (objectName "sourceLinks") whose links
// open in the browser.
QLabel* makeSourceLinksLabel(QWidget* parent);

}  // namespace bld::ui::help
