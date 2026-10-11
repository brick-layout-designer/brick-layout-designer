# BlueBrick parity: actions, menus and context menus

This page compares vanilla BlueBrick (`.reference/BlueBrick/BlueBrick`) with this desktop app and the web app.
It was first audited on 2026-10-10.

- **Rows:** one per BlueBrick undoable action (`Actions/**`), then the menu and context-menu items that are not actions.
- **Cells:** **same**, **differs** (with how), or **missing**. Each cell has a code pointer.
- **Code pointers:**
  - Desktop paths are under `src/`.
  - Web paths are under `apps/web/src/editor/` in the web repo.
  - BlueBrick paths are under `.reference/BlueBrick/BlueBrick/`.
- **Mirror:** the web repo mirrors this table in `references/PARITY.md`, section "BlueBrick parity". That file's desktop-to-web checklist covers every menu item in detail.

## Known gaps

These are confirmed in both apps unless a row says otherwise. They are the gaps that matter most, in order:

1. **Text cells can't be moved.** BlueBrick drags text (`MoveText`); our two apps only let you edit a text cell's text. There is no text drag in `ui/MapViewDrag.cpp` (desktop), and the web comment at `EditorPage.tsx` ("text cells don't drag") says the same.
2. **Rotating a part that is connected to other track.** BlueBrick's `RotateBrick` steps the selection around its connection to the fixed track, through the free connections in turn, so the part stays connected (`Actions/Bricks/RotateBrick.cs`). Ours turns the part about its own centre, which breaks the connection.
3. **Rotate CW / CCW skips text and rulers.** It works on parts only: `MapView::rotateSelected` on desktop, `rotateBricksAboutCentroid` on web. BlueBrick has `RotateText` and `RotateRulers`.
4. **Bring to Front / Send to Back skips text and rulers.** It works on parts only: `MapView::bringSelectionToFront` / `sendSelectionToBack` and `ReorderBricksCommand` on desktop, `reorderBricks` on web. BlueBrick has `BringTextToFront`, `SendTextToBack`, `BringRulerToFront` and `SendRulerToBack`.
5. **A ruler's offset or a circle's radius can't be dragged on the map.** BlueBrick's `ScaleRuler` does this. Ours sets both in the Edit Ruler dialog only.
6. **Detach is only in the dialog.** BlueBrick has Detach on the map's context menu (`MapPanel.cs:1018`). Ours has it in the Edit Ruler dialog only: `ui/EditDialogs.cpp:266-283` on desktop, `EditRulerDialog.tsx:100` on web.

## Actions

