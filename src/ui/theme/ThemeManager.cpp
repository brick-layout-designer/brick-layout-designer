#include "ThemeManager.h"

#include <QApplication>
#include <QFontDatabase>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

static void initThemeResources() { Q_INIT_RESOURCE(theme); }

namespace bld::ui::theme {

bool loadFonts() {
    static const bool ok = [] {
        initThemeResources();
        for (const char* f : { "Figtree-Regular", "Figtree-Medium", "Figtree-SemiBold", "Figtree-Bold",
                               "BricolageGrotesque-Medium", "BricolageGrotesque-Bold" })
            QFontDatabase::addApplicationFont(QStringLiteral(":/bld/fonts/%1.ttf").arg(QLatin1String(f)));
        return QFontDatabase::hasFamily(QLatin1String(kBodyFamily))
               && QFontDatabase::hasFamily(QLatin1String(kHeadingFamily));
    }();
    return ok;
}

Mode resolveMode(ThemeChoice choice, Qt::ColorScheme system) {
    switch (choice) {
    case ThemeChoice::Light: return Mode::Light;
    case ThemeChoice::Dark: return Mode::Dark;
    case ThemeChoice::System: break;
    }
    return system == Qt::ColorScheme::Dark ? Mode::Dark : Mode::Light;
}

QPalette buildPalette(Mode mode, const Accent& accent) {
    const Neutrals& n = neutrals(mode);
    QPalette p;
    const auto all = [&p](QPalette::ColorRole role, const QColor& c) { p.setColor(role, c); };
    all(QPalette::Window, n.panel);
    all(QPalette::WindowText, n.ink);
    all(QPalette::Base, n.panel);
    all(QPalette::AlternateBase, n.soft);
    all(QPalette::Text, n.ink);
    all(QPalette::Button, n.panel);
    all(QPalette::ButtonText, n.ink);
    all(QPalette::BrightText, n.danger);
    all(QPalette::Light, n.panel);
    all(QPalette::Midlight, n.line);
    all(QPalette::Mid, n.border);
    all(QPalette::Dark, n.border);
    all(QPalette::Shadow, mode == Mode::Dark ? QColor(0, 0, 0) : n.border);
    all(QPalette::Highlight, accent.main);
    all(QPalette::HighlightedText, accent.onMain);
    all(QPalette::Accent, accent.main);
    all(QPalette::Link, accent.text(mode));
    all(QPalette::LinkVisited, accent.text(mode));
    all(QPalette::PlaceholderText, n.muted);
    all(QPalette::ToolTipBase, n.tourBg);
    all(QPalette::ToolTipText, n.tourInk);
    for (QPalette::ColorRole r : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText })
        p.setColor(QPalette::Disabled, r, n.muted);
    p.setColor(QPalette::Disabled, QPalette::Base, n.soft);
    p.setColor(QPalette::Disabled, QPalette::Button, n.soft);
    p.setColor(QPalette::Disabled, QPalette::Highlight, n.border);
    return p;
}

