#pragma once

#include "../core/Brick.h"
#include "../core/Group.h"
#include "SnapFeel.h"

#include <QColor>
#include <QEvent>
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
class QNativeGestureEvent;
class QPointingDevice;
class QTimer;
class QTouchEvent;
class QVariantAnimation;
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

    // The loading card over the map (LoadingCard.h). "Opening layout…",
    // painted at once; loadMap() takes it from there.
    void showOpening(const QString& detail);
    void hideOpening();
    // After the parts library changed: reads the layout's pictures again
    // with the loading card, then rebuilds the scene.
    void reloadPictures();
    class LoadingCard* loadingCard() const { return loadingCard_; }
    // Parts in the layout whose picture couldn't be read (lower-case keys).
    const QStringList& failedPictures() const { return failedPictures_; }
    // How often the card repaints while pictures load (tests: 0, every one).
    static void setPictureProgressStepMs(int ms);

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
    // `dragSnap`: the snap session of the drag that drops it (its final
    // snap keeps the join the drag held); none for a click or tap.
    void addPartAtScenePos(const QString& partKey, QPointF scenePosPx, snapfeel::Session* dragSnap = nullptr);

    // Resolve where a new part of `partKey` should land if dropped at
    // `cursorScenePx`, applying the same selected-brick anchor + connection
    // snap + grid snap precedence used by addPartAtScenePos. `outCentreStuds`
    // and `outOrientation` describe the would-be placement; `outSnapped` is
    // true when a connection snap (or selection anchor) fired.
    // `outSnapPointScenePx` (when non-null and outSnapped is true) returns
    // the world-snap-point in scene coords so the caller can render a snap
    // ring. Used by the live drag preview.
    // `session`: the drag's snap state (hold, speed gate), or none for a
    // one-off placement; `final` is the drop.
    void resolvePartPlacement(const QString& partKey, QPointF cursorScenePx,
                              QPointF* outCentreStuds, float* outOrientation,
                              bool* outSnapped,
                              QPointF* outSnapPointScenePx,
                              snapfeel::Session* session = nullptr, bool final = false) const;

    // Connection-snap reach in studs for the view as it is: about 14 screen
    // px at the current zoom, kept between 0.5 and 4 studs, scaled by the
    // Snap strength setting (SnapFeel.h).
    double connectionSnapReachStuds() const;
    // A connection snap is showing (the ring) during a drag.
    bool connectionSnapShown() const { return liveSnapActive_; }
    // Where it shows the ring (scene px).
    QPointF connectionSnapPoint() const { return liveSnapPointScene_; }
    // Alt (Option on a Mac) is held: place without connection snap.
    bool snapBypassed() const;

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
    // What Ungroup would do to the selection: nothing grouped, split it, or
    // only sets that are always used whole.
    enum class UngroupState { Nothing, Splits, AlwaysWhole };
    UngroupState ungroupState() const;
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

    // Touch: a part picked by a tap in the parts panel waits for a tap on
    // the map (the action bar says so, with Cancel).
    void armPartPlacement(const QString& partKey);
    void cancelPartPlacement();
    const QString& armedPart() const { return armedPart_; }
    // Touch: a part dragged out of the parts panel by a finger (the parts
    // panel keeps the touch, so it reports where it is, in global coords).
    // The ghost follows while over the map; a drop there places it exactly
    // as a mouse drop does. False when the finger ended off the map.
    void touchPartDragTo(const QString& partKey, QPoint globalPos);
    bool touchPartDropAt(const QString& partKey, QPoint globalPos);
    void touchPartDragCancel();
    // The same for a module dragged out of the Module library by touch.
    void touchModuleDragTo(const QString& bbmPath, QPoint globalPos);
    bool touchModuleDropAt(const QString& bbmPath, QPoint globalPos);
    // Places the module saved at `bbmPath` with its centre at `scenePos`
    // (a drop from the Module library); false when it can't be read.
    // `dragSnap`: the snap session of the drag that drops it, if any.
    bool dropModuleAt(const QString& bbmPath, QPointF scenePos, snapfeel::Session* dragSnap = nullptr);
    // Places a module's parts sheets (already read, e.g. from a server) as
    // one module named `name`, centred on `scenePos` (snapped like a drop),
    // reading its part pictures first with the loading card. Selects the
    // placed parts. False when it has no parts.
    bool placeModule(core::Map& module, const QString& name, const QString& source, QPointF scenePos,
                     snapfeel::Session* dragSnap = nullptr);
    // The scene point at the middle of what the map shows.
    QPointF viewCentre() const;
    class TouchActionBar* touchActionBar() const { return touchBar_; }
    // The ring filling under a resting finger, until the menu opens.
    bool longPressRingShown() const;
    // How long a finger rests for the context menu.
    static constexpr int kLongPressMs = 500;

    // Edit module: one placed module opened part by part (the web's Edit
    // module). Empty: every module acts as one piece. Entering or leaving
    // clears the selection; the rest of the layout is dimmed and out of
    // reach meanwhile, and a bar at the top says so (with Done).
    void setEditingModule(const QString& moduleId);
    const QString& editingModule() const { return editingModuleId_; }
    // Who else is editing which module (from presence): module id -> names.
    void setPeersEditing(const QHash<QString, QStringList>& who);
    class ModuleEditBar* moduleEditBar() const { return editBar_; }
    // Whether the selection may move or turn as a whole; says why not when
    // a pinned module stops it.
    bool selectionMayMove();
    // Parts placed while a module is edited join it; a set or library
    // module placed meanwhile melts into it. Pushes undo steps.
    void absorbIntoEditedModule(const QSet<QString>& guids);
    // The edited module's outline (its parts on visible sheets, plus the
    // frame's half-stud margin), in studs.
    std::optional<QRectF> editedModuleFrameStuds() const;

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
    // The touch bar's Add part: show the parts panel.
    void addPartRequested();
    // Edit module was entered (an id) or left (empty).
    void editingModuleChanged(const QString& moduleId);
    // The map's right-click menu: a module's Colours...
    void moduleLookRequested(const QString& moduleId);

