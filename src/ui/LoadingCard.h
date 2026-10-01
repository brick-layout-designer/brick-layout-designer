#pragma once

// The loading card, like the web's: "Opening layout…" with a busy bar,
// "Loading part pictures… 132 of 480" with a real progress bar, or
// "3 pictures couldn't load" with Retry. Floats over its parent (the map),
// or sits in a layout (the parts browser).

#include <QFrame>
#include <QString>

class QLabel;
class QProgressBar;
class QPushButton;
class QToolButton;

namespace bld::ui {

class LoadingCard : public QFrame {
    Q_OBJECT
public:
    // Where a floating card sits over its parent.
    enum class Place { Inline, Centre, Top, Bottom };

    explicit LoadingCard(QWidget* parent, Place place = Place::Inline);

    // "Opening layout…": a busy bar, no number yet.
    void showBusy(const QString& title, const QString& detail = {});
    // "Loading part pictures…  132 of 480" and a bar at done / total.
    void showProgress(const QString& title, int done, int total, const QString& detail = {});
    // "3 pictures couldn't load" with Retry and a hide button.
    void showFailed(const QString& text);
    void finish();
    // Lays out and paints the card now, before control returns to the event loop.
    void paintNow();
    void setPlace(Place where);

    // "1,204 of 2,853" in the user's number format.
    static QString countText(int done, int total);
    // "1 picture" / "3 pictures".
    static QString pictures(int n);

    QString title() const;
    QString count() const;
    bool failureShown() const;
    QProgressBar* bar() const { return bar_; }
    QPushButton* retryButton() const { return retry_; }

signals:
    void retryRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void showAs(bool failed);
    void place();

    Place place_;
    QLabel* title_ = nullptr;
    QLabel* count_ = nullptr;
    QLabel* detail_ = nullptr;
    QProgressBar* bar_ = nullptr;
    QPushButton* retry_ = nullptr;
    QToolButton* hide_ = nullptr;
};

}  // namespace bld::ui
