#include "Tours.h"

#include "ui/help/HelpButton.h"
#include "ui/theme/AppPrefs.h"
#include "ui/theme/Tokens.h"

#include <QAbstractButton>
#include <QApplication>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStyle>
#include <QStyleOption>
#include <QTimer>
#include <QVBoxLayout>

static void initTourResources() { Q_INIT_RESOURCE(tours); }

namespace bld::ui::tours {

namespace {

constexpr int kPad = 6;     // the outline's gap round the control
constexpr int kEdge = 8;    // the card keeps this far from the window's edges
constexpr int kGap = 12;    // between the control and the card
constexpr int kCardWidth = 340;
constexpr int kFollowMs = 250;
constexpr int kLookTries = 8;  // two seconds to find a control that is still opening

const char* const kTargetProperty = "tourTarget";
const char* const kFallbackProperty = "tourTargetFallback";

theme::Mode modeOf(const QWidget* w) {
    return w->palette().color(QPalette::Window).lightness() < 128 ? theme::Mode::Dark : theme::Mode::Light;
}

bool showing(const QWidget* w) { return w && w->isVisible() && w->width() > 0 && w->height() > 0; }

// The card: Tab and Shift+Tab go round its buttons, Esc ends the tour,
// and no key reaches the window underneath.
class StepCard : public QFrame {
public:
    StepCard(TourOverlay* overlay, QWidget* parent) : QFrame(parent), overlay_(overlay) {}

protected:
    bool focusNextPrevChild(bool next) override {
        QList<QWidget*> items;
        for (auto* b : findChildren<QPushButton*>())
            if (b->isVisible()) items.append(b);
        if (items.isEmpty()) return false;
        const auto at = items.indexOf(QApplication::focusWidget());
        const auto n = items.size();
        const auto to = next ? (at + 1) % n : (at <= 0 ? n - 1 : at - 1);
        items.at(to)->setFocus(next ? Qt::TabFocusReason : Qt::BacktabFocusReason);
        return true;
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) {
            // Queued: this card goes away with the tour.
            QMetaObject::invokeMethod(overlay_, &TourOverlay::finish, Qt::QueuedConnection);
        }
        e->accept();
    }

private:
    TourOverlay* overlay_;
};

// One of the welcome's ways in: a big button with a bold name and a line
// under it. (A QPushButton won't size itself to the labels inside it.)
class WelcomeTile : public QAbstractButton {
public:
    explicit WelcomeTile(QWidget* parent) : QAbstractButton(parent) {}
    QSize sizeHint() const override { return layout() ? layout()->sizeHint() : QAbstractButton::sizeHint(); }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QStyleOption opt;
        opt.initFrom(this);
        QPainter p(this);
        style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
    }
};

}  // namespace

// ---- the catalogue --------------------------------------------------------

Catalogue parseCatalogue(const QByteArray& json) {
    Catalogue c;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const QJsonObject w = root.value(QLatin1String("welcome")).toObject();
    const QJsonObject actions = w.value(QLatin1String("actions")).toObject();
    const auto action = [&actions](const char* key, const char* field) {
        return actions.value(QLatin1String(key)).toObject().value(QLatin1String(field)).toString();
    };
    c.welcome = { w.value(QLatin1String("id")).toString(),   w.value(QLatin1String("title")).toString(),
                  w.value(QLatin1String("text")).toString(), action("layout", "label"),
                  action("layout", "text"),                  action("club", "label"),
                  action("club", "text"),                    action("tour", "label"),
                  action("tour", "text"),                    w.value(QLatin1String("dismiss")).toString() };
    const QJsonObject b = root.value(QLatin1String("buttons")).toObject();
    c.buttons = { b.value(QLatin1String("next")).toString(), b.value(QLatin1String("back")).toString(),
                  b.value(QLatin1String("skip")).toString(), b.value(QLatin1String("done")).toString(),
                  b.value(QLatin1String("of")).toString() };
    for (const auto& tv : root.value(QLatin1String("tours")).toArray()) {
        const QJsonObject t = tv.toObject();
        Tour tour{ t.value(QLatin1String("id")).toString(), t.value(QLatin1String("title")).toString(),
                   t.value(QLatin1String("devices")).toString(), {} };
        for (const auto& sv : t.value(QLatin1String("steps")).toArray()) {
            const QJsonObject s = sv.toObject();
            tour.steps.append({ s.value(QLatin1String("target")).toString(), s.value(QLatin1String("title")).toString(),
                                s.value(QLatin1String("text")).toString() });
        }
        c.tours.append(tour);
    }
    return c;
}

