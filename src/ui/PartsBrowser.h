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
class QSlider;

namespace bld::core  { class Map; }
namespace bld::ui::theme { class PrefsStore; }
namespace bld::parts { class PartsLibrary; struct PartMetadata; }

namespace bld::ui {

class BudgetSession;

// A part's tooltip in the Parts list: its description and key, who built
// it (an imported model's <Designer>), and which server it came from when
// it's one of a server's parts (server-parts/<folder>; serverLabels maps
// each folder to the server's name in "Your servers").
QString partTooltip(const parts::PartMetadata& meta, const QString& key, const QString& desc,
                    const QHash<QString, QString>& serverLabels);

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

    // Finds a part: every category, the search set to `text` (its number).
    void showPart(const QString& text);

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

    // The pictures' size in pixels (theme::kPartsIconMin..Max); the grid's
    // cells follow it. Set by the slider under the list, Ctrl+wheel or a
    // pinch over it, and the synced setting partsIconSize.
    int iconSize() const { return iconSize_; }
    void setIconSize(int px);
    QSlider* sizeSlider() const { return sizeSlider_; }
    // Where the size is kept (synced with the account while connected).
    // The app's store by default; tests pass their own.
    void setPrefsStore(theme::PrefsStore* store);
    // A size the person chose: shown now, saved once they pause.
    void chooseIconSize(int px);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    QPushButton* catalogLink_ = nullptr;
    // A finger on the grid: undecided, scrolling it, or dragging a part out.
    enum class TouchState { None, Undecided, Scroll, Drag, Held, Pinch };
    TouchState touch_ = TouchState::None;
    QPointF touchStart_;
    QString touchKey_;
    bool handleTouch(QTouchEvent* e);
    // A finger held still on a part opens the same menu as a right-click.
    QTimer* holdTimer_ = nullptr;
    void showPartMenu(const QPoint& pos);
    // Two fingers apart or together: the size when they landed, and how far apart.
    qreal pinchStartDist_ = 0;
    int pinchStartSize_ = 0;
    qreal wheelSteps_ = 0;  // Ctrl+wheel, in notches, for high-resolution wheels

public:
    // How long a finger is held on a part before its menu opens (as on the map).
    static constexpr int kLongPressMs = 500;

private:
    void applyFilter();
    void loadSomeIcons();
    void updateGridSize();
    void saveIconSize();
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
    // Re-reading the pictures at a new size, without the loading card.
    bool quietIcons_ = false;
    bool budgetNumbers_ = false;
    int iconSize_ = 0;
    QSlider* sizeSlider_ = nullptr;
    QTimer* saveSize_ = nullptr;
    theme::PrefsStore* prefs_ = nullptr;
};

}
