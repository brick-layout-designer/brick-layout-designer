#pragma once

// Design tokens: the one list of colours, type and shapes the desktop app
// and the web app (apps/web/src/theme/tokens.ts in the web repo) share.
// Keep the values identical in both; TokensTest spot-checks them.
//
// Only the app's chrome (panels, toolbars, docks, dialogs, the status
// bar) uses these. The map canvas keeps the background and grid colours
// the layout file carries.

#include <QColor>
#include <QList>
#include <QString>

namespace bld::ui::theme {

enum class Mode { Light, Dark };

struct Neutrals {
    QColor bg, panel, line, border, ink, muted, soft, grid, gridLine, roomLine;
    QColor ok, okSoft, danger, tourBg, tourInk, tourMuted;
};

const Neutrals& neutrals(Mode mode);

struct Accent {
    QString id;     // brick, ocean, forest, plum, sunny (the stored value)
    QString label;  // plain-words name shown in Settings
    QColor main;    // filled buttons, the selected tool, focus rings
    QColor onMain;  // text and icons drawn on `main`
    QColor softLight, softDark;
    QColor textLight, textDark;  // accent-coloured text on panels

    QColor soft(Mode m) const { return m == Mode::Dark ? softDark : softLight; }
    QColor text(Mode m) const { return m == Mode::Dark ? textDark : textLight; }
};

// In the order Settings shows them; brick first (the default).
const QList<Accent>& accents();
// The accent with this id, or brick for an unknown one.
const Accent& accent(const QString& id);

inline constexpr const char* kDefaultAccent = "brick";

// Families registered from the embedded fonts (see ThemeManager::loadFonts).
inline constexpr const char* kHeadingFamily = "Bricolage Grotesque";
inline constexpr const char* kBodyFamily = "Figtree";

// Corner radii in px: controls, cards, bubbles, sections.
struct Radius {
    static constexpr int control = 10;
    static constexpr int card = 12;
    static constexpr int bubble = 14;
    static constexpr int section = 18;
};

// Spacing grid in px.
inline constexpr int kSpacing = 8;

// Font scale for "Bigger text and buttons".
inline constexpr double kLargeTextScale = 1.125;

}  // namespace bld::ui::theme