Catalogue translated(Catalogue c) {
    // tours.json is the web's file word for word, so its text can't carry
    // tr(); TourStrings.cpp marks the same strings for lupdate.
    const auto tr = [](QString& s) {
        if (!s.isEmpty()) s = QCoreApplication::translate("Tours", s.toUtf8().constData());
    };
    Welcome& w = c.welcome;
    for (QString* s : { &w.title, &w.text, &w.layoutLabel, &w.layoutText, &w.clubLabel, &w.clubText, &w.tourLabel,
                        &w.tourText, &w.dismiss })
        tr(*s);
    Buttons& b = c.buttons;
    for (QString* s : { &b.next, &b.back, &b.skip, &b.done, &b.of }) tr(*s);
    for (Tour& t : c.tours) {
        tr(t.title);
        for (Step& s : t.steps) {
            tr(s.title);
            tr(s.text);
        }
    }
    return c;
}

const Catalogue& catalogue() {
    static const Catalogue c = [] {
        initTourResources();
        QFile f(QStringLiteral(":/bld/tours/tours.json"));
        return f.open(QIODevice::ReadOnly) ? translated(parseCatalogue(f.readAll())) : Catalogue{};
    }();
    return c;
}

const Tour* findTour(const QString& id) {
    for (const Tour& t : catalogue().tours)
        if (t.id == id) return &t;
    return nullptr;
}

QList<const Tour*> desktopTours() {
    QList<const Tour*> out;
    for (const Tour& t : catalogue().tours)
        if (t.devices != QLatin1String("phone")) out.append(&t);
    return out;
}

QString stepCount(int n, int total) {
    QString s = catalogue().buttons.of;
    return s.replace(QLatin1String("{n}"), QString::number(n)).replace(QLatin1String("{total}"), QString::number(total));
}

// ---- targets and the seen list ----------------------------------------------

void tag(QWidget* w, const QString& id, bool fallback) {
    if (w) w->setProperty(fallback ? kFallbackProperty : kTargetProperty, id);
}

QWidget* findTarget(QWidget* host, const QString& id) {
    if (!host) return nullptr;
    const auto all = host->findChildren<QWidget*>();
    for (auto* w : all)
        if (w->property(kTargetProperty).toString() == id && showing(w)) return w;
    for (auto* w : all) {
        auto* b = qobject_cast<help::HelpButton*>(w);
        if (b && b->key() == id && showing(b->target())) return b->target();
    }
    for (auto* w : all)
        if (w->property(kFallbackProperty).toString() == id && showing(w)) return w;
    return nullptr;
}

bool seen(const theme::PrefsStore& store, const QString& id) { return store.prefs().toursSeen.contains(id); }

void markSeen(theme::PrefsStore& store, const QString& id) {
    theme::AppPrefs p = store.prefs();
    if (p.toursSeen.contains(id)) return;
    p.toursSeen.append(id);
    store.update(p);
}

void forgetSeen(theme::PrefsStore& store) {
    theme::AppPrefs p = store.prefs();
    p.toursSeen.clear();
    store.update(p);
}

// ---- the overlay ----------------------------------------------------------