protected:
    // Touch (pinch, two-finger pan, tap, drag, long press) and trackpad
    // gestures, before QGraphicsView sees them.
    bool viewportEvent(QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
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
        double rotationAtPress = 0;   // item->rotation() at mousePress
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

    // True when a left press at `viewPos` would start moving the grid's
    // cell-index origin.
    bool wouldDragGridOrigin(QPoint viewPos) const;
    // A selected linear ruler's endpoint handle under `clickScene`; with
    // `startDrag`, the endpoint drag begins.
    bool rulerEndpointAt(QPointF clickScene, bool startDrag);
    // `screenPx` in scene px at the current zoom, never under the handle size.
    double handleRadiusScenePx(double screenPx) const;

    // Touch. One finger on a part (or with a drawing tool) is the mouse's
    // left button, through the same press / move / release code, so
    // selecting, moving, grid and connection snapping are the mouse's. One
    // finger on empty map pans; two pinch and pan.
    enum class TouchState { None, Press, PanOrTap, Pan, Pinch, LongPressed };
    bool handleTouch(QTouchEvent* e);
    bool handleNativeGesture(QNativeGestureEvent* e);
    void sendTouchMouse(QEvent::Type type, QPointF viewPos);
    // Lets go of a one-finger press without a click: a moved part goes back.
    void cancelTouchPress();
    void longPressFired();
    void panBy(QPointF delta);
    // Scale by `factor`, keeping the scene point under `from` (viewport
    // coords) under `to`.
    void zoomAt(QPointF from, QPointF to, double factor);
    void refreshTouchBar();
    TouchState touchState_ = TouchState::None;
    QPointF touchStart_, touchLast_, pinchCentre_;
    double  pinchDist_ = 0.0;
    bool    touchMoved_ = false;
    QTimer* longPressTimer_ = nullptr;
    QVariantAnimation* longPressAnim_ = nullptr;  // the filling ring
    void stopLongPressRing();
    void paintLongPressRing(QPainter* painter);
    class TouchActionBar* touchBar_ = nullptr;
    QString armedPart_;

    void captureDragStart();
    void commitDragIfMoved();
    // Run connection snap as a single rigid group shift during drag. Called
    // after QGraphicsView::mouseMoveEvent has translated every selected
    // item by the mouse delta. If a snap fires, translates all dragged
    // items by the snap shift so connections take priority over Qt's
    // built-in per-item drag and over grid snap.
    // `fromMove`: Qt has just moved the items (a pointer move); false for
    // a settle re-run, which keeps the last raw delta.
    void applyLiveConnectionSnap(bool fromMove = true);
    // The snap key of the active (grabbed) end, if the grabbed part is
    // being dragged; empty without a grab anchor.
    QString activeKey() const;
    // The drag's delta where the pointer has the parts (scene px), before
    // any snap moved or turned them; none until the first live snap.
    std::optional<QPointF> dragRawDeltaPx_;
    // Snap state of the brick drag, the part dragged in and the module
    // dragged in (hold, pointer speed), and the clock their speeds use.
    // Modifier keys of the latest pointer or drag event (Alt: no connection snap).
    Qt::KeyboardModifiers snapMods_;
    mutable snapfeel::Session dragSnap_;
    mutable snapfeel::Session placeSnap_;
    // A flex move's end snapping (calm snap: hold and switch on targets).
    snapfeel::Session flexSnap_;
    // Bend the flex move's chain to the pointer, its end snapping like a
    // dragged part's grabbed end; `final` on release.
    void bendFlexTo(QPointF mouseStuds, bool final);
    mutable snapfeel::Session moduleSnap_;
    double snapClockMs() const;
    // A pointer move at viewport position `vp` for `session`'s speed.
    void sampleSnapSpeed(snapfeel::Session& session, QPointF vp) const;
    // Re-runs the live snap once a fast drag stops dead.
    class QTimer* snapSettle_ = nullptr;
    // Snap marks: the ring on the target and the dot on the moving connection.
    void setSnapMarks(bool active, QPointF ringScene, std::optional<QPointF> movingScene);
    std::vector<BrickOriginSnapshot> selectedBrickSnapshots() const;

    // Reads the pictures `map` needs that weren't read yet, counting them
    // on the loading card. False when a newer loadMap() ran meanwhile.
    bool preloadPictures(const core::Map& map);
    // Hides the card, or says which pictures couldn't load.
    void finishLoading();
    void retryFailedPictures();
    class LoadingCard* loadingCard_ = nullptr;
    QStringList failedPictures_;
    QStringList shownFailures_;
    bool preloading_ = false;
    int loadGeneration_ = 0;

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
    // The groups (sets) above the copied parts, by source layer name.
    QHash<QString, std::vector<core::Group>> clipboardGroups_;

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
    // Bend handles: a round handle on each free end of the flexible run
    // the selection holds (flex track, magnet couplings...); dragging one
    // bends the run so that end follows.
    struct BendHandle {
        int layer = -1;
        QString guid;
        int connection = -1;
        QPointF studs;         // where the end is in the map (committed)
        QPointF local;         // the connection on its brick, studs from the sprite centre
        QSet<QString> run;
    };
    std::vector<BendHandle> bendHandles_;
    // The handles' bricks' scene items: a handle is drawn and grabbed where
    // its brick's item is now, so it follows a drag, a turn or a bend
    // before it's committed. Cleared before the scene is rebuilt.
    QHash<QString, QGraphicsItem*> bendItems_;
    QPointF bendHandleScene(const BendHandle& h) const;
    bool flexFromHandle_ = false;
    bool overBendHandle_ = false;
    void refreshBendHandles();
public:
    // A bend handle's radius on screen (px, the same at every zoom) and in
    // the scene now; `hit` for the area that grabs it.
    double bendHandleScreenRadius(bool hit = false) const;
    double bendHandleRadiusScenePx(bool hit = false) const;
    // The bend handle under a viewport point, or -1.
    int bendHandleAt(QPoint viewPos) const;
private:
    bool startBendFromHandle(int index);
    void paintBendHandles(QPainter* painter) const;
public:
    // The bend handles now (tests): where each is drawn and grabbed, in studs.
    std::vector<QPointF> bendHandlePositions() const;
private:
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

    // Edit module (MapViewModules.cpp).
    QString editingModuleId_;
    QHash<QString, QStringList> peersEditing_;
    class ModuleEditBar* editBar_ = nullptr;
    void refreshModuleEditBar();
    // Item flags and dimming for Edit module and pinned modules, after a build.
    void applyModuleState();
    // Modules picked whole (or only the edited one's parts): called from
    // the scene's selectionChanged. `before` is the selection before.
    void shapeModuleSelection();
    QSet<QString> lastBrickSelection_;
    void paintModuleEdit(QPainter* painter);
    // A press on a selection with a pinned module: the drag is refused.
    bool pinnedDragBlocked_ = false;
    bool pinnedDragTold_ = false;
    // Editing: the module's outline and its parts' areas when the press
    // happened, to tell when a dragged part leaves it.
    std::optional<QRectF> editOutlineAtPress_;
    QHash<QString, QRectF> editAreasAtPress_;
    void checkPartsLeftModule();
    void showStatus(const QString& text, int ms);
    // The topmost item at a viewport point, past module frames and names.
    QGraphicsItem* itemUnder(QPoint viewPos) const;
    // A module's entries at the top of the right-click menu.
    void addModuleMenu(class QMenu& menu, const QString& clickedBrickGuid);

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
    std::optional<QPointF> liveSnapMovingScene_;

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
    // The dragged module's bricks, centroid at the origin, for its snap.
    std::vector<core::Brick> dragPreviewModuleBricks_;
    // The connection snap of `bricks` (already placed) onto the map's free
    // ends: the module turns `turn` degrees about `pivot` (its joining
    // connection) and lands it on `to` (turnPoint). Nothing when none.
    struct ModuleSnap {
        QPointF pivot;
        QPointF to;
        double turn = 0;
    };
    std::optional<ModuleSnap> moduleSnapShift(const std::vector<const core::Brick*>& bricks,
                                              QPointF cursorStuds, snapfeel::Session* session,
                                              bool final) const;
    void clearDragPreview();
    void updateDragPreview(const QString& partKey, QPointF cursorScenePx);
    void updateModuleDragPreview(const QString& bbmPath, QPointF cursorScenePx);
    void updateSetDragPreview(const QString& setKey, QPointF cursorScenePx);
    void showDropTargetHint();
    void clearDropTargetHint();
};

}
