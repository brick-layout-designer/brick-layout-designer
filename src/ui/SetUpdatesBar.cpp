#include "SetUpdatesBar.h"

#include "help/HelpButton.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>

#include <algorithm>

namespace bld::ui {

SetUpdatesBar::SetUpdatesBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("SetUpdatesBar"));
    setAccessibleName(tr("Update available"));
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(16, 6, 8, 6);
    row->setSpacing(8);
    text_ = new QLabel(this);
    text_->setObjectName(QStringLiteral("setUpdatesText"));
    text_->setTextFormat(Qt::RichText);
    text_->setWordWrap(true);
    row->addWidget(text_, 1);
    row->addWidget(new help::HelpButton(QStringLiteral("set.updates"), this));
    show_ = new QPushButton(tr("Show"), this);
    show_->setObjectName(QStringLiteral("setUpdatesShow"));
    show_->setToolTip(tr("Pick them on the map"));
    show_->setFocusPolicy(Qt::NoFocus);
    row->addWidget(show_);
    update_ = new QPushButton(tr("Update"), this);
    update_->setObjectName(QStringLiteral("setUpdatesUpdate"));
    update_->setProperty("accent", true);
    update_->setFocusPolicy(Qt::NoFocus);
    row->addWidget(update_);
    close_ = new QPushButton(QStringLiteral("×"), this);
    close_->setObjectName(QStringLiteral("setUpdatesClose"));
    close_->setToolTip(tr("Not now"));
    close_->setAccessibleName(tr("Not now"));
    close_->setFlat(true);
    close_->setFocusPolicy(Qt::NoFocus);
    row->addWidget(close_);
    connect(show_, &QPushButton::clicked, this, &SetUpdatesBar::showRequested);
    connect(update_, &QPushButton::clicked, this, &SetUpdatesBar::updateRequested);
    connect(close_, &QPushButton::clicked, this, [this] {
        closedFor_ = key_;
        hide();
    });
    hide();
}

void SetUpdatesBar::setUpdates(const QStringList& names, const QString& key) {
    key_ = key;
    if (names.isEmpty() || key == closedFor_) {
        hide();
        return;
    }
    QMap<QString, int> counts;
    QStringList order;
    for (const QString& n : names) {
        if (!counts.contains(n)) order << n;
        ++counts[n];
    }
    QStringList listed;
    for (const QString& n : std::as_const(order))
        listed << (counts.value(n) > 1 ? tr("“%1” (%2 placed)").arg(n.toHtmlEscaped()).arg(counts.value(n))
                                       : tr("“%1”").arg(n.toHtmlEscaped()));
    text_->setText(order.size() == 1
                       ? tr("<b>Update available:</b> the set %1 changed since it was placed.")
                             .arg(listed.join(QStringLiteral(", ")))
                       : tr("<b>Update available:</b> the sets %1 changed since they were placed.")
                             .arg(listed.join(QStringLiteral(", "))));
    update_->setText(names.size() == 1 ? tr("Update") : tr("Update all %1").arg(names.size()));
    adjustSize();
    show();
    raise();
}

QString SetUpdatesBar::text() const {
    return text_->text();
}

void SetUpdatesBar::place(const QRect& area) {
    const int width = std::min(area.width() - 32, 640);
    const int height = heightForWidth(width) > 0 ? heightForWidth(width) : sizeHint().height();
    resize(width, height);
    move(area.left() + (area.width() - width) / 2, area.bottom() - height - 48);
}

void SetUpdatesBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const double radius = std::min(r.height() / 2, 14.0);
    path.addRoundedRect(r, radius, radius);
    QColor fill = palette().color(QPalette::Window);
    fill.setAlpha(245);
    p.fillPath(path, fill);
    p.setPen(QPen(palette().color(QPalette::Highlight), 1.5));
    p.drawPath(path);
}

} // namespace bld::ui
