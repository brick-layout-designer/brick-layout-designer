#include "ModuleEditBar.h"

#include "help/HelpButton.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace bld::ui {

ModuleEditBar::ModuleEditBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("ModuleEditBar"));
    setAccessibleName(tr("Edit module"));
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(16, 6, 6, 6);
    col->setSpacing(2);
    auto* row = new QHBoxLayout();
    row->setSpacing(10);
    col->addLayout(row);
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

    sheetsRow_ = new QWidget(this);
    auto* sheetsRow = new QHBoxLayout(sheetsRow_);
    sheetsRow->setContentsMargins(0, 0, 0, 0);
    sheetsRow->setSpacing(6);
    sheets_ = new QLabel(sheetsRow_);
    sheets_->setObjectName(QStringLiteral("moduleEditSheets"));
    sheets_->setTextFormat(Qt::RichText);
    sheets_->setFont(small);
    sheets_->setForegroundRole(QPalette::PlaceholderText);
    sheetsRow->addWidget(sheets_);
    showSheets_ = new QPushButton(sheetsRow_);
    showSheets_->setObjectName(QStringLiteral("moduleEditShowSheets"));
    showSheets_->setFlat(true);
    showSheets_->setFocusPolicy(Qt::NoFocus);
    sheetsRow->addWidget(showSheets_);
    sheetsRow->addStretch(1);
    connect(showSheets_, &QPushButton::clicked, this, &ModuleEditBar::showHiddenSheets);
    sheetsRow_->hide();
    col->addWidget(sheetsRow_);
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

void ModuleEditBar::setSheetsHint(const QString& text, const QString& showLabel) {
    sheets_->setText(text);
    showSheets_->setText(showLabel);
    showSheets_->setVisible(!showLabel.isEmpty());
    sheetsRow_->setVisible(!text.isEmpty());
    adjustSize();
}

QString ModuleEditBar::sheetsHint() const { return sheetsRow_->isHidden() ? QString() : sheets_->text(); }

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
    // A pill on one line; rounded corners when the sheets line shows too.
    const double radius = std::min(r.height() / 2, 18.0);
    path.addRoundedRect(r, radius, radius);
    QColor fill = palette().color(QPalette::Window);
    fill.setAlpha(242);
    p.fillPath(path, fill);
    p.setPen(QPen(palette().color(QPalette::Mid), 1));
    p.drawPath(path);
}

}  // namespace bld::ui
