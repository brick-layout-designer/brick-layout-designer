// The redesign's look: the tokens match the web's (apps/web/src/theme/
// tokens.ts), the palette in light and dark, "match my computer" following
// the system colour scheme, the colours and bigger text, and the settings'
// JSON as the server's /api/me/preferences has it.

#include "ui/theme/AppPrefs.h"
#include "ui/theme/ThemeManager.h"
#include "ui/theme/Tokens.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QJsonArray>
#include <QSettings>
#include <QStyle>

using namespace bld::ui::theme;

namespace {

QString hex(const QColor& c) { return c.name(QColor::HexRgb).toUpper(); }

// Puts the app's palette, font and stylesheet back after each test.
class Theme : public ::testing::Test {
protected:
    void SetUp() override {
        QSettings().remove(kGroup);
        palette_ = QApplication::palette();
        font_ = QApplication::font();
        css_ = qApp->styleSheet();
    }
    void TearDown() override {
        qApp->setStyleSheet(css_);
        QApplication::setPalette(palette_);
        QApplication::setFont(font_);
        QSettings().remove(kGroup);
    }
    static constexpr const char* kGroup = "test-theme";
    QPalette palette_;
    QFont font_;
    QString css_;
};

}  // namespace

TEST(Tokens, MatchTheWebValues) {
    EXPECT_EQ(hex(neutrals(Mode::Light).bg), QStringLiteral("#F6F4EF"));
    EXPECT_EQ(hex(neutrals(Mode::Light).ink), QStringLiteral("#1E2124"));
    EXPECT_EQ(hex(neutrals(Mode::Light).muted), QStringLiteral("#5B6168"));
    EXPECT_EQ(hex(neutrals(Mode::Dark).panel), QStringLiteral("#1F2226"));
    EXPECT_EQ(hex(neutrals(Mode::Dark).okSoft), QStringLiteral("#1D3326"));
    EXPECT_EQ(hex(neutrals(Mode::Dark).tourMuted), QStringLiteral("#4A5057"));
    EXPECT_EQ(hex(accent(QStringLiteral("ocean")).main), QStringLiteral("#2459C4"));
    EXPECT_EQ(hex(accent(QStringLiteral("forest")).text(Mode::Dark)), QStringLiteral("#8FD1A8"));
    EXPECT_EQ(hex(accent(QStringLiteral("plum")).soft(Mode::Light)), QStringLiteral("#F1E8F9"));
    EXPECT_EQ(accents().size(), 5);
    EXPECT_EQ(accents().first().id, QStringLiteral("brick"));
    EXPECT_EQ(accent(QStringLiteral("no-such")).id, QStringLiteral("brick"));
    EXPECT_EQ(Radius::control, 10);
    EXPECT_EQ(Radius::section, 18);
    EXPECT_DOUBLE_EQ(kLargeTextScale, 1.125);
}

TEST(Tokens, SunnyKeepsItsYellowWithDarkText) {
    const Accent sunny = accent(QStringLiteral("sunny"));
    EXPECT_EQ(hex(sunny.main), QStringLiteral("#B8860B"));
    EXPECT_EQ(hex(sunny.onMain), QStringLiteral("#1E2124"));
    for (const Accent& a : accents()) {
        if (a.id != QLatin1String("sunny")) {
            EXPECT_EQ(hex(a.onMain), QStringLiteral("#FFFFFF")) << a.id.toStdString();
        }
    }
}

TEST(Tokens, EmbeddedFontsLoad) {
    EXPECT_TRUE(loadFonts());
}

TEST_F(Theme, PaletteInLightAndDark) {
    const QPalette light = buildPalette(Mode::Light, accent(QStringLiteral("brick")));
    EXPECT_EQ(hex(light.color(QPalette::Window)), QStringLiteral("#FFFFFF"));
    EXPECT_EQ(hex(light.color(QPalette::WindowText)), QStringLiteral("#1E2124"));
    EXPECT_EQ(hex(light.color(QPalette::Highlight)), QStringLiteral("#C2412D"));
    EXPECT_EQ(hex(light.color(QPalette::HighlightedText)), QStringLiteral("#FFFFFF"));
    EXPECT_EQ(hex(light.color(QPalette::Link)), QStringLiteral("#A8331F"));
    EXPECT_EQ(hex(light.color(QPalette::Disabled, QPalette::Text)), QStringLiteral("#5B6168"));

    const QPalette dark = buildPalette(Mode::Dark, accent(QStringLiteral("forest")));
    EXPECT_EQ(hex(dark.color(QPalette::Window)), QStringLiteral("#1F2226"));
    EXPECT_EQ(hex(dark.color(QPalette::Text)), QStringLiteral("#ECEDEF"));
    EXPECT_EQ(hex(dark.color(QPalette::Highlight)), QStringLiteral("#2E7D4F"));
    EXPECT_EQ(hex(dark.color(QPalette::Link)), QStringLiteral("#8FD1A8"));

    const QPalette sunny = buildPalette(Mode::Light, accent(QStringLiteral("sunny")));
    EXPECT_EQ(hex(sunny.color(QPalette::HighlightedText)), QStringLiteral("#1E2124"));
    EXPECT_TRUE(buildStyleSheet(Mode::Dark, accent(QStringLiteral("ocean"))).contains(QStringLiteral("#93b4f0")));
}

