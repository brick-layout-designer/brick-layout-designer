#pragma once

#include <QDockWidget>
#include <QHash>
#include <QIcon>
#include <QString>

class QTableWidget;
class QLineEdit;
class QLabel;
class QAction;

namespace bld::core  { class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::ui {

class MapView;

// Live readout of every part currently placed on the map. Columns:
// icon, part#, count, description, budget (if a .bbb is loaded), and
// a "missing" delta showing how many past the budget cap. Mirrors
// BlueBrick's PartUsageView.
//
// Clicking a row selects every brick of that part on the map so the
// user can jump straight to them.
class PartUsagePanel : public QDockWidget {
    Q_OBJECT
public:
    PartUsagePanel(parts::PartsLibrary& lib, QWidget* parent = nullptr);

    // Point at the MapView so clicks can drive selection + connect to
    // the map's undo-stack for live refresh. Pass nullptr to detach.
    void bindMapView(MapView* view);

    // Rebuild the table from the currently-bound map. Auto-called on
    // undo-stack changes; callers shouldn't need to call this directly.
    void refresh();

    // Drop cached sprite icons after the parts library was rescanned.
    void clearIconCache() { iconCache_.clear(); }

private:
    parts::PartsLibrary& lib_;
    MapView*     mapView_ = nullptr;
    QTableWidget* table_   = nullptr;
    QLineEdit*   filterE_  = nullptr;
    QLabel*      summary_  = nullptr;
    QAction*     selectAllOfPartAct_ = nullptr;
    // Scaled row icons by part key. refresh() runs on every selection
    // change, so re-decoding + smooth-scaling each sprite there made
    // plain clicks on the map lag on layouts with many distinct parts.
    QHash<QString, QIcon> iconCache_;

    void selectAllBricksOfCurrentRow();
    QString filterText() const;
};

}  // namespace bld::ui
