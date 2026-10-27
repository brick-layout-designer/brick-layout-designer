#pragma once

// "Update available" over the bottom of the map for placed custom sets
// whose set changed since they were placed (edit/SetUpdates.h): which ones,
// with Show, Update and "not now", as the web's SetUpdatesBar.tsx. Nothing
// updates by itself; once closed it comes back when a set changes again.

#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;

namespace bld::ui {

class SetUpdatesBar : public QWidget {
    Q_OBJECT
public:
    explicit SetUpdatesBar(QWidget* parent);

    // The sets' names (one per placed copy) and a key for these versions;
    // nothing hides the bar. A key closed with "not now" stays hidden.
    void setUpdates(const QStringList& names, const QString& key);
    // Keeps the bar centred along the bottom of `area` (parent coords).
    void place(const QRect& area);

    QString text() const;
    QPushButton* showButton() const { return show_; }
    QPushButton* updateButton() const { return update_; }
    QPushButton* closeButton() const { return close_; }

signals:
    void showRequested();
    void updateRequested();

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    QLabel* text_ = nullptr;
    QPushButton* show_ = nullptr;
    QPushButton* update_ = nullptr;
    QPushButton* close_ = nullptr;
    QString key_;
    QString closedFor_;
};

} // namespace bld::ui
