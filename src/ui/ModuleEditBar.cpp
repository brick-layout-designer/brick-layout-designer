#include "ModuleEditBar.h"

#include "help/HelpButton.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>

namespace bld::ui {

ModuleEditBar::ModuleEditBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("ModuleEditBar"));
    setAccessibleName(tr("Edit module"));
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(16, 6, 6, 6);
    row->setSpacing(10);
    text_ = new QLabel(this);
    text_->setObjectName(QStringLiteral("moduleEditText"));
    text_->setTextFormat(Qt::RichText);
    row->addWidget(text_);
    others_ = new QLabel(this);
    others_->setObjectName(QStringLiteral("moduleEditOthers"));
    QFont small = others_->font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    others_->setFont(small);
    others_->setForegroundRole(QPalette::PlaceholderText);
    others_->hide();
    row->addWidget(others_);
    row->addWidget(new help::HelpButton(QStringLiteral("module.edit"), this));
    done_ = new QPushButton(tr("Done"), this);
    done_->setObjectName(QStringLiteral("moduleEditDone"));
    done_->setToolTip(tr("Back to the whole layout (Esc)"));
    done_->setProperty("accent", true);
    done_->setFocusPolicy(Qt::NoFocus);
    row->addWidget(done_);
    connect(done_, &QPushButton::clicked, this, &ModuleEditBar::done);
    hide();
}

void ModuleEditBar::setModuleName(const QString& name) {
    text_->setText(tr("Editing module <b>%1</b>").arg(name.toHtmlEscaped()));
    adjustSize();
}

void ModuleEditBar::setOthers(const QString& text) {
    others_->setText(text);
    others_->setVisible(!text.isEmpty());
    adjustSize();
}

QString ModuleEditBar::text() const { return text_->text(); }
QString ModuleEditBar::others() const { return others_->isHidden() ? QString() : others_->text(); }

void ModuleEditBar::place(const QRect& area) {
    adjustSize();
    const QSize s = sizeHint().boundedTo(area.size() - QSize(32, 0));
    resize(s);
    move(area.left() + (area.width() - s.width()) / 2, area.top() + 8);
}

void ModuleEditBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    path.addRoundedRect(r, r.height() / 2, r.height() / 2);
    QColor fill = palette().color(QPalette::Window);
    fill.setAlpha(242);
    p.fillPath(path, fill);
    p.setPen(QPen(palette().color(QPalette::Mid), 1));
    p.drawPath(path);
}

}  // namespace bld::ui