TourOverlay::TourOverlay(QWidget* host, const Tour& tour, theme::PrefsStore& store)
    : QWidget(host), host_(host), tour_(tour), store_(store) {
    setObjectName(QStringLiteral("TourOverlay"));
    setAttribute(Qt::WA_NoSystemBackground);
    setGeometry(host->rect());
    host->installEventFilter(this);

    focusBefore_ = QApplication::focusWidget();
    if (focusBefore_)
        connect(focusBefore_, &QObject::destroyed, this, [this] { focusBefore_ = nullptr; });

    const theme::Neutrals& n = theme::neutrals(modeOf(host));
    auto* card = new StepCard(this, this);
    card_ = card;
    card->setObjectName(QStringLiteral("TourCard"));
    card->setAccessibleName(tour.title);
    card->setFixedWidth(qMin(kCardWidth, qMax(200, host->width() - 2 * kEdge)));
    card->setStyleSheet(QStringLiteral(
        "QFrame#TourCard { background: %1; border-radius: %4px; }"
        "QFrame#TourCard QLabel { color: %2; background: transparent; }"
        "QFrame#TourCard QLabel#TourCount { color: %3; font-weight: 600; }"
        "QFrame#TourCard QPushButton { border: none; border-radius: 8px; padding: 6px 12px; font-weight: 700;"
        " background: transparent; color: %2; min-height: 22px; }"
        "QFrame#TourCard QPushButton#TourSkip { color: %3; }"
        "QFrame#TourCard QPushButton#TourNext { background: %2; color: %1; }"
        "QFrame#TourCard QPushButton:focus { outline: none; border: 2px solid %3; }")
                            .arg(n.tourBg.name(), n.tourInk.name(), n.tourMuted.name())
                            .arg(theme::Radius::card));
    auto* col = new QVBoxLayout(card);
    col->setContentsMargins(16, 14, 16, 14);
    col->setSpacing(6);
    count_ = new QLabel(card);
    count_->setObjectName(QStringLiteral("TourCount"));
    title_ = new QLabel(card);
    title_->setObjectName(QStringLiteral("TourTitle"));
    QFont tf = QApplication::font();
    tf.setFamily(QLatin1String(theme::kHeadingFamily));
    tf.setWeight(QFont::Bold);
    if (tf.pointSizeF() > 0) tf.setPointSizeF(tf.pointSizeF() * 1.3);
    title_->setFont(tf);
    title_->setWordWrap(true);
    text_ = new QLabel(card);
    text_->setObjectName(QStringLiteral("TourText"));
    text_->setWordWrap(true);
    col->addWidget(count_);
    col->addWidget(title_);
    col->addWidget(text_);
    auto* row = new QHBoxLayout;
    row->setSpacing(6);
    const Buttons& words = catalogue().buttons;
    skip_ = new QPushButton(words.skip, card);
    skip_->setObjectName(QStringLiteral("TourSkip"));
    back_ = new QPushButton(words.back, card);
    back_->setObjectName(QStringLiteral("TourBack"));
    next_ = new QPushButton(words.next, card);
    next_->setObjectName(QStringLiteral("TourNext"));
    for (auto* b : { skip_, back_, next_ }) {
        b->setAutoDefault(false);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::StrongFocus);
    }
    row->addWidget(skip_);
    row->addStretch(1);
    row->addWidget(back_);
    row->addWidget(next_);
    col->addLayout(row);
    // Queued: a click never deletes the button it came from.
    connect(skip_, &QPushButton::clicked, this, [this] {
        QMetaObject::invokeMethod(this, &TourOverlay::finish, Qt::QueuedConnection);
    });
    connect(back_, &QPushButton::clicked, this, &TourOverlay::back);
    connect(next_, &QPushButton::clicked, this, [this] {
        if (step_ == tour_.steps.size() - 1)
            QMetaObject::invokeMethod(this, &TourOverlay::finish, Qt::QueuedConnection);
        else
            next();
    });

    timer_ = new QTimer(this);
    timer_->setInterval(kFollowMs);
    connect(timer_, &QTimer::timeout, this, &TourOverlay::follow);
    timer_->start();

    raise();
    show();
    showStep();
}

