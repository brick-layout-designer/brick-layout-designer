#include "HelpButton.h"

#include "HelpPages.h"
#include "HelpTexts.h"
#include "ui/theme/AppPrefs.h"

#include <QAction>
#include <QApplication>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QToolBar>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <cmath>
#include <numbers>

namespace bld::ui::help {

namespace {

constexpr int kCircle = 20;     // the round "?"
constexpr int kGap = 8;         // between the button and a tooltip or popover
constexpr int kEdge = 8;        // kept from the screen's edges
constexpr int kTipWidth = 240;  // widest a tooltip gets
constexpr int kPopoverWidth = 300;
constexpr int kPulseMs = 2700;

// `a` over `b`, `amount` of the way.
QColor mix(const QColor& a, const QColor& b, double amount) {
    return QColor::fromRgbF(static_cast<float>(b.redF() + (a.redF() - b.redF()) * amount),
                            static_cast<float>(b.greenF() + (a.greenF() - b.greenF()) * amount),
                            static_cast<float>(b.blueF() + (a.blueF() - b.blueF()) * amount));
}

// The dark "tour" bubble colours, from the app's palette (ThemeManager
// sets ToolTipBase / ToolTipText to the tokens' tourBg / tourInk).
QColor bubbleBg() { return QApplication::palette().color(QPalette::ToolTipBase); }
QColor bubbleInk() { return QApplication::palette().color(QPalette::ToolTipText); }

// The tooltip: the sentence on a dark rounded bubble.
class TipBubble : public QLabel {
public:
    TipBubble() : QLabel(nullptr, Qt::ToolTip | Qt::FramelessWindowHint) {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
        setWordWrap(true);
        setContentsMargins(12, 8, 12, 8);
    }

protected:
    void paintEvent(QPaintEvent* e) override {
        {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(bubbleBg());
            p.drawRoundedRect(QRectF(rect()), 8, 8);
        }
        QLabel::paintEvent(e);
    }
};

// Draws the outline pulse over a control, then deletes itself.
class PulseOverlay : public QWidget {
public:
    PulseOverlay(QWidget* target, QWidget* host) : QWidget(host) {
        setObjectName(QStringLiteral("HelpPulse"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        const QRect r(target->mapTo(host, QPoint(0, 0)), target->size());
        setGeometry(r.adjusted(-4, -4, 4, 4).intersected(host->rect()));
        auto* anim = new QVariantAnimation(this);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setDuration(kPulseMs);
        connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            phase_ = v.toDouble();
            update();
        });
        connect(anim, &QVariantAnimation::finished, this, &QObject::deleteLater);
        raise();
        show();
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        // Three soft pulses.
        const double strength = std::sin(std::numbers::pi * std::fmod(phase_ * 3.0, 1.0));
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QColor ring = palette().color(QPalette::Highlight);
        QColor halo = ring;
        halo.setAlphaF(static_cast<float>(0.3 * strength));
        ring.setAlphaF(static_cast<float>(0.4 + 0.6 * strength));
        const QRectF r = QRectF(rect()).adjusted(3, 3, -3, -3);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(halo, 6));
        p.drawRoundedRect(r, 8, 8);
        p.setPen(QPen(ring, 2.5));
        p.drawRoundedRect(r, 8, 8);
    }

private:
    double phase_ = 0.0;
};

}  // namespace

