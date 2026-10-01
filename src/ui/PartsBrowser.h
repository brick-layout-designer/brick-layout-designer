#pragma once

#include <QDockWidget>
#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

class QComboBox;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QTimer;

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

signals:
    void partActivated(const QString& key);

    // Emitted when the user deletes an imported part from disk via
    // the right-click menu. MainWindow listens to trigger a parts-
    // library rescan so the deleted entry stops appearing in the
    // grid.
    void partDeleted();

    // "Re-import from Source" on an imported part.
    void reimportRequested(const QString& key);

public:
    // The thumbnails are read a few at a time after rebuild(), so a library
    // of thousands of parts doesn't freeze the window; these say how far.
    int iconsLoaded() const { return iconsDone_; }
    int iconsWanted() const { return iconsTotal_; }
    class LoadingCard* loadingCard() const { return loading_; }

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