void TourOverlay::showStep() {
    const Step& s = tour_.steps.at(step_);
    const bool last = step_ == tour_.steps.size() - 1;
    count_->setText(tour_.title + QStringLiteral(" · ") + stepCount(step_ + 1, static_cast<int>(tour_.steps.size())));
    title_->setText(s.title);
    text_->setText(s.text);
    card_->setAccessibleName(s.title);
    card_->setAccessibleDescription(s.text);
    skip_->setVisible(!last);
    back_->setVisible(step_ > 0);
    next_->setText(last ? catalogue().buttons.done : catalogue().buttons.next);
    tries_ = 0;
    target_ = nullptr;
    follow();
    next_->setFocus(Qt::OtherFocusReason);
}

void TourOverlay::follow() {
    if (done_) return;
    QWidget* t = findTarget(host_, tour_.steps.at(step_).target);
    if (!t && tries_ < kLookTries) ++tries_;
    if (t != target_) {
        // Forget the control if it goes away (one watch at a time).
        disconnect(targetGone_);
        target_ = t;
        if (t) targetGone_ = connect(t, &QObject::destroyed, this, [this] { target_ = nullptr; });
    }
    place();
}

void TourOverlay::place() {
    setGeometry(host_->rect());
    QRect hole;
    if (target_) {
        const QPoint topLeft = mapFromGlobal(target_->mapToGlobal(QPoint(0, 0)));
        hole = QRect(topLeft, target_->size()).adjusted(-kPad, -kPad, kPad, kPad).intersected(rect());
    }
    if (hole != hole_) {
        hole_ = hole;
        update();
    }
    card_->adjustSize();
    const QSize c = card_->size();
    const QRect r = rect();
    const auto clampX = [&](int x) { return qBound(kEdge, x, qMax(kEdge, r.width() - c.width() - kEdge)); };
    const auto clampY = [&](int y) { return qBound(kEdge, y, qMax(kEdge, r.height() - c.height() - kEdge)); };
    QPoint at;
    if (hole_.isEmpty())
        at = QPoint((r.width() - c.width()) / 2, (r.height() - c.height()) / 2);
    else if (hole_.bottom() + kGap + c.height() <= r.height() - kEdge)
        at = QPoint(clampX(hole_.left()), hole_.bottom() + kGap);
    else if (hole_.top() - kGap - c.height() >= kEdge)
        at = QPoint(clampX(hole_.left()), hole_.top() - kGap - c.height());
    else if (hole_.right() + kGap + c.width() <= r.width() - kEdge)
        at = QPoint(hole_.right() + kGap, clampY(hole_.top()));
    else if (hole_.left() - kGap - c.width() >= kEdge)
        at = QPoint(hole_.left() - kGap - c.width(), clampY(hole_.top()));
    else
        at = QPoint(clampX(hole_.left()), clampY(hole_.bottom() - c.height()));
    card_->move(at);
}

void TourOverlay::next() {
    if (done_ || step_ >= tour_.steps.size() - 1) return;
    ++step_;
    showStep();
}

void TourOverlay::back() {
    if (done_ || step_ == 0) return;
    --step_;
    showStep();
}

void TourOverlay::finish() {
    if (done_) return;
    done_ = true;
    timer_->stop();
    host_->removeEventFilter(this);
    markSeen(store_, tour_.id);
    hide();
    if (focusBefore_) focusBefore_->setFocus(Qt::OtherFocusReason);
    emit finished(tour_.id);
    deleteLater();
}

void TourOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath dim;
    dim.addRect(rect());
    if (!hole_.isEmpty()) {
        QPainterPath cut;
        cut.addRoundedRect(QRectF(hole_), 10, 10);
        dim = dim.subtracted(cut);
    }
    p.fillPath(dim, QColor(20, 22, 25, 128));
    if (!hole_.isEmpty()) {
        p.setPen(QPen(theme::accent(store_.prefs().accent).main, 4));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(hole_).adjusted(-2, -2, 2, 2), 11, 11);
    }
}