// The click-open box: the title, the longer text, "Show me" and "Learn
// more". A popup, so a click anywhere else closes it.
class HelpPopover : public QFrame {
public:
    HelpPopover(HelpButton* button, const HelpEntry& entry) : QFrame(button, Qt::Popup), button_(button) {
        setObjectName(QStringLiteral("HelpPopover"));
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_DeleteOnClose);
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(entry.title);
        setFixedWidth(kPopoverWidth);
        auto* col = new QVBoxLayout(this);
        col->setContentsMargins(16, 14, 16, 14);
        col->setSpacing(8);
        auto* title = new QLabel(entry.title, this);
        title->setObjectName(QStringLiteral("HelpTitle"));
        QFont f = title->font();
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() * 1.1);
        title->setFont(f);
        col->addWidget(title);
        auto* more = new QLabel(entry.more, this);
        more->setObjectName(QStringLiteral("HelpMore"));
        more->setWordWrap(true);
        col->addWidget(more);
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 4, 0, 0);
        row->setSpacing(8);
        auto* showMe = new QPushButton(HelpButton::tr("Show me"), this);
        showMe->setObjectName(QStringLiteral("HelpShowMe"));
        showMe->setAutoDefault(false);
        showMe->setCursor(Qt::PointingHandCursor);
        row->addWidget(showMe);
        connect(showMe, &QPushButton::clicked, button, &HelpButton::showMe);
        auto* learn = new QPushButton(HelpButton::tr("Learn more"), this);
        learn->setObjectName(QStringLiteral("HelpLearnMore"));
        learn->setAutoDefault(false);
        learn->setCursor(Qt::PointingHandCursor);
        const QUrl page = helpPageFor(entry.learnMoreUrl);
        learn->setVisible(!page.isEmpty());
        learn->setToolTip(HelpButton::tr("Opens the help pages"));
        connect(learn, &QPushButton::clicked, this, [this, page] {
            openHelpPage(page);
            button_->closePopover(false);
        });
        row->addWidget(learn);
        row->addStretch(1);
        col->addLayout(row);

        const QColor bg = bubbleBg();
        const QColor ink = bubbleInk();
        setStyleSheet(QStringLiteral(
            "QFrame#HelpPopover QLabel { color: %2; background: transparent; }"
            "QLabel#HelpMore { color: %3; }"
            "QPushButton#HelpShowMe { background: %2; color: %1; border: none; border-radius: 8px;"
            " padding: 6px 12px; font-weight: 700; min-height: 20px; }"
            "QPushButton#HelpShowMe:hover { background: %3; }"
            "QPushButton#HelpShowMe:focus { outline: none; border: 2px solid %4; }"
            "QPushButton#HelpLearnMore { background: transparent; color: %2; border: none; padding: 6px 8px;"
            " font-weight: 600; text-decoration: underline; min-height: 20px; }"
            "QPushButton#HelpLearnMore:focus { outline: none; border: 2px solid %4; border-radius: 8px; }")
                          .arg(bg.name(), ink.name(), mix(ink, bg, 0.9).name(),
                               QApplication::palette().color(QPalette::Highlight).name()));
    }

    void paintEvent(QPaintEvent* e) override {
        // The rounded bubble itself (a translucent popup draws no frame).
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(bubbleBg());
        p.drawRoundedRect(QRectF(rect()), 12, 12);
        QFrame::paintEvent(e);
    }

protected:
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) {
            e->accept();
            button_->closePopover(true);
            return;
        }
        QFrame::keyPressEvent(e);
    }

    void mousePressEvent(QMouseEvent* e) override {
        // A click on the "?" itself only closes the popover (it shouldn't
        // reach the button and open it again).
        if (!rect().contains(e->position().toPoint()) && button_
            && QRect(button_->mapToGlobal(QPoint(0, 0)), button_->size())
                   .contains(e->globalPosition().toPoint()))
            setAttribute(Qt::WA_NoMouseReplay);
        QFrame::mousePressEvent(e);
    }

private:
    QPointer<HelpButton> button_;
};

HelpButton::HelpButton(const QString& key, theme::PrefsStore& store, QWidget* parent, QWidget* target)
    : QAbstractButton(parent), key_(key), store_(store), target_(target) {
    setObjectName(QStringLiteral("HelpIcon"));
    setProperty("helpKey", key);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    const auto entry = helpEntry(key);
    if (!entry) qWarning("HelpButton: no help text for \"%s\"", qPrintable(key));
    setAccessibleName(tr("Help: %1").arg(entry ? entry->title : key));
    if (entry) setAccessibleDescription(entry->shortText);
    connect(this, &QAbstractButton::clicked, this, [this] {
        if (isPopoverOpen()) closePopover(false);
        else showPopover();
    });
    connect(&store_, &theme::PrefsStore::changed, this, &HelpButton::applyPrefs);
    applyPrefs();
}

HelpButton::HelpButton(const QString& key, QWidget* parent, QWidget* target)
    : HelpButton(key, theme::PrefsStore::instance(), parent, target) {}

HelpButton::~HelpButton() {
    delete tip_;
}

HelpButton* HelpButton::addTo(QToolBar* toolbar, const QString& key, QWidget* target) {
    auto* b = new HelpButton(key, toolbar, target);
    b->setVisibilityAction(toolbar->addWidget(b));
    return b;
}

QWidget* HelpButton::target() const { return target_ ? target_.data() : parentWidget(); }