TEST_F(Theme, MatchMyComputerFollowsTheColourScheme) {
    EXPECT_EQ(resolveMode(ThemeChoice::System, Qt::ColorScheme::Dark), Mode::Dark);
    EXPECT_EQ(resolveMode(ThemeChoice::System, Qt::ColorScheme::Light), Mode::Light);
    EXPECT_EQ(resolveMode(ThemeChoice::System, Qt::ColorScheme::Unknown), Mode::Light);
    EXPECT_EQ(resolveMode(ThemeChoice::Light, Qt::ColorScheme::Dark), Mode::Light);
    EXPECT_EQ(resolveMode(ThemeChoice::Dark, Qt::ColorScheme::Light), Mode::Dark);

    PrefsStore store(QString::fromLatin1(kGroup));
    ThemeManager manager(store);
    AppPrefs p = store.prefs();
    p.theme = ThemeChoice::System;
    store.update(p);
    manager.onColorSchemeChanged(Qt::ColorScheme::Dark);
    EXPECT_EQ(manager.mode(), Mode::Dark);
    EXPECT_EQ(hex(QApplication::palette().color(QPalette::Window)), QStringLiteral("#1F2226"));
    manager.onColorSchemeChanged(Qt::ColorScheme::Light);
    EXPECT_EQ(hex(QApplication::palette().color(QPalette::Window)), QStringLiteral("#FFFFFF"));

    // A chosen theme ignores the system.
    p.theme = ThemeChoice::Dark;
    store.update(p);
    manager.onColorSchemeChanged(Qt::ColorScheme::Light);
    EXPECT_EQ(hex(QApplication::palette().color(QPalette::Window)), QStringLiteral("#1F2226"));
}

TEST_F(Theme, ColoursAndBiggerTextApply) {
    PrefsStore store(QString::fromLatin1(kGroup));
    ThemeManager manager(store);
    manager.apply();
    const double base = manager.baseFont().pointSizeF();
    ASSERT_GT(base, 0);
    EXPECT_DOUBLE_EQ(QApplication::font().pointSizeF(), base);
    EXPECT_EQ(QApplication::font().family(), QStringLiteral("Figtree"));

    AppPrefs p = store.prefs();
    p.accent = QStringLiteral("plum");
    p.largeText = true;
    store.update(p);  // the manager follows the store
    EXPECT_EQ(hex(QApplication::palette().color(QPalette::Highlight)), QStringLiteral("#8A4FBF"));
    EXPECT_DOUBLE_EQ(QApplication::font().pointSizeF(), base * 1.125);

    p.largeText = false;
    store.update(p);
    EXPECT_DOUBLE_EQ(QApplication::font().pointSizeF(), base);
}

TEST_F(Theme, SettingsKeepTheirValuesAndTheServersJson) {
    {
        PrefsStore store(QString::fromLatin1(kGroup));
        EXPECT_EQ(store.prefs().theme, ThemeChoice::System);
        EXPECT_EQ(store.prefs().accent, QStringLiteral("brick"));
        EXPECT_TRUE(store.prefs().helpIcons);
        EXPECT_FALSE(store.prefs().updatedAt.isValid());
        AppPrefs p;
        p.theme = ThemeChoice::Dark;
        p.accent = QStringLiteral("sunny");
        p.expertMode = true;
        p.toursSeen = { QStringLiteral("editor.intro") };
        store.update(p);
        EXPECT_TRUE(store.prefs().updatedAt.isValid());
    }
    PrefsStore again(QString::fromLatin1(kGroup));
    EXPECT_EQ(again.prefs().theme, ThemeChoice::Dark);
    EXPECT_EQ(again.prefs().accent, QStringLiteral("sunny"));
    EXPECT_TRUE(again.prefs().expertMode);  // kept for the server, though not shown
    EXPECT_TRUE(again.prefs().updatedAt.isValid());

    const QJsonObject json = again.prefs().toJson();
    EXPECT_EQ(json.value(QStringLiteral("theme")).toString(), QStringLiteral("dark"));
    EXPECT_EQ(json.value(QStringLiteral("toursSeen")).toArray().size(), 1);
    EXPECT_EQ(json.keys().size(), 8);
    EXPECT_EQ(json.value(QStringLiteral("partsIconSize")).toInt(), 96);
    EXPECT_EQ(json.value(QStringLiteral("connectionSnap")).toString(), QStringLiteral("gentle"));

    // The server's keys laid over; bad values are skipped, as the server refuses them.
    const AppPrefs merged = AppPrefs::fromJson(
        QJsonObject{ { QStringLiteral("accent"), QStringLiteral("ocean") },
                     { QStringLiteral("theme"), QStringLiteral("purple") },
                     { QStringLiteral("largeText"), QStringLiteral("yes") },
                     { QStringLiteral("toursSeen"), QJsonArray{ QStringLiteral("ok.1"), QStringLiteral("bad id") } } },
        again.prefs());
    EXPECT_EQ(merged.accent, QStringLiteral("ocean"));
    EXPECT_EQ(merged.theme, ThemeChoice::Dark);
    EXPECT_FALSE(merged.largeText);
    EXPECT_EQ(merged.toursSeen, QStringList{ QStringLiteral("ok.1") });
}
