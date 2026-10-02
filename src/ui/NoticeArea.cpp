#include "NoticeArea.h"

#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace bld::ui {

NoticeArea::NoticeArea(QWidget* over) : QWidget(over), over_(over) {
    setObjectName(QStringLiteral("NoticeArea"));
    column_ = new QVBoxLayout(this);
    column_->setContentsMargins(12, 0, 12, 24);  // room for the cards' shadows
    column_->setSpacing(10);
    over->installEventFilter(this);
    hide();
}

void NoticeArea::showNotice(const QString& id, const QString& title, const QString& text,
                            const QList<NoticeAction>& actions, const QString& details, bool important) {
    // Replacing a notice: the old card goes once this call (maybe one of its
    // own buttons' signals) has returned.
    if (QFrame* old = cards_.take(id)) {
        old->hide();
        old->deleteLater();
    }
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("NoticeCard"));
    card->setProperty("important", important);
    card->setAutoFillBackground(true);
    card->setAccessibleName(title);
    auto* shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(30);
    shadow->setOffset(0, 12);
    shadow->setColor(QColor(30, 33, 36, 36));
    card->setGraphicsEffect(shadow);

    auto* col = new QVBoxLayout(card);
    col->setContentsMargins(16, 12, 12, 12);
    col->setSpacing(6);
    auto* head = new QHBoxLayout;
    auto* titleLabel = new QLabel(card);
    titleLabel->setObjectName(QStringLiteral("NoticeTitle"));
    titleLabel->setWordWrap(true);
    titleLabel->setText(important ? tr("Important: %1").arg(title) : title);
    head->addWidget(titleLabel, 1);
    auto* close = new QToolButton(card);
    close->setObjectName(QStringLiteral("NoticeClose"));
    close->setText(QStringLiteral("✕"));
    close->setToolTip(tr("Hide this message"));
    close->setAccessibleName(tr("Hide this message"));
    close->setCursor(Qt::PointingHandCursor);
    head->addWidget(close, 0, Qt::AlignTop);
    col->addLayout(head);

    if (!text.isEmpty()) {
        auto* body = new QLabel(text, card);
        body->setObjectName(QStringLiteral("NoticeText"));
        body->setWordWrap(true);
        body->setTextFormat(Qt::PlainText);
        col->addWidget(body);
    }

    QScrollArea* notes = nullptr;
    if (!details.isEmpty()) {
        notes = new QScrollArea(card);
        notes->setObjectName(QStringLiteral("NoticeDetails"));
        notes->setWidgetResizable(true);
        notes->setFrameShape(QFrame::NoFrame);
        notes->setMaximumHeight(200);
        auto* notesText = new QLabel(details, notes);
        notesText->setWordWrap(true);
        notesText->setTextFormat(Qt::MarkdownText);
        notesText->setOpenExternalLinks(true);
        notesText->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        notes->setWidget(notesText);
        notes->hide();
        col->addWidget(notes);
    }

    // The buttons sit in a row of their own, which place() turns into a
    // column when they don't fit the card side by side.
    auto* buttonRow = new QWidget(card);
    buttonRow->setObjectName(QStringLiteral("NoticeButtons"));
    auto* buttons = new QBoxLayout(QBoxLayout::LeftToRight, buttonRow);
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(6);
    if (notes) {
        auto* whatsNew = new QPushButton(tr("What's new"), buttonRow);
        whatsNew->setObjectName(QStringLiteral("NoticeWhatsNew"));
        whatsNew->setCheckable(true);
        whatsNew->setCursor(Qt::PointingHandCursor);
        connect(whatsNew, &QPushButton::toggled, this, [this, notes](bool on) {
            notes->setVisible(on);
            place();
        });
        buttons->addWidget(whatsNew);
    }
    buttons->addStretch(1);
    for (const NoticeAction& a : actions) {
        auto* b = new QPushButton(a.text, buttonRow);
        b->setProperty("accent", a.accent);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, [this, id, a] {
            // Run after the click's own signal: an action may open a dialog
            // or replace this notice.
            QTimer::singleShot(0, this, [this, id, a] {
                if (a.closes) {
                    hideNotice(id);
                    emit closed(id);
                }
                if (a.run) a.run();
            });
        });
        buttons->addWidget(b);
    }
    col->addWidget(buttonRow);
    connect(close, &QToolButton::clicked, this, [this, id] {
        QTimer::singleShot(0, this, [this, id] {
            hideNotice(id);
            emit closed(id);
        });
    });

    column_->addWidget(card);
    cards_.insert(id, card);
    card->show();
    show();
    raise();
    place();
}

void NoticeArea::hideNotice(const QString& id) {
    if (QFrame* card = cards_.take(id)) {
        card->hide();
        card->deleteLater();
    }
    if (cards_.isEmpty()) hide();
    else place();
}

bool NoticeArea::isShown(const QString& id) const { return cards_.contains(id); }

QFrame* NoticeArea::card(const QString& id) const { return cards_.value(id, nullptr); }

bool NoticeArea::eventFilter(QObject* watched, QEvent* event) {
    if (watched == over_ && event->type() == QEvent::Resize) place();
    return false;
}

void NoticeArea::place() {
    if (cards_.isEmpty()) return;
    const int w = std::max(0, std::min(480, over_->width() - 16));
    // Buttons too wide to sit side by side (long names, a server's address)
    // go one under another rather than being cut off. Measured as styled,
    // against the room the card really has.
    const QMargins area = layout() ? layout()->contentsMargins() : QMargins();
    for (QFrame* card : std::as_const(cards_)) {
        auto* row = card->findChild<QWidget*>(QStringLiteral("NoticeButtons"));
        auto* box = row ? qobject_cast<QBoxLayout*>(row->layout()) : nullptr;
        if (!box) continue;
        int need = 0;
        int count = 0;
        for (auto* b : row->findChildren<QPushButton*>(Qt::FindDirectChildrenOnly)) {
            b->ensurePolished();
            need += b->sizeHint().width();
            ++count;
        }
        need += box->spacing() * std::max(0, count - 1);
        card->ensurePolished();
        const QMargins inner = card->contentsMargins() + (card->layout() ? card->layout()->contentsMargins() : QMargins());
        const int room = w - area.left() - area.right() - inner.left() - inner.right();
        box->setDirection(need > room ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    }
    layout()->activate();
    const int h = std::min(heightForWidth(w) > 0 ? heightForWidth(w) : sizeHint().height(), over_->height() - 60);
    resize(w, std::max(0, h));
    // Under the "Showing <view>" pill, like the loading card.
    move((over_->width() - w) / 2, 52);
    raise();
}

}  // namespace bld::ui