void HelpButton::setVisibilityAction(QAction* action) {
    visibilityAction_ = action;
    applyPrefs();
}

QLabel* HelpButton::tooltip() const { return tip_ && tip_->isVisible() ? tip_.data() : nullptr; }

QWidget* HelpButton::popover() const { return popover_ && popover_->isVisible() ? popover_.data() : nullptr; }

bool HelpButton::isPopoverOpen() const { return popover() != nullptr; }

QSize HelpButton::sizeHint() const { return { kCircle + 4, kCircle + 4 }; }

void HelpButton::applyPrefs() {
    // An unknown key never shows (HelpTextsTest catches those).
    const bool on = store_.prefs().helpIcons && helpEntry(key_).has_value();
    if (!on) {
        hideTip();
        closePopover(false);
    }
    if (visibilityAction_) {
        visibilityAction_->setVisible(on);
    } else if (!on) {
        hide();
    } else if (isHidden() && testAttribute(Qt::WA_WState_ExplicitShowHide) && parentWidget()) {
        show();
    }
}

void HelpButton::placeFloating(QWidget* box) const {
    box->adjustSize();
    const QPoint topLeft = mapToGlobal(QPoint(0, 0));
    const QRect anchor(topLeft, size());
    const QScreen* s = screen();
    const QRect area = s ? s->availableGeometry() : QRect(anchor.center() - QPoint(2000, 2000), QSize(4000, 4000));
    const QSize sz = box->size();
    int x = anchor.center().x() - sz.width() / 2;
    x = qBound(area.left() + kEdge, x, qMax(area.left() + kEdge, area.right() - kEdge - sz.width()));
    int y = anchor.bottom() + kGap;
    if (y + sz.height() > area.bottom() - kEdge && anchor.top() - kGap - sz.height() >= area.top() + kEdge)
        y = anchor.top() - kGap - sz.height();
    box->move(x, y);
}

void HelpButton::showTip() {
    if (isPopoverOpen() || !isVisible()) return;
    const auto entry = helpEntry(key_);
    if (!entry) return;
    if (!tip_) {
        tip_ = new TipBubble;
        tip_->setObjectName(QStringLiteral("HelpTooltip"));
    }
    tip_->setFont(font());
    QPalette pal = tip_->palette();
    pal.setColor(QPalette::WindowText, bubbleInk());
    tip_->setPalette(pal);
    tip_->setText(entry->shortText);
    // As wide as the sentence, up to kTipWidth, then it wraps.
    const QMargins m = tip_->contentsMargins();
    const QRect text = tip_->fontMetrics().boundingRect(QRect(0, 0, kTipWidth - m.left() - m.right(), 2000),
                                                        Qt::TextWordWrap, entry->shortText);
    tip_->setFixedSize(text.width() + m.left() + m.right() + 1, text.height() + m.top() + m.bottom());
    placeFloating(tip_);
    tip_->show();
    tip_->raise();
    update();
}

void HelpButton::hideTip() {
    if (tip_) tip_->hide();
    update();
}

void HelpButton::showPopover() {
    const auto entry = helpEntry(key_);
    if (!entry || isPopoverOpen()) return;
    hideTip();
    popover_ = new HelpPopover(this, *entry);
    popover_->setFont(font());
    connect(popover_, &QObject::destroyed, this, [this] { update(); });
    placeFloating(popover_);
    popover_->show();
    popover_->setFocus(Qt::PopupFocusReason);
    update();
}

void HelpButton::closePopover(bool refocus) {
    if (popover_) {
        popover_->close();
        popover_ = nullptr;
    }
    hovered_ = false;
    if (refocus) {
        quietFocus_ = true;
        setFocus(Qt::OtherFocusReason);
        quietFocus_ = false;
    }
    update();
}

void HelpButton::showMe() {
    QWidget* t = target();
    closePopover(true);
    if (t) pulseWidget(t);
}

