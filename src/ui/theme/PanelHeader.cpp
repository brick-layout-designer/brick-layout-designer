#include "PanelHeader.h"

#include "AppPrefs.h"
#include "Tokens.h"
#include "ui/help/HelpButton.h"

#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QMainWindow>
#include <QMenu>
#include <QTimer>
#include <QToolButton>

namespace bld::ui::theme {

namespace {

// The web's dock width (dockLayout.ts DEFAULT_WIDTH, MIN_DOCK_WIDTH).
constexpr int kDefaultWidth = 280;
constexpr int kMinWidth = 180;

QMainWindow* mainWindowOf(const QDockWidget* dock) {
    for (QWidget* w = dock->parentWidget(); w; w = w->parentWidget())
        if (auto* mw = qobject_cast<QMainWindow*>(w)) return mw;
    return nullptr;
}

// True when no other shown panel is docked on `area`.
bool sideEmpty(QMainWindow* mw, Qt::DockWidgetArea area, const QDockWidget* except) {
    for (QDockWidget* d : mw->findChildren<QDockWidget*>())
        if (d != except && !d->isHidden() && !d->isFloating() && mw->dockWidgetArea(d) == area) return false;
    return true;
}

}  // namespace

QList<PanelHeader::Target> PanelHeader::targets(PanelPlace current) {
    if (current == PanelPlace::Float)
        return { { PanelPlace::Left, tr("Dock to left") },
                 { PanelPlace::Right, tr("Dock to right") },
                 { PanelPlace::Hidden, tr("Hide panel") } };
    QList<Target> out;
    if (current != PanelPlace::Left) out.append({ PanelPlace::Left, tr("Move to left") });
    if (current != PanelPlace::Right) out.append({ PanelPlace::Right, tr("Move to right") });
    out.append({ PanelPlace::Float, tr("Float panel") });
    if (current != PanelPlace::Hidden) out.append({ PanelPlace::Hidden, tr("Hide panel") });
    return out;
}

PanelPlace PanelHeader::placeOf(const QDockWidget* dock) {
    if (dock->isHidden()) return PanelPlace::Hidden;
    if (dock->isFloating()) return PanelPlace::Float;
    QMainWindow* mw = mainWindowOf(dock);
    switch (mw ? mw->dockWidgetArea(const_cast<QDockWidget*>(dock)) : Qt::NoDockWidgetArea) {
    case Qt::LeftDockWidgetArea: return PanelPlace::Left;
    case Qt::RightDockWidgetArea: return PanelPlace::Right;
    default: return PanelPlace::Elsewhere;
    }
}

void PanelHeader::moveTo(QDockWidget* dock, PanelPlace place) {
    switch (place) {
    case PanelPlace::Left:
    case PanelPlace::Right: {
        QMainWindow* mw = mainWindowOf(dock);
        if (!mw) return;
        const Qt::DockWidgetArea area =
            place == PanelPlace::Left ? Qt::LeftDockWidgetArea : Qt::RightDockWidgetArea;
        // Qt only offers a drop zone near the window's edge, so an empty
        // side is hard to reach by dragging; this puts the panel there.
        const bool empty = sideEmpty(mw, area, dock);
        const int width = dock->width() >= kMinWidth ? dock->width() : kDefaultWidth;
        if (dock->isFloating()) dock->setFloating(false);
        mw->addDockWidget(area, dock);
        dock->show();
        dock->raise();
        if (empty) {
            // Lay the new side out first, or the width is overwritten.
            mw->layout()->activate();
            mw->resizeDocks({ dock }, { width }, Qt::Horizontal);
        }
        break;
    }
    case PanelPlace::Float:
        dock->show();
        dock->setFloating(true);
        dock->raise();
        break;
    case PanelPlace::Hidden:
    case PanelPlace::Elsewhere:
        dock->close();
        break;
    }
}

void PanelHeader::setShown(QDockWidget* dock, bool shown) {
    if (!shown) dock->close();
    else if (dock->isHidden()) moveTo(dock, PanelPlace::Right);
}

PanelHeader::PanelHeader(QDockWidget* dock, const QString& helpKey, PrefsStore& store)
    : QWidget(dock), dock_(dock) {
    setObjectName(QStringLiteral("PanelHeader"));
    setAttribute(Qt::WA_StyledBackground);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(2 * kSpacing, kSpacing, kSpacing, kSpacing);
    row->setSpacing(kSpacing);
    title_ = new QLabel(dock->windowTitle(), this);
    title_->setObjectName(QStringLiteral("PanelTitle"));
    row->addWidget(title_, 1);
    if (!helpKey.isEmpty()) {
        help_ = new help::HelpButton(helpKey, store, this, dock);
        row->addWidget(help_);
    }
    // "⋯": move to the other side, float, or hide, like the web.
    menuButton_ = new QToolButton(this);
    menuButton_->setObjectName(QStringLiteral("PanelMenu"));
    menuButton_->setText(QStringLiteral("⋯"));
    menuButton_->setToolTip(tr("Move or hide panel"));
    menuButton_->setAccessibleName(tr("Move or hide %1").arg(dock->windowTitle()));
    menuButton_->setPopupMode(QToolButton::InstantPopup);
    menu_ = new QMenu(menuButton_);
    menu_->setObjectName(QStringLiteral("PanelMoveMenu"));
    menuButton_->setMenu(menu_);
    connect(menu_, &QMenu::aboutToShow, this, &PanelHeader::fillMenu);
    fillMenu();
    row->addWidget(menuButton_);
    auto* close = new QToolButton(this);
    close->setObjectName(QStringLiteral("PanelClose"));
    close->setText(QStringLiteral("×"));
    close->setToolTip(tr("Hide this panel (the Panels menu brings it back)"));
    close->setAccessibleName(tr("Hide %1").arg(dock->windowTitle()));
    row->addWidget(close);
    connect(close, &QToolButton::clicked, dock, &QDockWidget::close);
    connect(dock, &QDockWidget::windowTitleChanged, title_, &QLabel::setText);
    connect(dock, &QDockWidget::windowTitleChanged, menuButton_, [this](const QString& t) {
        menuButton_->setAccessibleName(tr("Move or hide %1").arg(t));
    });
    // The menu follows the panel when it floats, docks or changes side.
    connect(dock, &QDockWidget::topLevelChanged, this, &PanelHeader::fillMenu);
    connect(dock, &QDockWidget::dockLocationChanged, this, &PanelHeader::fillMenu);
    refreshFont();
}

PanelHeader* PanelHeader::install(QDockWidget* dock, const QString& helpKey, PrefsStore& store) {
    auto* header = new PanelHeader(dock, helpKey, store);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    dock->setTitleBarWidget(header);
    return header;
}

void PanelHeader::fillMenu() {
    menu_->clear();
    for (const Target& t : targets(placeOf(dock_))) {
        QAction* a = menu_->addAction(t.label);
        a->setData(static_cast<int>(t.place));
        // Moved once the menu has closed, never inside its own signal.
        connect(a, &QAction::triggered, dock_, [dock = dock_, place = t.place] {
            QTimer::singleShot(0, dock, [dock, place] { moveTo(dock, place); });
        });
    }
}

void PanelHeader::changeEvent(QEvent* e) {
    QWidget::changeEvent(e);
    if (e->type() == QEvent::FontChange || e->type() == QEvent::ApplicationFontChange) refreshFont();
}

void PanelHeader::refreshFont() {
    QFont f = QApplication::font();
    f.setFamily(QLatin1String(kHeadingFamily));
    f.setWeight(QFont::Bold);
    if (f.pointSizeF() > 0) f.setPointSizeF(f.pointSizeF() * 1.3);
    title_->setFont(f);
}

}  // namespace bld::ui::theme
