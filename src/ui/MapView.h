#pragma once

#include "../core/Brick.h"

#include <QColor>
#include <QGraphicsView>
#include <QHash>
#include <QPoint>
#include <QPointF>
#include <QSet>
#include <QString>

#include <QRectF>
#include <QStringList>

#include <memory>
#include <optional>
#include <vector>

class QDragLeaveEvent;
class QGraphicsItem;
class QGraphicsPixmapItem;
class QUndoStack;

namespace bld::core    { class Map; class LayerGrid; }
namespace bld::edit    { class FlexMove; }
namespace bld::parts   { class PartsLibrary; }
namespace bld::rendering { class SceneBuilder; }

namespace bld::ui {

class SelectionOverlay;

class MapView : public QGraphicsView {
    Q_OBJECT
public:
    explicit MapView(parts::PartsLibrary& parts, QWidget* parent = nullptr);
    ~MapView() override;

    void loadMap(std::unique_ptr<core::Map> map);
    // Budget whose limitation (when on) placement and paste respect.
    void setBudget(class BudgetSession* budget) { budget_ = budget; }
    void rebuildScene();  // re-run SceneBuilder against current map (after edits)

    core::Map* currentMap() { return map_.get(); }

    // What a saved view shows on this screen only (never saved in the
    // layout): exactly `sheets` (unset: every sheet as the layout has it),
    // the grid and the labels. It stays across rebuilds and live reloads.
    struct ViewFilter {
        std::optional<QStringList> sheets;
        bool grid = true;
        bool labels = true;
    };
    void setViewFilter(std::optional<ViewFilter> filter);
    const std::optional<ViewFilter>& viewFilter() const { return viewFilter_; }
    // The part of the map on screen now, in studs; unset with no map.
    std::optional<QRectF> screenRectStuds() const;
    // Fits `studs` into the window.
    void showRegionStuds(const QRectF& studs);
    rendering::SceneBuilder* builder() { return builder_.get(); }
    QUndoStack* undoStack() { return undoStack_.get(); }

    // Snap step applied to brick drag-drop, in studs. 0 disables snapping.
    void  setSnapStepStuds(double studs);
    double snapStepStuds() const { return snapStepStuds_; }

    // Default rotation step in degrees — used by the menu Rotate CW/CCW
    // actions (the explicit rotateSelected(degrees) call still takes a
    // caller-supplied amount).
    void  setRotationStepDegrees(double deg) { rotationStepDegrees_ = deg; }
    double rotationStepDegrees() const { return rotationStepDegrees_; }

    // Edit operations (invoked by menu/shortcut actions):
    void rotateSelected(float degrees);
    void nudgeSelected(double dxStuds, double dyStuds);
    void deleteSelected();
    void addPartAtViewCenter(const QString& partKey);
    void addPartAtScenePos(const QString& partKey, QPointF scenePosPx);

    // Resolve where a new part of `partKey` should land if dropped at
    // `cursorScenePx`, applying the same selected-brick anchor + connection
    // snap + grid snap precedence used by addPartAtScenePos. `outCentreStuds`
    // and `outOrientation` describe the would-be placement; `outSnapped` is
    // true when a connection snap (or selection anchor) fired.
    // `outSnapPointScenePx` (when non-null and outSnapped is true) returns
    // the world-snap-point in scene coords so the caller can render a snap
    // ring. Used by the live drag preview.
    void resolvePartPlacement(const QString& partKey, QPointF cursorScenePx,
                              QPointF* outCentreStuds, float* outOrientation,
                              bool* outSnapped,
                              QPointF* outSnapPointScenePx) const;

    // Clipboard / selection ops.
    void copySelection();            // copy selected bricks to the internal clipboard
    void cutSelection();             // copy + delete
    void pasteClipboard();           // insert clipboard bricks with fresh guids + offset
    void duplicateSelection();       // copy + paste in one command
    void selectAll();
    void deselectAll();
    bool clipboardEmpty() const { return clipboard_.empty(); }

