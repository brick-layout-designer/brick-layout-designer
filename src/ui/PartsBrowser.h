#pragma once

#include <QDockWidget>
#include <QPoint>
#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

class QComboBox;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QTimer;
class QTouchEvent;

class QPushButton;

namespace bld::core  { class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::ui {

class BudgetSession;

// Dock panel showing parts in a thumbnail grid (QListView::IconMode).
// Top strip: category dropdown + live text filter. Double-click (or
// Enter) on a thumbnail activates the part.
class PartsBrowser : public QDockWidget {
    Q_OBJECT
public:
    explicit PartsBrowser(parts::PartsLibrary& lib, QWidget* parent = nullptr);

    void rebuild();   // re-read the library and repopulate

    // Insert a single library entry into the grid without touching the
    // other thousands of items. Used by the importer right after a
    // freshly-scanned part is registered with the library, so the user
    // sees the new thumbnail immediately without a UI freeze.
    void addOne(const QString& key);

    // MIME type used when a thumbnail is dragged out of this panel. MapView
    // recognises the same string in its drop handler.
    static constexpr const char* kPartMimeType = "application/x-bld-part";

    // Follow the budget: Show Only Budgeted Parts hides the rest, Show
    // Budget Numbers adds "used/limit" under each part (red when over).
    // `map` gives the layout the counts come from.
    void setBudget(BudgetSession* budget, std::function<const core::Map*()> map);
    // Recount after the layout changed.
    void refreshBudget();

    // "Browse the catalog on the web…" (builds that talk to a server).
    void setCatalogLinkVisible(bool visible);

signals:
    void partActivated(const QString& key);
    // "Browse the catalog on the web…" was clicked.
    void browseCatalogRequested();

    // Emitted when the user deletes an imported part from disk via
    // the right-click menu. MainWindow listens to trigger a parts-
    // library rescan so the deleted entry stops appearing in the
    // grid.
    void partDeleted();

    // "Re-import from Source" on an imported part.
    void reimportRequested(const QString& key);

    // Touch: a part tapped (to place with a tap on the map), or dragged out
    // sideways by a finger. The panel keeps the finger, so it says where it
    // is (global coords) until it lifts or the touch is cancelled.
    void partTapped(const QString& key);
    void touchDragMoved(const QString& key, QPoint globalPos);
    void touchDragDropped(const QString& key, QPoint globalPos);
    void touchDragCancelled();

public:
    // The thumbnails are read a few at a time after rebuild(), so a library
    // of thousands of parts doesn't freeze the window; these say how far.
    int iconsLoaded() const { return iconsDone_; }
    int iconsWanted() const { return iconsTotal_; }
    class LoadingCard* loadingCard() const { return loading_; }

    QListWidget* grid() const { return grid_; }

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    QPushButton* catalogLink_ = nullptr;
    // A finger on the grid: undecided, scrolling it, or dragging a part out.
    enum class TouchState { None, Undecided, Scroll, Drag, Held };
    TouchState touch_ = TouchState::None;
    QPointF touchStart_;
    QString touchKey_;
    bool handleTouch(QTouchEvent* e);
    // A finger held still on a part opens the same menu as a right-click.
    QTimer* holdTimer_ = nullptr;
    void showPartMenu(const QPoint& pos);

public:
    // How long a finger is held on a part before its menu opens (as on the map).
    static constexpr int kLongPressMs = 500;

private:
    void applyFilter();
    void loadSomeIcons();
    QString categoryForPath(const QString& absPath) const;

    parts::PartsLibrary& lib_;
    BudgetSession* budget_ = nullptr;
    std::function<const core::Map*()> map_;
    QComboBox*    category_ = nullptr;
    QLineEdit*    filter_   = nullptr;
    QListWidget*  grid_     = nullptr;
    class LoadingCard* loading_ = nullptr;
    QTimer* iconTimer_ = nullptr;
    QStringList iconQueue_;                       // keys, in the grid's order
    QHash<QString, QListWidgetItem*> iconItems_;  // key -> item still without its thumbnail
    int iconsDone_ = 0;
    int iconsTotal_ = 0;
};

}
