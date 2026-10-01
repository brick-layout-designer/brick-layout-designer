#pragma once

// Friendly, non-blocking notices over the map, at the top: "Version 1.3.0
// is ready · What's new · Download · Later", "This server suggests a newer
// version", "This server can't do everything yet". Each has an id; showing
// the same id again replaces it. Nothing here is a modal box.

#include <QFrame>
#include <QHash>
#include <QList>
#include <QString>

#include <functional>

class QVBoxLayout;

namespace bld::ui {

struct NoticeAction {
    QString text;
    std::function<void()> run;  // may be empty: the button only closes the notice
    bool accent = false;
    bool closes = true;  // the notice goes away once clicked
};

class NoticeArea : public QWidget {
    Q_OBJECT
public:
    // Floats over `over` (the map), at the top.
    explicit NoticeArea(QWidget* over);

    // Show (or replace) notice `id`. `details`, when given, is shown under a
    // "What's new" toggle. Important notices are marked and coloured.
    void showNotice(const QString& id, const QString& title, const QString& text,
                    const QList<NoticeAction>& actions, const QString& details = {}, bool important = false);
    void hideNotice(const QString& id);
    bool isShown(const QString& id) const;
    // The card for `id` (tests and screenshots), or nullptr.
    QFrame* card(const QString& id) const;

signals:
    // Closed with its ✕ or one of its buttons.
    void closed(const QString& id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void place();

    QWidget* over_;
    QVBoxLayout* column_;
    QHash<QString, QFrame*> cards_;
};

}  // namespace bld::ui