    void bringSelectionToFront();
    void sendSelectionToBack();

    // Grouping (same-layer vanilla groups — modules span layers).
    void groupSelection();
    void ungroupSelection();
    // Extend selection to every brick reachable via connection links from the
    // current selection (transitive closure over Connexion.linkedToId).
    void selectPath();

    // Text ops.
    void addTextAtViewCenter(const QString& text);
    void addTextAtScenePos(const QString& text, QPointF scenePosPx);
    // Prompt the user to edit the selected single text cell's content.
    void editSelectedTextContent();

    // Active tool — affects left-click behaviour on the map.
    enum class Tool {
        Select, PaintArea, EraseArea, DrawLinearRuler, DrawCircularRuler,
        // Click points to build a venue outline polygon (first point,
        // subsequent points make edges). Right-click or Enter finishes;
        // Escape cancels. Replaces any existing outline.
        DrawVenueOutline,
        // Same interaction, but appends a new obstacle polygon to the
        // current venue (must already have an outline).
        DrawVenueObstacle,
    };
    void setTool(Tool t) { tool_ = t; }
    Tool tool() const { return tool_; }

    // Area paint state (read by the paint handler).
    void setPaintColor(QColor c) { paintColor_ = c; }
    QColor paintColor() const { return paintColor_; }

signals:
    void selectionChanged();
    // Emitted when the layer set changes shape (a new layer was added as
    // part of a paste across layers, an imported module created layers,
    // etc.) so MainWindow can refresh the LayerPanel.
    void layersChanged();
    // A map was put in the view (opened, new, or a live layout reloaded).
    void mapLoaded();

protected:
    void wheelEvent(QWheelEvent* e) override;
    // Viewport event filter — installed on viewport() so wheel events
    // funnel through our wheelEvent() only and never reach the
    // QAbstractScrollArea base class, which would otherwise pan the
    // viewport via (hidden) scrollbars.
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    // Draws the persistent scale-indicator bar in the lower-left of the
    // viewport after the rest of the scene. Using drawForeground keeps
    // the readout pinned to the viewport corner regardless of pan / zoom.
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    struct BrickOriginSnapshot {
        QGraphicsItem* item = nullptr;
        int    layerIndex   = -1;
        QString guid;
        QPointF scenePosAtPress;      // item->pos() at mousePress
        QPointF studTopLeftAtPress;   // brick.displayArea.topLeft() at mousePress
    };

    // Ruler / label drag snapshots. One per logical item (rulers may
    // render as multiple scene items; we store the first piece's
    // scenePosAtPress to compute the drag delta).
    struct RulerDragSnapshot {
        QGraphicsItem* anyPiece = nullptr;
        int     layerIndex = -1;
        QString guid;
        QPointF scenePosAtPress;
    };
    struct LabelDragSnapshot {
        QGraphicsItem* item = nullptr;
        QString labelId;
        QPointF scenePosAtPress;
    };

    void captureDragStart();
    void commitDragIfMoved();
    // Run connection snap as a single rigid group shift during drag. Called
    // after QGraphicsView::mouseMoveEvent has translated every selected
    // item by the mouse delta. If a snap fires, translates all dragged
    // items by the snap shift so connections take priority over Qt's
    // built-in per-item drag and over grid snap.
    void applyLiveConnectionSnap();
    // Generous snap radius around free connection points. BlueBrick's
    // getMovedSnapPoint uses roughly 2 × grid step as the search range;
    // we use the larger of 16 studs (half a brick-unit wide) and 2 × grid
    // step so connections feel "sticky" even on a 1-stud grid.
    double connectionSnapThresholdStuds() const;
    std::vector<BrickOriginSnapshot> selectedBrickSnapshots() const;

    parts::PartsLibrary& parts_;
    std::unique_ptr<core::Map> map_;
    std::unique_ptr<rendering::SceneBuilder> builder_;
    std::optional<ViewFilter> viewFilter_;
    void applyViewFilter();
    std::unique_ptr<QUndoStack> undoStack_;

