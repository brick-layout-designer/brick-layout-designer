#include "TouchActionBar.h"

#include "theme/Icons.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>

namespace bld::ui {

namespace {
constexpr int kButton = 56;  // a comfortable finger, square
}

TouchActionBar::TouchActionBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("TouchActionBar"));
    setAttribute(Qt::WA_StyledBackground, false);
    row_ = new QHBoxLayout(this);
    row_->setContentsMargins(8, 6, 8, 6);
    row_->setSpacing(4);

    placing_ = new QLabel(this);
    placing_->setObjectName(QStringLiteral("touchPlacingText"));
    row_->addWidget(placing_);

    selectionButtons_ = {
        addButton(QStringLiteral("touchRotateLeft"), tr("Rotate left"), QStringLiteral("turnLeft")),
        addButton(QStringLiteral("touchRotateRight"), tr("Rotate right"), QStringLiteral("turnRight")),
        addButton(QStringLiteral("touchDuplicate"), tr("Duplicate"), {}),
        addButton(QStringLiteral("touchDelete"), tr("Delete"), QStringLiteral("delete")),
        addButton(QStringLiteral("touchDone"), tr("Done"), {}),
    };
    idleButtons_ = {
        addButton(QStringLiteral("touchAddPart"), tr("Add part"), {}),
        addButton(QStringLiteral("touchUndo"), tr("Undo"), QStringLiteral("undo")),
        addButton(QStringLiteral("touchRedo"), tr("Redo"), QStringLiteral("redo")),
    };
    placingButtons_ = { addButton(QStringLiteral("touchCancelPlacing"), tr("Cancel"), {}) };

    connect(button(QStringLiteral("touchRotateLeft")), &QToolButton::clicked, this, &TouchActionBar::rotateLeft);
    connect(button(QStringLiteral("touchRotateRight")), &QToolButton::clicked, this, &TouchActionBar::rotateRight);
    connect(button(QStringLiteral("touchDuplicate")), &QToolButton::clicked, this, &TouchActionBar::duplicate);
    connect(button(QStringLiteral("touchDelete")), &QToolButton::clicked, this, &TouchActionBar::remove);
    connect(button(QStringLiteral("touchDone")), &QToolButton::clicked, this, &TouchActionBar::done);
    connect(button(QStringLiteral("touchAddPart")), &QToolButton::clicked, this, &TouchActionBar::addPart);
    connect(button(QStringLiteral("touchUndo")), &QToolButton::clicked, this, &TouchActionBar::undo);
    connect(button(QStringLiteral("touchRedo")), &QToolButton::clicked, this, &TouchActionBar::redo);
    connect(button(QStringLiteral("touchCancelPlacing")), &QToolButton::clicked, this, &TouchActionBar::cancelPlacing);
    refreshIcons();
    setMode(Mode::Idle);
    hide();
}

QToolButton* TouchActionBar::addButton(const QString& name, const QString& text, const QString& icon) {
    auto* b = new QToolButton(this);
    b->setObjectName(name);
    b->setText(text);
    b->setAccessibleName(text);
    b->setProperty("lineIcon", icon);
    b->setToolButtonStyle(icon.isEmpty() ? Qt::ToolButtonTextOnly : Qt::ToolButtonTextUnderIcon);
    b->setIconSize(QSize(24, 24));
    b->setMinimumSize(kButton, kButton);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    row_->addWidget(b);
    return b;
}

QToolButton* TouchActionBar::button(const QString& objectName) const {
    return findChild<QToolButton*>(objectName);
}

void TouchActionBar::setMode(Mode mode, const QString& placingName) {
    mode_ = mode;
    for (auto* b : selectionButtons_) b->setVisible(mode == Mode::Selection);
    for (auto* b : idleButtons_) b->setVisible(mode == Mode::Idle);
    for (auto* b : placingButtons_) b->setVisible(mode == Mode::Placing);
    placing_->setVisible(mode == Mode::Placing);
    placing_->setText(tr("Tap the map to place %1").arg(placingName));
    adjustSize();
}

void TouchActionBar::place(const QRect& area) {
    adjustSize();
    const QSize s = sizeHint().boundedTo(area.size());
    resize(s);
    move(area.left() + (area.width() - s.width()) / 2, area.bottom() - s.height() - 16);
}

void TouchActionBar::refreshIcons() {
    for (auto* b : findChildren<QToolButton*>()) {
        const QString name = b->property("lineIcon").toString();
        if (!name.isEmpty()) b->setIcon(theme::lineIcon(name, palette()));
    }
}

void TouchActionBar::changeEvent(QEvent* e) {
    if (e->type() == QEvent::PaletteChange) refreshIcons();
    QWidget::changeEvent(e);
}

void TouchActionBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
    p.fillPath(path, palette().color(QPalette::Window));
    p.setPen(QPen(palette().color(QPalette::Mid), 1));
    p.drawPath(path);
}

}  // namespace bld::ui