| BlueBrick action | Desktop | Web |
|---|---|---|
| AddBrick | same: `AddBrickCommand` (`edit/EditCommands.h`) | same: `placeBrick` (`mutations.ts`) |
| AddConnectBrick | same: connection snap on place (`ui/MapView.cpp` "Connection snap") | same: `snap.ts`, `liveSnapReach.ts` |
| AddConnectGroup | same: a group placed snapped, turned on its pivot part | same |
| DeleteBrick | same: `DeleteBricksCommand` | same: `deleteBricks` / `deleteBricksAcrossLayers` |
| DuplicateBrick | **differs.** Duplicate is copy plus paste under the cursor, not BlueBrick's offset copy (`ui/MapViewClipboard.cpp:144`) | **differs** in the same way (`mixedSelection.ts`) |
| MoveBrick | same: `MoveBricksCommand` | same: `translateBricks` / `translateBricksAcrossLayers` |
| RotateBrick | **differs.** (a) There is no stepping round a fixed connection (gap 2). (b) The pivot is the mean of the sprite centres; BlueBrick uses the centre of the selection's bounding box (`Actions/Items/RotateItems.cs`). One part is the same either way (`MapView::rotateSelected`). | **differs** in the same two ways (`rotateBricksAboutCentroid`, `brickGeometry.ts` `rotateAroundPivots`) |
| RotateAndMoveBrick | same: snap-rotate on drop (`ui/MapViewDrag.cpp:628-715`) | same |
| RotateBrickOnPivotBrick | same: a dragged selection turns to fit the connection it snaps to (`MapViewDrag.cpp` `connectionRotates`) | same: `snap.ts` |
| BringBrickToFront / SendBrickToBack | same, apart from order (see ChangeItemOrder): `ReorderBricksCommand` | same: `reorderBricks` |
| ChangeBrickElevation | same: altitude in Part Properties (`EditBrickCommand`) | same: `editBrick` |
| ReplaceBrick | same: Find & Replace, part scope (`ui/FindDialog.cpp`) | same: `findReplace.ts` |
| FlexMove | same: `edit/FlexMove.cpp` | same: flex in `EditorPage.tsx` |
| ChangeItemOrder (base) | **differs.** BlueBrick inserts each item at index 0 in *selection (click) order*, so BACK reverses that order and FRONT follows it. Ours keeps the items' existing relative order. This is an intentional difference: ours does not depend on click order. | **differs** in the same way |
| DuplicateItems / MoveItems / RotateItems (bases) | see the rows above | see the rows above |
| GroupItems / UngroupItems | same: `GroupBricksCommand` / `UngroupBricksCommand` | same: `groupBricks` / `ungroupBricks` |
| MoveGridOrigin | same: `MoveGridOriginCommand` | same: `moveGridOrigin` |
| AddArea / DeleteArea | same: paint / erase (`PaintAreaCellsCommand`) | same: `paintAreaCells` |
| MoveArea | **missing.** You can't shift a whole area layer by cells | **missing** |
| AddLayer / RemoveLayer | same: `AddLayerCommand` / `DeleteLayerCommand` | same: `addLayer` / `deleteLayer` |
| MoveLayerUp / MoveLayerDown | same: `MoveLayerCommand` | same: `moveLayer` |
| ChangeLayerOption | same: layer properties (name, transparency, hull, elevation) | same: `renameLayer`, `setLayerTransparency`, `setLayerHullProperties`, ... |
| HideLayer / ShowLayer | **differs.** Not an undo step (`ui/LayerPanel.cpp:166`) | **differs.** Shown and hidden through the doc (`setLayerVisible`) |
| SelectLayer | **differs.** Picking a sheet is not an undo step. This is intentional: selection can span sheets. | **differs** in the same way |
| ChangeBackgroundColor | same: `ChangeBackgroundColorCommand` | same: `setBackgroundColor` |
| ChangeGeneralInfo | same: `ChangeGeneralInfoCommand` | same: `setGeneralInfo` |
| ChangeMapAppearance | **differs.** Grid colour, font and area-cell size are settings, not undo steps (Settings dialog). This is intentional. | **differs** in the same way |
| AddRuler | same: `AddRulerItemCommand` | same: `addLinearRuler` / `addCircularRuler` |
| DeleteRuler | same: `DeleteRulerItemCommand` | same: `deleteRulerItem` |
| DuplicateRuler | same: mixed-selection copy / paste | same |
| EditRuler | same: Edit Ruler dialog (`EditRulerItemCommand`) | same: `EditRulerDialog.tsx` |
| MoveRulers | same: `MoveRulerItemCommand` (`MapViewDrag.cpp:583`) | same: `moveRulerItem` |
| MoveRulerControlPoint | same: `MoveRulerEndpointCommand` | same: `moveRulerEndpoint` |
| AttachRulerToBrick | same: context menu (`ui/MapViewContextMenu.cpp:250-270`) | same: `attachRulerEndpoint` |
| DetachRuler | **differs.** Dialog only, no context-menu entry (gap 6) | **differs.** Dialog only |
| ScaleRuler | **differs.** Offset and radius are dialog-only (gap 5) | **differs.** Dialog only |
| RotateRulers | **missing** (gap 3) | **missing** |
| BringRulerToFront / SendRulerToBack | **missing** (gap 4) | **missing** |
| AddText | same: `AddTextCellCommand` | same: `addTextCell` / `newTextBox` |
| DeleteText | same: `DeleteTextCellCommand` | same: `deleteTextCell` |
| DuplicateText | same: mixed-selection copy / paste | same |
| EditText | same: Edit Text dialog (`EditTextCellTextCommand`) | same: `editTextCellFull` |
| MoveText | **missing** (gap 1) | **missing** |
| RotateText | **missing** (gap 3) | **missing** |
| BringTextToFront / SendTextToBack | **missing** (gap 4) | **missing** |

## Map context menu (`MapPanel.cs`)

| BlueBrick item | Desktop | Web |
|---|---|---|
| Bring to Front / Send to Back | same, parts only (gap 4) | same, parts only |
| Select All / Deselect All | same | same |
| Select Path | same: `MapView::selectPath` | same |
| Group / Ungroup | same | same |
| Attach (ruler) | same: per endpoint | same |
| Detach (ruler) | **missing** from the menu (gap 6) | **missing** from the menu |
| Use as Model | **missing.** Nothing picks the clicked part in the parts library | **missing** |
| Properties | same | same |
| Scroll bars | n/a: there are no scroll bars | n/a |

## Main menus (`MainForm.cs`)

The menus otherwise match, item by item. The web repo's `references/PARITY.md` covers them in detail. The differences:

- **Rotate CW / CCW and Bring to Front / Send to Back:** parts only (gaps 3 and 4).
- **Move-step and rotation-step submenus:** same.
- **Paint / Erase / Choose colour:** same.
- **Ruler select / add ruler / add circle:** same.
- **Budget (New / Open / Import and Merge / Close / Save / Save As, Show only budgeted, Show numbers, Use limitation):** same.
- **Save Selection in Library:** same, as **Make a Module** and the module library.
- **Download Additional Parts:** differs. Parts come from the server catalog instead.
- **Reload Part Library:** differs. The catalog reloads live.

## Intentional differences (for Aaron)

- **Selection can span sheets.** BlueBrick acts on the selected layer only, and SelectLayer is an undo step.
- **ChangeItemOrder keeps relative order.** BlueBrick's BACK reverses the click order.
- **Duplicate is copy plus paste under the cursor.**
- **Map-appearance settings are not undo steps.**

## Recommended fixes, in order

1. Drag text cells: MoveText.
2. Rotate a connected part around its connection: RotateBrick's stepping.
3. Rotate and Bring to Front / Send to Back for text and rulers.
4. Detach on the context menu.
5. On-canvas ruler offset and radius handles.
6. Use as Model.
7. MoveArea.
8. Rotation pivot: the centre of the bounding box, as BlueBrick does. This changes where multi-part rotations land; check it with bbconv first.