bool TourOverlay::eventFilter(QObject* watched, QEvent* e) {
    if (watched == host_ && (e->type() == QEvent::Resize || e->type() == QEvent::LayoutRequest)) place();
    return QWidget::eventFilter(watched, e);
}

void TourOverlay::mousePressEvent(QMouseEvent* e) {
    // The window underneath waits while the tour is open.
    e->accept();
    next_->setFocus(Qt::MouseFocusReason);
}

TourOverlay* startTour(QWidget* host, const QString& id, theme::PrefsStore& store) {
    const Tour* t = findTour(id);
    if (!host || !t || t->steps.isEmpty()) return nullptr;
    for (auto* old : host->findChildren<TourOverlay*>(Qt::FindDirectChildrenOnly)) old->finish();
    // The host owns it (it deletes itself when the tour ends).
    auto* overlay = new TourOverlay(host, *t, store);
    overlay->raise();
    return overlay;
}

namespace {

// Waits for the next window of a kind to open, then starts the tour on it.
class StartOnShow : public QObject {
public:
    StartOnShow(const char* className, QString id, theme::PrefsStore& store)
        : QObject(qApp), className_(className), id_(std::move(id)), store_(store) {
        qApp->installEventFilter(this);
        QTimer::singleShot(5000, this, [this] { deleteLater(); });
    }

protected:
    bool eventFilter(QObject* watched, QEvent* e) override {
        if (e->type() == QEvent::Show && !fired_) {
            auto* w = qobject_cast<QWidget*>(watched);
            if (w && w->isWindow() && w->inherits(className_)) {
                fired_ = true;
                // Once the window has laid itself out.
                QTimer::singleShot(0, w, [w, id = id_, &store = store_] { startTour(w, id, store); });
                deleteLater();
            }
        }
        return false;
    }

private:
    const char* className_;
    QString id_;
    theme::PrefsStore& store_;
    bool fired_ = false;
};

}  // namespace

void startTourOnNext(const char* className, const QString& id, theme::PrefsStore& store) {
    new StartOnShow(className, id, store);
}

// ---- the welcome ----------------------------------------------------------

