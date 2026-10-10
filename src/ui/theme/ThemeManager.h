#pragma once

// Applies the look from the tokens to the whole app: the Fusion style, a
// QPalette, the embedded fonts and a small stylesheet (radii, spacing,
// panel headers, buttons, tabs, the status bar), for light, dark or "match
// my computer". Follows PrefsStore and the system color scheme.

#include "AppPrefs.h"
#include "Tokens.h"

#include <QFont>
#include <QObject>
#include <QPalette>

namespace bld::ui::theme {

// Registers the embedded Figtree and Bricolage Grotesque fonts (once).
// True when both families are available afterwards.
bool loadFonts();

// Light or dark, for a choice and the system's scheme.
Mode resolveMode(ThemeChoice choice, Qt::ColorScheme system);

QPalette buildPalette(Mode mode, const Accent& accent);
// `touch`: touch mode's bigger dividers (TouchMode.h).
QString buildStyleSheet(Mode mode, const Accent& accent, bool touch = false);

class ThemeManager : public QObject {
    Q_OBJECT
public:
    explicit ThemeManager(PrefsStore& store, QObject* parent = nullptr);

    // Style, palette, font and stylesheet onto qApp, from the store.
    void apply();

    Mode mode() const { return mode_; }
    // The app font "Bigger text" scales (the body font at the normal size).
    QFont baseFont() const { return baseFont_; }

public slots:
    // The system switched light / dark (QStyleHints::colorSchemeChanged).
    void onColorSchemeChanged(Qt::ColorScheme scheme);

signals:
    void applied();

private:
    PrefsStore& store_;
    Qt::ColorScheme system_ = Qt::ColorScheme::Unknown;
    Mode mode_ = Mode::Light;
    QFont baseFont_;
};

}  // namespace bld::ui::theme
