#include "LoadingCard.h"

#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace bld::ui {

LoadingCard::LoadingCard(QWidget* parent, Place place) : QFrame(parent), place_(place) {
    setObjectName(QStringLiteral("LoadingCard"));
    setAutoFillBackground(true);
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(16, 12, 12, 12);
    col->setSpacing(8);

    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    title_ = new QLabel(this);
    title_->setObjectName(QStringLiteral("LoadingTitle"));
    title_->setTextFormat(Qt::RichText);
    title_->setWordWrap(true);
    row->addWidget(title_, 1);
    count_ = new QLabel(this);
    count_->setObjectName(QStringLiteral("LoadingCount"));
    row->addWidget(count_);
    retry_ = new QPushButton(tr("Retry"), this);
    retry_->setProperty("accent", true);
    retry_->setCursor(Qt::PointingHandCursor);
    row->addWidget(retry_);
    hide_ = new QToolButton(this);
    hide_->setObjectName(QStringLiteral("LoadingHide"));
    hide_->setText(QStringLiteral("✕"));
    hide_->setToolTip(tr("Hide this message"));
    hide_->setAccessibleName(tr("Hide this message"));
    hide_->setCursor(Qt::PointingHandCursor);
    row->addWidget(hide_);
    col->addLayout(row);

    bar_ = new QProgressBar(this);
    bar_->setTextVisible(false);
    bar_->setFixedHeight(8);
    col->addWidget(bar_);
    detail_ = new QLabel(this);
    detail_->setObjectName(QStringLiteral("LoadingDetail"));
    detail_->setWordWrap(true);
    col->addWidget(detail_);

    // Retry and hide only hide the card: never delete it from its own signal.
    connect(retry_, &QPushButton::clicked, this, &LoadingCard::retryRequested);
    connect(hide_, &QToolButton::clicked, this, &LoadingCard::finish);

    if (place_ != Place::Inline) {
        auto* shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(30);
        shadow->setOffset(0, 12);
        shadow->setColor(QColor(30, 33, 36, 36));
        setGraphicsEffect(shadow);
        parent->installEventFilter(this);
    }
    hide();
}

QString LoadingCard::countText(int done, int total) {
    const QLocale l;
    return tr("%1 of %2").arg(l.toString(done), l.toString(total));
}

QString LoadingCard::pictures(int n) {
    return n == 1 ? tr("1 picture") : tr("%1 pictures").arg(QLocale().toString(n));
}

void LoadingCard::showBusy(const QString& title, const QString& detail) {
    title_->setText(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()));
    count_->clear();
    count_->hide();
    setToolTip({});
    bar_->setRange(0, 0);
    bar_->setAccessibleName(title);
    detail_->setText(detail);
    detail_->setVisible(!detail.isEmpty());
    showAs(false);
}

void LoadingCard::showProgress(const QString& title, int done, int total, const QString& detail) {
    total = std::max(total, 1);
    done = std::clamp(done, 0, total);
    title_->setText(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()));
    count_->setText(countText(done, total));
    count_->show();
    setToolTip({});
    bar_->setRange(0, total);
    bar_->setValue(done);
    bar_->setAccessibleName(title);
    bar_->setAccessibleDescription(count_->text());
    detail_->setText(detail);
    detail_->setVisible(!detail.isEmpty());
    showAs(false);
}

void LoadingCard::showFailed(const QString& text) {
    title_->setText(text.toHtmlEscaped());
    count_->hide();
    detail_->hide();
    showAs(true);
}

void LoadingCard::showAs(bool failed) {
    setProperty("failed", failed);
    bar_->setVisible(!failed);
    retry_->setVisible(failed);
    hide_->setVisible(failed);
    // A card that's only telling you something doesn't get in the mouse's way.
    setAttribute(Qt::WA_TransparentForMouseEvents, !failed && place_ != Place::Inline);
    setAccessibleName(title());
    // The [failed] look reaches the labels only once they're polished again.
    for (QWidget* w : { static_cast<QWidget*>(this), static_cast<QWidget*>(title_) }) {
        style()->unpolish(w);
        style()->polish(w);
    }
    place();
    show();
    raise();
}

void LoadingCard::finish() { hide(); }

void LoadingCard::paintNow() {
    if (!isVisible()) return;
    if (layout()) layout()->activate();
    place();
    repaint();
}

void LoadingCard::setPlace(Place where) {
    if (place_ == where || place_ == Place::Inline || where == Place::Inline) return;
    place_ = where;
    place();
}

QString LoadingCard::title() const {
    QString t = title_->text();
    t.remove(QStringLiteral("<b>")).remove(QStringLiteral("</b>"));
    return t;
}

QString LoadingCard::count() const { return count_->isVisibleTo(this) ? count_->text() : QString(); }

bool LoadingCard::failureShown() const { return isVisible() && property("failed").toBool(); }

bool LoadingCard::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parent() && event->type() == QEvent::Resize) place();
    return false;
}

void LoadingCard::place() {
    if (place_ == Place::Inline) return;
    auto* p = parentWidget();
    if (!p) return;
    const int w = std::max(0, std::min(420, p->width() - 32));
    const int h = heightForWidth(w) > 0 ? heightForWidth(w) : sizeHint().height();
    resize(w, h);
    // Top: under the "Showing <view>" pill.
    const int y = place_ == Place::Centre ? (p->height() - h) / 2
                : place_ == Place::Bottom ? p->height() - h - 24
                                          : 52;
    move((p->width() - w) / 2, std::max(8, y));
}

}  // namespace bld::ui
