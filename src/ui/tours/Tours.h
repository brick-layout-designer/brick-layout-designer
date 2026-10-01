#pragma once

// The first-launch welcome and the guided tours, the desktop twin of the
// web's apps/web/src/tours. The words come from tours.json, an identical
// copy of the web app's file, so both apps have the same tour ids and the
// same step text (ToursTest checks the copy and that every step finds its
// control here).
//
// A tour is a few short steps. Each step outlines a real control (its
// `target`: a widget whose "tourTarget" property names it, else the
// control a "?" with that help key explains) and a small card beside it
// says what it's for, with Back / Next / Skip. Esc closes, Tab stays in
// the card, and finishing or skipping remembers the tour in the synced
// settings' toursSeen. A step whose control isn't showing puts its card
// in the middle.

#include <QDialog>
#include <QList>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;

namespace bld::ui::theme {
class PrefsStore;
}

namespace bld::ui::tours {

struct Step {
    QString target;
    QString title;
    QString text;
};

struct Tour {
    QString id;
    QString title;
    QString devices;  // "phone", "large" or "all"
    QList<Step> steps;
};

struct Welcome {
    QString id, title, text;
    QString layoutLabel, layoutText, clubLabel, clubText, tourLabel, tourText;
    QString dismiss;
};

struct Buttons {
    QString next, back, skip, done, of;
};

struct Catalogue {
    Welcome welcome;
    Buttons buttons;
    QList<Tour> tours;
};

// Reads a tours.json; empty on bad input.
Catalogue parseCatalogue(const QByteArray& json);
// The app's catalogue (the embedded tours.json).
const Catalogue& catalogue();
const Tour* findTour(const QString& id);
// The tours offered here: not the phone-only ones.
QList<const Tour*> desktopTours();
// "2 of 5".
QString stepCount(int n, int total);

// The widget a step points at inside `host`, or null when it isn't showing.
QWidget* findTarget(QWidget* host, const QString& id);
// Tag `w` as the control a tour step called `id` points at. A fallback
// is used only while nothing else for `id` is showing (a narrow window
// folds toolbar buttons away).
void tag(QWidget* w, const QString& id, bool fallback = false);

bool seen(const theme::PrefsStore& store, const QString& id);
void markSeen(theme::PrefsStore& store, const QString& id);
// Forget every finished tour and the welcome (Settings › Show tours again).
void forgetSeen(theme::PrefsStore& store);

// The card and the dimmed page with a hole round the control. It covers
// `host` (a window or dialog) and deletes itself when the tour ends.
class TourOverlay : public QWidget {
    Q_OBJECT
public:
    TourOverlay(QWidget* host, const Tour& tour, theme::PrefsStore& store);

    const Tour& tour() const { return tour_; }
    int step() const { return step_; }
    QWidget* card() const { return card_; }
    // The control outlined now, or null.
    QWidget* target() const { return target_; }
    // The outline, in this widget's coordinates (empty when centred).
    QRect hole() const { return hole_; }

    void next();
    void back();
    // Skip, Esc or Done: remembers the tour and goes away.
    void finish();

signals:
    void finished(const QString& id);

protected:
    void paintEvent(QPaintEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;

private:
    void showStep();
    void follow();
    void place();

    QWidget* host_;
    Tour tour_;
    theme::PrefsStore& store_;
    int step_ = 0;
    int tries_ = 0;
    bool done_ = false;
    QWidget* target_ = nullptr;
    QMetaObject::Connection targetGone_;
    QRect hole_;
    QWidget* focusBefore_ = nullptr;
    QWidget* card_ = nullptr;
    QLabel* count_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* text_ = nullptr;
    QPushButton* skip_ = nullptr;
    QPushButton* back_ = nullptr;
    QPushButton* next_ = nullptr;
    QTimer* timer_ = nullptr;
};

// Starts tour `id` over `host`. Null for an unknown id.
TourOverlay* startTour(QWidget* host, const QString& id, theme::PrefsStore& store);
// Starts tour `id` on the next window that opens and inherits `className`
// (a dialog about to be opened). Gives up after a few seconds.
void startTourOnNext(const char* className, const QString& id, theme::PrefsStore& store);

// The first-launch welcome: what the app is for, and three ways in.
class WelcomeDialog : public QDialog {
    Q_OBJECT
public:
    enum class Choice { None, Layout, Club, Tour };
    explicit WelcomeDialog(QWidget* parent = nullptr);
    Choice choice() const { return choice_; }

private:
    Choice choice_ = Choice::None;
};

}  // namespace bld::ui::tours
