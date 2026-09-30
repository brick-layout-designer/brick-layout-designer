#include "PanelHeader.h"

#include "AppPrefs.h"
#include "Tokens.h"

#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QToolTip>

namespace bld::ui::theme {

PanelHeader::PanelHeader(QDockWidget* dock, const QString& helpText, PrefsStore& store)
    : QWidget(dock), helpText_(helpText) {
    setObjectName(QStringLiteral("PanelHeader"));
    setAttribute(Qt::WA_StyledBackground);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(2 * kSpacing, kSpacing, kSpacing, kSpacing);
    row->setSpacing(kSpacing);
    title_ = new QLabel(dock->windowTitle(), this);
    title_->setObjectName(QStringLiteral("PanelTitle"));
    row->addWidget(title_, 1);
    help_ = new QToolButton(this);
    help_->setObjectName(QStringLiteral("PanelHelp"));
    help_->setText(QStringLiteral("?"));
    help_->setToolTip(helpText);
    help_->setAccessibleName(tr("What is %1?").arg(dock->windowTitle()));
    help_->setCursor(Qt::PointingHandCursor);
    help_->setVisible(!helpText.isEmpty() && store.prefs().helpIcons);
    row->addWidget(help_);
    connect(help_, &QToolButton::clicked, this, [this] {
        QToolTip::showText(help_->mapToGlobal(QPoint(0, help_->height())), helpText_, help_);
    });
    auto* close = new QToolButton(this);
    close->setObjectName(QStringLiteral("PanelClose"));
    close->setText(QStringLiteral("×"));
    close->setToolTip(tr("Hide this panel (View menu brings it back)"));
    close->setAccessibleName(tr("Hide %1").arg(dock->windowTitle()));
    row->addWidget(close);
    connect(close, &QToolButton::clicked, dock, &QDockWidget::close);
    connect(dock, &QDockWidget::windowTitleChanged, title_, &QLabel::setText);
    connect(&store, &PrefsStore::changed, this, [this, &store] {
        help_->setVisible(!helpText_.isEmpty() && store.prefs().helpIcons);
    });
    refreshFont();
}

PanelHeader* PanelHeader::install(QDockWidget* dock, const QString& helpText, PrefsStore& store) {
    auto* header = new PanelHeader(dock, helpText, store);
    dock->setTitleBarWidget(header);
    return header;
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