QString buildStyleSheet(Mode mode, const Accent& a) {
    const Neutrals& n = neutrals(mode);
    QString css = QStringLiteral(R"(
QMainWindow::separator { background: @line; width: 1px; height: 1px; }
QToolBar[mainToolbar="true"] { background: @panel; border: none; border-bottom: 1px solid @line; padding: 4px 10px; spacing: 2px; }
QToolBar[mainToolbar="true"]::separator { background: @line; width: 1px; margin: 8px 6px; }
QToolBar[mainToolbar="true"] QToolButton { border: none; border-radius: @rcpx; padding: 4px 6px; color: @muted; font-weight: 600; }
QToolBar[mainToolbar="true"] QToolButton:hover { background: @soft; color: @ink; }
QToolBar[mainToolbar="true"] QToolButton:checked { background: @asoft; color: @atext; }
QToolBar[mainToolbar="true"] QToolButton:disabled { color: @border; }
QToolBar[mainToolbar="true"] QToolButton[popupMode="1"] { padding-right: 18px; }
QToolBar[mainToolbar="true"] QToolButton::menu-button { border: none; width: 14px; }
QFrame#TaskTabs { background: @soft; border-radius: @rcpx; padding: 3px; }
QFrame#TaskTabs QToolButton { border: none; border-radius: 8px; padding: 6px 14px; color: @muted; font-weight: 600; background: transparent; }
QFrame#TaskTabs QToolButton:hover { color: @ink; }
QFrame#TaskTabs QToolButton:checked { background: @panel; color: @ink; font-weight: 700; }
QToolButton#HelpButton { border: 1px solid @border; border-radius: @rcpx; background: @panel; color: @ink; font-weight: 800; min-width: 30px; min-height: 30px; }
QToolButton#HelpButton:hover { background: @soft; }
QWidget#PanelHeader { background: @panel; border-bottom: 1px solid @line; }
QLabel#PanelTitle { color: @ink; }
QToolButton#PanelClose { border: none; border-radius: 6px; color: @muted; padding: 0 4px; }
QToolButton#PanelClose:hover { background: @soft; color: @ink; }
QToolButton#PanelMenu { border: none; border-radius: 6px; color: @muted; padding: 0 4px; }
QToolButton#PanelMenu:hover { background: @soft; color: @ink; }
QToolButton#PanelMenu::menu-indicator { image: none; width: 0; }
QDockWidget { color: @ink; }
QFrame[viewCard="true"] { background: @panel; border: 1px solid @line; border-radius: @rcpx; }
QFrame[viewCard="true"][active="true"] { background: @soft; border-color: @amain; }
QFrame#ViewOptions { background: transparent; border: none; border-top: 1px solid @line; }
QToolButton[viewRowTool="true"] { background: transparent; border: none; border-radius: @rcpx; color: @muted; font-weight: 600; padding: 2px 6px; }
QToolButton[viewRowTool="true"]:hover { background: @soft; color: @ink; }
QToolButton[viewRowTool="true"]:checked { color: @ink; }
QFrame#Segmented { background: @soft; border-radius: @rcpx; }
QFrame#Segmented QPushButton { background: transparent; border: none; color: @muted; padding: 5px 10px; }
QFrame#Segmented QPushButton:checked { background: @panel; color: @ink; font-weight: 700; border: 1px solid @line; }
QFrame#ViewIndicator { background: @panel; border: 1px solid @line; border-radius: 16px; }
QFrame#ViewIndicator QPushButton { background: transparent; border: none; color: @atext; font-weight: 600; padding: 4px 10px; }
QFrame#ViewIndicator QPushButton:hover { background: @soft; }
QPushButton { background: @panel; color: @ink; border: 1px solid @border; border-radius: @rcpx; padding: 6px 14px; min-height: 20px; }
QPushButton:hover { background: @soft; }
QPushButton:pressed { background: @line; }
QPushButton:default, QPushButton[accent="true"] { background: @amain; color: @aon; border-color: @amain; font-weight: 700; }
QPushButton:disabled { color: @muted; background: @soft; border-color: @line; }
QTabWidget::pane { border: 1px solid @line; border-radius: @rcpx; top: -1px; }
QTabBar::tab { background: transparent; color: @muted; padding: 6px 14px; margin-right: 2px; border-top-left-radius: 8px; border-top-right-radius: 8px; font-weight: 600; }
QTabBar::tab:selected { background: @panel; color: @ink; border: 1px solid @line; border-bottom-color: @panel; }
QTabBar::tab:hover:!selected { color: @ink; background: @soft; }
QStatusBar { background: @panel; color: @muted; border-top: 1px solid @line; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: @muted; padding: 0 8px; }
QStatusBar QLabel#ZoomLabel { color: @ink; font-weight: 700; }
QToolTip { background: @tourBg; color: @tourInk; border: none; border-radius: 8px; padding: 6px 8px; }
QMenu { background: @panel; color: @ink; border: 1px solid @line; border-radius: 8px; padding: 4px; }
QMenu::item { padding: 6px 24px 6px 12px; border-radius: 6px; }
QMenu::item:selected { background: @soft; color: @ink; }
QMenu::item:disabled { color: @muted; }
QMenu::separator { height: 1px; background: @line; margin: 4px 8px; }
QMenuBar { background: @panel; color: @ink; border-bottom: 1px solid @line; }
QMenuBar::item { padding: 6px 10px; background: transparent; border-radius: 6px; }
QMenuBar::item:selected { background: @soft; }
)");
    const QList<std::pair<const char*, QColor>> vars{
        { "@panel", n.panel }, { "@line", n.line }, { "@border", n.border }, { "@ink", n.ink },
        { "@muted", n.muted }, { "@soft", n.soft }, { "@tourBg", n.tourBg }, { "@tourInk", n.tourInk },
        { "@asoft", a.soft(mode) }, { "@atext", a.text(mode) }, { "@amain", a.main }, { "@aon", a.onMain },
    };
    for (const auto& [name, colour] : vars) css.replace(QLatin1String(name), colour.name());
    css.replace(QLatin1String("@rcpx"), QStringLiteral("%1px").arg(Radius::control));
    return css;
}

ThemeManager::ThemeManager(PrefsStore& store, QObject* parent) : QObject(parent), store_(store) {
    system_ = QGuiApplication::styleHints()->colorScheme();
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            &ThemeManager::onColorSchemeChanged);
    connect(&store_, &PrefsStore::changed, this, [this] { apply(); });
    loadFonts();
    baseFont_ = QApplication::font();
    if (QFontDatabase::hasFamily(QLatin1String(kBodyFamily))) baseFont_.setFamily(QLatin1String(kBodyFamily));
}

void ThemeManager::onColorSchemeChanged(Qt::ColorScheme scheme) {
    system_ = scheme;
    if (store_.prefs().theme == ThemeChoice::System) apply();
}

void ThemeManager::apply() {
    const AppPrefs& p = store_.prefs();
    mode_ = resolveMode(p.theme, system_);
    const Accent& a = accent(p.accent);
    // Once: setting a style again re-polishes every widget.
    static bool fusion = false;
    if (!fusion) {
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        fusion = true;
    }
    QFont f = baseFont_;
    if (p.largeText) {
        if (f.pointSizeF() > 0) f.setPointSizeF(f.pointSizeF() * kLargeTextScale);
        else f.setPixelSize(qRound(f.pixelSize() * kLargeTextScale));
    }
    QApplication::setFont(f);
    QApplication::setPalette(buildPalette(mode_, a));
    qApp->setStyleSheet(buildStyleSheet(mode_, a));
    emit applied();
}

}  // namespace bld::ui::theme