WelcomeDialog::WelcomeDialog(QWidget* parent) : QDialog(parent) {
    const Welcome& w = catalogue().welcome;
    setObjectName(QStringLiteral("WelcomeDialog"));
    setWindowTitle(w.title);
    const theme::Mode mode = modeOf(this);
    const theme::Neutrals& n = theme::neutrals(mode);
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(24, 22, 24, 20);
    setMinimumWidth(520);
    col->setSpacing(12);
    auto* title = new QLabel(w.title, this);
    title->setObjectName(QStringLiteral("WelcomeTitle"));
    QFont tf = QApplication::font();
    tf.setFamily(QLatin1String(theme::kHeadingFamily));
    tf.setWeight(QFont::Bold);
    if (tf.pointSizeF() > 0) tf.setPointSizeF(tf.pointSizeF() * 1.6);
    title->setFont(tf);
    title->setWordWrap(true);
    auto* text = new QLabel(w.text, this);
    text->setObjectName(QStringLiteral("WelcomeText"));
    text->setWordWrap(true);
    col->addWidget(title);
    col->addWidget(text);

    // The three ways in, one under the other.
    auto* row = new QVBoxLayout;
    row->setSpacing(10);
    const auto tile = [&](const QString& name, const QString& label, const QString& about, Choice c) {
        auto* b = new WelcomeTile(this);
        b->setObjectName(name);
        b->setAccessibleName(label);
        b->setAccessibleDescription(about);
        b->setFocusPolicy(Qt::StrongFocus);
        b->setCursor(Qt::PointingHandCursor);
        auto* inner = new QVBoxLayout(b);
        inner->setContentsMargins(14, 12, 14, 12);
        auto* l = new QLabel(label, b);
        QFont lf = tf;
        if (lf.pointSizeF() > 0) lf.setPointSizeF(QApplication::font().pointSizeF() * 1.2);
        l->setFont(lf);
        auto* a = new QLabel(about, b);
        for (auto* lab : { l, a }) lab->setAttribute(Qt::WA_TransparentForMouseEvents);
        inner->addWidget(l);
        inner->addWidget(a);
        if (c != Choice::None) {
            connect(b, &QAbstractButton::clicked, this, [this, c] {
                choice_ = c;
                accept();
            });
        }
        row->addWidget(b);
        return b;
    };
    auto* layout = tile(QStringLiteral("WelcomeLayout"), w.layoutLabel, w.layoutText, Choice::None);
    // Its three ways, shown under it once it's picked.
    auto* ways = new QWidget(this);
    ways->setObjectName(QStringLiteral("WelcomeLayoutWays"));
    auto* waysRow = new QHBoxLayout(ways);
    waysRow->setContentsMargins(14, 0, 14, 0);
    waysRow->setSpacing(8);
    const auto way = [&](const QString& name, const QString& label, Choice c) {
        auto* b = new QPushButton(label, ways);
        b->setObjectName(name);
        b->setAutoDefault(false);
        connect(b, &QPushButton::clicked, this, [this, c] {
            choice_ = c;
            accept();
        });
        waysRow->addWidget(b);
        return b;
    };
    auto* fresh = way(QStringLiteral("WelcomeNewLayout"), tr("New layout"), Choice::NewLayout);
    way(QStringLiteral("WelcomeOpenFile"), tr("Open a file…"), Choice::OpenFile);
#ifdef BLD_SYNC
    way(QStringLiteral("WelcomeServer"), tr("Connect to a server…"), Choice::Server);
#endif
    waysRow->addStretch(1);
    ways->hide();
    row->addWidget(ways);
    connect(layout, &QAbstractButton::clicked, this, [this, ways, fresh] {
        ways->show();
        fresh->setFocus(Qt::OtherFocusReason);
        adjustSize();
    });
    tile(QStringLiteral("WelcomeClub"), w.clubLabel, w.clubText, Choice::Club);
    tile(QStringLiteral("WelcomeTour"), w.tourLabel, w.tourText, Choice::Tour);
    col->addLayout(row);

    auto* bottom = new QHBoxLayout;
    bottom->addStretch(1);
    auto* notNow = new QPushButton(w.dismiss, this);
    notNow->setObjectName(QStringLiteral("WelcomeDismiss"));
    notNow->setAutoDefault(false);
    connect(notNow, &QPushButton::clicked, this, &QDialog::reject);
    bottom->addWidget(notNow);
    col->addLayout(bottom);

    const theme::Accent& accent = theme::accent(theme::PrefsStore::instance().prefs().accent);
    setStyleSheet(QStringLiteral(
        "QAbstractButton#WelcomeLayout, QAbstractButton#WelcomeClub, QAbstractButton#WelcomeTour {"
        " border-radius: %1px; background: %2; border: 1px solid %3; }"
        "QAbstractButton#WelcomeLayout { border: 2px solid %4; }"
        "QAbstractButton#WelcomeTour { background: %5; border: 2px solid %5; }"
        "QAbstractButton#WelcomeClub:focus, QAbstractButton#WelcomeTour:focus, QAbstractButton#WelcomeLayout:focus {"
        " border: 2px solid %4; }"
        "QAbstractButton#WelcomeTour QLabel { color: %6; }"
        "QLabel#WelcomeText { color: %7; }")
                      .arg(theme::Radius::card)
                      .arg(n.panel.name(), n.line.name(), accent.main.name(), n.tourBg.name(), n.tourInk.name(),
                           n.muted.name()));
    layout->setFocus(Qt::OtherFocusReason);
}

}  // namespace bld::ui::tours