void HelpButton::paintEvent(QPaintEvent*) {
    const QPalette& pal = palette();
    const bool lit = hovered_ || isDown() || isPopoverOpen() || (tip_ && tip_->isVisible());
    const QColor accentText = pal.color(QPalette::Link);
    const QColor panel = pal.color(QPalette::Window);
    const QColor muted = pal.color(QPalette::PlaceholderText);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF circle(QRectF(rect()).center() - QPointF(kCircle / 2.0, kCircle / 2.0), QSizeF(kCircle, kCircle));
    if (hasFocus() && focusRing_) {
        // The keyboard focus ring.
        p.setPen(QPen(pal.color(QPalette::Highlight), 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(circle.adjusted(-1.5, -1.5, 1.5, 1.5));
    }
    p.setPen(QPen(lit ? accentText : muted, 1.6));
    p.setBrush(lit ? mix(accentText, panel, 0.14) : panel);
    p.drawEllipse(circle.adjusted(1, 1, -1, -1));
    QFont f = font();
    f.setBold(true);
    f.setWeight(QFont::ExtraBold);
    f.setPixelSize(12);
    p.setFont(f);
    p.setPen(lit ? accentText : muted);
    p.drawText(circle, Qt::AlignCenter, QStringLiteral("?"));
}

void HelpButton::enterEvent(QEnterEvent* e) {
    QAbstractButton::enterEvent(e);
    hovered_ = true;
    showTip();
}

void HelpButton::leaveEvent(QEvent* e) {
    QAbstractButton::leaveEvent(e);
    hovered_ = false;
    if (!focusTip_) hideTip();
}

void HelpButton::focusInEvent(QFocusEvent* e) {
    QAbstractButton::focusInEvent(e);
    // A click focuses the button too; that shouldn't bring up the tooltip.
    const bool keyboard = e->reason() == Qt::TabFocusReason || e->reason() == Qt::BacktabFocusReason
                          || e->reason() == Qt::ShortcutFocusReason;
    focusTip_ = keyboard && !quietFocus_;
    focusRing_ = e->reason() != Qt::MouseFocusReason;
    if (focusTip_) showTip();
}

void HelpButton::focusOutEvent(QFocusEvent* e) {
    QAbstractButton::focusOutEvent(e);
    focusTip_ = false;
    if (!hovered_) hideTip();
}

void HelpButton::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        // Keep Esc from also closing the dialog underneath.
        const bool had = isPopoverOpen() || (tip_ && tip_->isVisible());
        if (isPopoverOpen()) closePopover(true);
        focusTip_ = false;
        hideTip();
        if (had) {
            e->accept();
            return;
        }
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        e->accept();
        click();
        return;
    }
    QAbstractButton::keyPressEvent(e);
}

void HelpButton::hideEvent(QHideEvent* e) {
    QAbstractButton::hideEvent(e);
    hideTip();
    if (popover_) closePopover(false);
}

void HelpButton::changeEvent(QEvent* e) {
    QAbstractButton::changeEvent(e);
    if (e->type() == QEvent::PaletteChange) update();
}

QWidget* withHelp(QWidget* control, const QString& key, QWidget* parent) {
    auto* row = new QWidget(parent ? parent : control->parentWidget());
    row->setObjectName(QStringLiteral("WithHelp"));
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(4);
    // Wrapping text takes the width; anything else keeps its size.
    const auto* label = qobject_cast<QLabel*>(control);
    const bool grows = label && label->wordWrap();
    h->addWidget(control, grows ? 1 : 0);
    h->addWidget(new HelpButton(key, row, control), 0, grows ? Qt::AlignTop : Qt::AlignVCenter);
    if (!grows) h->addStretch(1);
    return row;
}

QWidget* headingWithHelp(const QString& text, const QString& key, QWidget* dialog) {
    auto* row = new QWidget(dialog);
    row->setObjectName(QStringLiteral("HeadingWithHelp"));
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    auto* label = new QLabel(text, row);
    label->setObjectName(QStringLiteral("DialogHeading"));
    QFont f = label->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 1.15);
    label->setFont(f);
    h->addWidget(label);
    h->addWidget(new HelpButton(key, row, dialog), 0, Qt::AlignVCenter);
    h->addStretch(1);
    return row;
}

HelpButton* addToMessageBox(QMessageBox* box, const QString& key) {
    auto* b = new HelpButton(key, box, box);
    if (auto* grid = qobject_cast<QGridLayout*>(box->layout()))
        grid->addWidget(b, 0, grid->columnCount(), Qt::AlignTop | Qt::AlignRight);
    return b;
}

QWidget* pulseWidget(QWidget* target) {
    if (!target) return nullptr;
    QWidget* host = target->window();
    if (host == target) {
        // A whole window: outline it from the inside.
        auto* overlay = new PulseOverlay(target, host);
        overlay->setGeometry(host->rect());
        return overlay;
    }
    return new PulseOverlay(target, host);
}

}  // namespace bld::ui::help