    std::vector<BrickOriginSnapshot> dragStart_;
    std::vector<RulerDragSnapshot>   rulerDragStart_;
    std::vector<LabelDragSnapshot>   labelDragStart_;

    // Middle-mouse pan state.
    bool    panning_ = false;
    QPoint  panAnchor_;

    // Mouse position in scene coords, updated on every press and move. The
    // connection-snap code reads this so the connection point closest to
    // the cursor — not to some arbitrary "anchor" brick — drives snapping
    // for multi-brick group drags.
    QPointF lastMouseScenePos_;

    // BlueBrick-style master-brick snap anchor. Set at mouse-press from
    // whichever brick the user grabbed and which of its connections was
    // closest to the click. Every live-drag snap frame aligns THIS ONE
    // connection to the nearest free compatible target, and the whole
    // selected group follows the shift. Cleared on mouseRelease.
    QString grabBrickGuid_;
    int     grabBrickLayerIndex_ = -1;
    int     grabActiveConnIdx_   = -1;   // index into part meta->connections
    void captureGrabAnchor(QPointF clickScenePos);
    void clearGrabAnchor();

    // Internal (in-process) brick clipboard. Cross-process paste would require
    // serialising to QMimeData via the system clipboard; deferred until a user
    // actually needs it.
    // Clipboard entries carry the source layer's name so paste can route
    // each brick back to a matching layer (creating one if necessary).
    // Copying a multi-layer selection and pasting into any map preserves
    // the layering — otherwise track pieces would flatten onto the first
    // brick layer along with scenery and everything looks broken.
    struct ClipEntry {
        QString     sourceLayerName;
        core::Brick brick;
    };
    std::vector<ClipEntry> clipboard_;

    // Snap + rotation step config, updated by MainWindow from toolbar + QSettings.
    double snapStepStuds_       = 0.0;     // 0 disables snap
    double rotationStepDegrees_ = 90.0;

    Tool   tool_ = Tool::Select;
    QColor paintColor_ = QColor(0, 128, 0);
    // Track which cells we already painted in the current stroke so dragging
    // a single cell size across cells adds exactly one change per cell.
    QSet<QPoint> strokeCellsTouched_;

    // Ruler-draw state: scene pos of the first click-anchor when a ruler
    // tool is active. Released-position pairs with this to create the ruler.
    bool    drawingRuler_ = false;
    QPointF rulerStart_;
    // Endpoint-drag state for selected linear rulers. When the user
    // clicks on a small square handle drawn at point1 or point2, we
    // enter this mode and on release commit a MoveRulerEndpointCommand.
    // Only linear rulers expose handles in this first pass — circular
    // edits still go through Properties for now.
    // BlueBrick's flex move: double-click-drag a piece of a selected
    // chain with hinged connections (flex track, magnet couplings) to
    // bend it. Edits the map live; one undo command on release.
    bool startFlexMove(QGraphicsItem* under, QPointF scenePos);
    void updateFlexItems();
    void finishFlexMove();
    // BlueBrick's MoveGridOrigin: with the grid layer selected (showing
    // cell indices), dragging empty space moves the index origin by cells.
    bool gridOriginDragging_ = false;
    int     gridLayer_ = -1;
    QPoint  gridDragStartCell_, gridDragLastCell_, gridOriginBefore_;
    QPoint  gridCellAt(QPointF scenePos) const;
    void    drawCellIndices(class QPainter* painter, const QRectF& rect, const core::LayerGrid& grid);
    void    setGridOrigin(QPoint corner);

    // Whether `quantity` more of `part` fit the budget; tells the user
    // when not (as BlueBrick, with an opt-out).
    bool budgetAllows(const QString& part, int quantity = 1);
    void reportBudgetRefusal();
    class BudgetSession* budget_ = nullptr;

    std::unique_ptr<edit::FlexMove> flex_;
    int     flexLayer_ = -1;
    QString flexGrabbed_;
    bool    flexMoved_ = false;
    QHash<QString, QGraphicsItem*> flexItems_;
    // Bricks selected when the left button last went down: Qt drops the
    // rest of a selection when a click on one of them is released, but
    // the double-click that starts a flex move needs the whole chain.
    QSet<QString> pressSelection_;

    bool    draggingRulerEndpoint_ = false;
    int     rulerEndpointIndex_ = -1;        // 0 = point1, 1 = point2
    int     rulerEndpointLayer_ = -1;
    QString rulerEndpointGuid_;
    QPointF rulerEndpointDragLast_;          // last scene-px pos under cursor
    QPointF rulerEndpointOriginalStuds_;     // pre-drag value, in studs
    // Live preview while the user click-drags a ruler — shows the line
    // (or circle) being drawn plus a label with current length / radius
    // pinned near the cursor. Lets the user dial in a target length
    // before release without watching the status bar.
    class QGraphicsItem* rulerPreviewShape_ = nullptr;
    class QGraphicsSimpleTextItem* rulerPreviewLabel_ = nullptr;
    void updateRulerPreview(QPointF endScene);
    void clearRulerPreview();

    // Venue-draw state: accumulated polygon vertices in scene-stud coords
    // while the user is clicking points in DrawVenueOutline /
    // DrawVenueObstacle mode. Finalised via right-click or Enter key,
    // cancelled via Escape.
    QVector<QPointF> venueDrawPoints_;
    // Preview item shown while drawing so the user sees what's being built.
    class QGraphicsPathItem* venueDrawPreview_ = nullptr;
    void finishVenueDraw();
    void updateVenueDrawPreview(QPointF hoverScenePos = {});

    // Live overlay item that paints outlines around every selected item.
    // Lives in the scene with a very high z-value so it's always on top.
    // Redrawn whenever scene()->selectionChanged fires.
    SelectionOverlay* selectionOverlay_ = nullptr;
    void refreshSelectionOverlay();

    // Set by the live connection-snap hook when the dragged brick is
    // currently locked to a connection; read by the overlay so the
    // selection outline renders in connection-snap colour to give the
    // user live feedback. Also stored in scene coords so the overlay can
    // draw a ring at the exact connection point.
    bool    liveSnapActive_ = false;
    QPointF liveSnapPointScene_;

    // Sidecar background-image cache. drawBackground reloads the pixmap
    // only when the path changes so panning over a large image stays
    // cheap. Held by value (no QGraphicsItem) so layer ordering can't
    // accidentally bury it behind other items.
    QPixmap cachedBackgroundImage_;
    QString cachedBackgroundPath_;

    // Live drag-from-parts-browser preview state. While the user is
    // dragging a part over the map, we paint a translucent ghost showing
    // where it would land — including any connection-snap rotation — so
    // they don't have to drop and undo to see if the snap took. The ghost
    // lives in the scene at a high z-value and is removed on dragLeave or
    // drop.
    QGraphicsPixmapItem* dragPreviewItem_ = nullptr;
    // The "key" identifies what's currently being previewed:
    //   * Empty: no preview.
    //   * "part:<partKey>": a parts-browser drag.
    //   * "module:<bbmPath>": a module-library drag.
    // Stored as a single string so the same dragPreviewItem_ can be
    // rebuilt on key change without juggling two parallel members.
    QString dragPreviewKey_;
    // Cached centroid (in scene px) of the currently-previewed module so
    // the ghost can be repositioned cheaply on every dragMove without
    // re-parsing or re-rasterizing.
    QPointF dragPreviewModuleCentroidScenePx_;
    // Offset in studs from the module's centroid to the bbox top-left.
    // Used so grid-snap can align the bbox top-left (matching how
    // single-brick drops snap their top-left), regardless of module size.
    QPointF dragPreviewModuleTopLeftOffsetStuds_;
    void clearDragPreview();
    void updateDragPreview(const QString& partKey, QPointF cursorScenePx);
    void updateModuleDragPreview(const QString& bbmPath, QPointF cursorScenePx);
    void showDropTargetHint();
    void clearDropTargetHint();
};

}
