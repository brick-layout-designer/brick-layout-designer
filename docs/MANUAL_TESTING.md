# Manual testing guide

See also [TRANSLATING.md](TRANSLATING.md) for the translator workflow.

The automated test suite (`ctest --test-dir build`) covers save/load round-trip,
command-layer undo/redo, LDraw parsing, and other non-interactive behaviour.
Everything that needs a running GUI — mouse interactions, rendering, live
snap, drag-and-drop — has to be verified by a human. That's this guide.

**Focus areas for every testing pass:**

- **Snap priority** — connection snap must always win over grid snap.
- **Selection visibility** — selected items must show a yellow outline.
- **Byte-exact round-trip** — vanilla BlueBrick must still open any `.bbm`
  this fork writes.
- **Undo/redo completeness** — Ctrl+Z should reverse every visible change.

When you find a bug: note which checklist item failed, the reproduction
steps, and screenshots before/after if visual. Ideally add a fixture to
`fixtures/bbm-corpus/` or a regression test alongside the fix.

---

## 0. Smoke test (do this first)

1. Launch `build/src/app/brick-layout-designer`.
2. Verify the app opens without a crash, the Parts / Layers / Modules /
   Module library docks are all visible.
3. File → New → any template. Scene should show a blank layout with the
   template's default layers.
4. File → Quit. Clean exit, no stderr warnings about dangling items.

If step 1 or 4 fails — stop. We have a regression that breaks everything
else and must be fixed first.

---

## 1. Selection + rendering

### 1.1 Basic click selection
- [ ] Click on a brick → yellow outline appears around it.
- [ ] Click on empty space → selection clears, rubber-band starts.
- [ ] Ctrl-click adds to selection; Ctrl-click again toggles off.
- [ ] Shift-click extends selection to clicked brick without clearing.
- [ ] Rubber-band drag selects every brick whose centre lies inside the band.

### 1.2 Selection overlay vs items
- [ ] Clicking on a brick that has connection dots still selects the
      brick (dots are children; shouldn't steal the click).
- [ ] Clicking on a selection-outline region doesn't select the overlay
      itself — the click passes through to the brick underneath.

### 1.3 Z-ordering
- [ ] Bricks on higher layers render above bricks on lower layers
      (reorder layers via Layers panel up/down arrows to verify).
- [ ] Within a single layer, later-added bricks render on top.
- [ ] "Bring to Front" / "Send to Back" in Edit menu actually reorder.

### 1.4 Rendering toggles (View menu)
For each toggle: flip on, verify visible effect; flip off, verify gone.
- [ ] Connection Points — dots appear on every brick's free connections
      (red fill, white ring), not just selected ones.
- [ ] Brick Hulls — outline around each brick.
  - [ ] For irregular parts (curves, switches, diagonals), the outline
        hugs the real sprite silhouette, not a loose bounding rect.
  - [ ] Parts without a pixmap (placeholder rects) fall back to the
        axis-aligned bounding rect.
- [ ] Brick Elevation Labels — altitude number near each brick.
- [ ] Watermark — author/LUG/event text in corner (requires General Info
      to have values).
- [ ] Module Names — module label appears above each module frame.
- [ ] Electric Circuits — colored lines between every pair of linked
      9V rail connections, one color per circuit component. Filled
      dots mark every electric plug on every brick.
  - [ ] Each connected component of the electric graph gets a unique
        color (cycled through a 7-color palette).
  - [ ] Moving a brick disconnects its end of the circuit; the next
        render shows the now-open plug as an unattached dot.
- [ ] Ruler Attach Points — attach markers on rulers render when on.

### 1.5 Grid rendering
- [ ] Grid layer with `displayGrid=true` shows major grid lines.
- [ ] `displaySubGrid=true` shows the minor subgrid lines at a lighter weight.
- [ ] Grid colors honour the LayerGrid's configured color specs.
- [ ] Zooming in shouldn't make the grid lines disappear (cosmetic pens).

---

## 2. Connection snap (the big one)

### 2.1 Single brick drag, free-to-free
Set up: place two identical rail pieces ~10 studs apart with their
adjacent ends free.

- [ ] Click one rail's free end, drag toward the other's free end.
- [ ] As you get within the snap threshold (grid-snap step + 2 studs,
      or 4 studs when grid snap is disabled) of the target end, the
      dragged rail visually jumps so its free end sits exactly on the
      target's free end.
- [ ] On release, the status bar shows "Connection snap".
- [ ] Undo (Ctrl+Z) restores the pre-drag position; redo reapplies.

### 2.2 Single brick drag, angle alignment
Set up: place a rail at 0° and another at 90°.

- [ ] Drag the second rail toward the first's free end.
- [ ] When it snaps, the dragged rail rotates so its free end faces 180°
      opposite the target's free end (the tracks form a smooth continuation).
- [ ] Releasing commits both the move and the rotation as one undo step.

### 2.3 Single brick drag, already-linked ends excluded
Set up: place three rails in a chain (A-B-C), then click rail B.

- [ ] Rail B's ends are both linked (to A and C). Dragging B should
      not produce a connection snap (no free lead conn).
- [ ] You still see grid-snap-only drag behaviour if a snap step is set.

### 2.4 Multi-select / group drag
Set up: select 4 rails forming a closed module with 4 free external ends.

- [ ] Drag the group toward another free rail end somewhere.
- [ ] The connection nearest the cursor is picked as the lead — as you
      move, dragging toward a different free external end of the group
      makes THAT end lead instead.
- [ ] When any of the 4 free ends gets within the snap threshold
      (grid step + 2 studs) of a compatible
      free target, the whole group shifts rigidly so that end aligns.
- [ ] On release, the shifted position commits as one undo step.

### 2.5 Status-bar diagnostics
- [ ] During a drag, status bar shows one of:
      - "Connection snap active (N candidate conn(s))" when snap is firing.
      - "Connection snap: N moving conn(s), no target within X studs"
        when you're too far from a target.
      - "Connection snap: no free connections in selection" when the
        selection has zero free conns (e.g. dragging tables only).

### 2.6 Grid-snap fallback for parts without connections
Set up: place a Table part (parts with no `<ConnexionList>` in their XML).

- [ ] Drag the table around. It should visually track the current snap
      step setting (Preferences → Editing → Snap step).
- [ ] No "Connection snap" message appears; you see the grid snap behaviour.

### 2.7 Dragging parts back together re-links them
Set up: two rails linked end-to-end.

- [ ] Drag one rail a bit away. The formerly-linked end of the remaining
      rail should now show a connection dot (= free).
- [ ] Drag the moved rail back. When its free end coincides within 0.5
      studs of the partner's free end, both dots disappear (re-linked).
- [ ] Verify linked state by checking: Attempting to drag either rail no
      longer fires connection snap at that end (it's linked again).

---

## 3. Rulers

### 3.1 Linear ruler draw + display
- [ ] Select Line tool. Click + drag to draw a linear ruler.
- [ ] Release: ruler renders with distance label in the middle.
- [ ] Verify the distance is correct for the two endpoints.

### 3.2 Ruler editing (double-click or context menu "Properties...")
- [ ] Color + thickness changes take effect after OK.
- [ ] Distance/unit flag toggles the label visibility.
- [ ] Unit combobox switches between studs, plates, bricks, cm, in, ft.
- [ ] Font + font color change takes effect.

### 3.3 Ruler attachment (detach path)
- [ ] Select a ruler attached to a brick.
- [ ] Properties → Detach Endpoint 1. Save, reload. Endpoint shows a
      free-anchor marker (orange) instead of attached (green).

### 3.4 Ruler attachment rendering (auto-follow)
Set up: a ruler with both endpoints attached to bricks.

- [ ] Move one attached brick. Ruler endpoint follows — the ruler line
      always runs between the current brick centres.
- [ ] Distance label updates to match the new distance.
- [ ] Undo the move. Ruler endpoint snaps back.
- [ ] Save; vanilla BlueBrick still opens the `.bbm` with the expected
      attached endpoints.

### 3.5 Ruler offsetting
- [ ] Linear ruler with `allowOffset=true`, `offsetDistance=-48`. The
      visible line draws OFFSET from the anchor points, with dashed
      guide-lines connecting the anchors to the line.
- [ ] Anchor-point markers (orange / green dots) appear at the actual
      anchors when offset is on.

### 3.6 Circular ruler
- [ ] Select Circle tool. Click + drag to define centre + radius.
- [ ] Circle renders with radius label to the right of centre.
- [ ] Radius label updates if you edit `radius` via Properties.

---

## 4. Save / load / round-trip

### 4.1 Save
- [ ] File → Save on a new document prompts for a file name.
- [ ] Saves a `.bbm` and (if sidecar content exists) a sibling `.bbm.bld`.
- [ ] Reopen the file → everything appears the same.

### 4.2 Byte-exact round-trip
- [ ] Pick any fixture from `fixtures/bbm-corpus/`. Open it.
- [ ] Save it to a new location without any edits.
- [ ] `diff original.bbm saved.bbm` should show no differences.

### 4.3 Autosave
- [ ] Edit a document. Wait 60 seconds.
- [ ] Quit the app without saving.
- [ ] Relaunch. App prompts to restore the autosave. Click yes — edits
      should be present.
- [ ] Do a rapid burst of edits (move, rotate, paste) — autosave fires
      at most once per 5 seconds via the undo-stack hook, so you lose
      at most 5 s of work on a crash (kill -9 the process to test).
- [ ] After crash-kill and relaunch, restore dialog offers the
      partially-edited state.

### 4.4 Open Recent
- [ ] File → Open Recent lists the last 12 files opened.
- [ ] Clicking an entry opens that file.
- [ ] Entries persist across app restarts.

### 4.5 Forward-compat (vanilla BlueBrick opens our `.bbm`)

We can't automate this on CI — BlueBrick is a Windows Forms GUI app
with no CLI validation mode, and Windows UI automation is too brittle
to include in the pipeline. Byte-exact round-trip (tested by
`ctest`) is the main guarantee: if we produce bytes identical to
vanilla, vanilla can read them. This section is the human backstop
for paths the round-trip corpus doesn't cover (new features, fresh
files built from scratch).

**Running vanilla BlueBrick on Linux/macOS**: the repo ships
[`scripts/run-vanilla-bluebrick.sh`](../scripts/run-vanilla-bluebrick.sh)
— pass it a `.bbm` and it launches BlueBrick.exe under plain `wine`
or (with `PROTON=1`) Proton-GE. Point `BLUEBRICK_EXE=/path` at a
custom install location, or stash the BlueBrick.1.9.2 folder under
`~/Documents/` / `~/Applications/` / `/opt/` for auto-detection.

**Minimum smoke set** — do these before every release tag:

- [ ] On Windows with BlueBrick 1.9.2 installed, save a fresh
      document from this app with: 3+ brick layers, a ruler,
      2+ area cells, a couple of text labels.
- [ ] Open that `.bbm` in vanilla BlueBrick. No error dialogs.
      Scene renders; z-order matches; brick positions identical
      (measure with BlueBrick's ruler tool).
- [ ] Save it in vanilla (any trivial no-op change + Save). Reopen
      here. No error dialogs; scene unchanged.

**Feature coverage** — add these when a release introduces new output:

- [ ] Modules with cross-layer members round-trip: save here →
      open in vanilla → expect per-layer Groups (flattened view).
      Save in vanilla → open here → module reconstructs via sidecar.
- [ ] Anchored labels: vanilla sees them as ordinary text at their
      snapshot world positions (frozen on save). Edit them in
      vanilla → reopen here; offer to re-link by proximity
      (acknowledge the prompt).
- [ ] Venue: `.bbm` carries no venue (by design — sidecar only).
      Vanilla should open without error; our `.bbm.bld` preserves
      the venue across round-trips.
- [ ] **Save Selection as Set**: export a mixed straight + curve +
      switch selection. Drop the `.set.xml` under
      BlueBrick's `parts/` folder, reload BlueBrick, place the new
      set from its Parts library. Tracks should align flush (no
      gaps) — same as what we see when placing the set here.

---

## 5. Modules

### 5.1 Create from selection
- [ ] Select a few bricks across layers. Edit → Modules → Create from
      Selection. Give it a name.
- [ ] Modules panel shows the new module with its member count.
- [ ] The module frame + name render on the map (if Module Names is on).

### 5.2 Move as unit
- [ ] Modules panel right-click → Select Members. Drag any member.
- [ ] Entire module moves as a rigid group.

### 5.3 Flatten
- [ ] Right-click module in panel → Flatten. Module disappears from the
      panel; member bricks remain on their layers.

### 5.4 Delete
- [ ] Right-click module in panel → Delete. Both the module entry AND
      its member bricks are removed. Undo restores both.

### 5.5 Brick delete → module auto-cleanup
- [ ] Create a module with 2 bricks. Select and delete both bricks.
- [ ] The now-empty module disappears from the Modules panel
      automatically (no zombie entry).

### 5.6 Module library
- [ ] Modules panel → Save to Library on a module writes a `.bbm` to
      the configured library folder.
- [ ] Drag from Module library panel onto the map imports the module at
      the drop position.

### 5.7 Save Selection as Set (BrickTracks-style `.set.xml`)
- [ ] Place several track pieces (mix of straights + curves + switches).
- [ ] Select them all (Ctrl+A or rubber band).
- [ ] Modules → **Save Selection as Set...** Enter a name.
- [ ] Choose a target under a user library path (Libraries dialog →
      add a folder if needed). Save.
- [ ] Status bar: `Saved set <name> (N subparts) to <path>`.
- [ ] Parts panel auto-rescans; the new set appears under its library
      with the name you entered.
- [ ] Drag the new set from the Parts panel onto the map. Every
      subpart appears at the correct position, rotations preserved,
      and the internal connections between tracks show yellow dots
      (not red) — no visual gaps at the joints.
- [ ] `.set.xml` file on disk uses 6-decimal position precision and
      `<group><SubPartList><SubPart id="..."><position>/<angle></SubPart>`
      schema matching the BrickTracks / TrixBrix sets shipped with
      BlueBrick 1.9.2.
- [ ] Opening the same `.set.xml` in vanilla BlueBrick 1.9.2 (or
      expanding it there) also produces a correctly-aligned layout.

---

## 6. Venues (sidecar-only)

### 6.1 Draw outline
- [ ] Map → Venue → Draw Outline... Click points on the map, right-click
      to finish.
- [ ] Venue outline appears underneath the bricks.
- [ ] Walkway buffer renders as a translucent band on the inside of
      non-Wall edges.

### 6.2 Draw by dimensions
- [ ] Map → Venue → Draw Outline by Dimensions... Fill in lengths +
      angles (ft, in, or stud). OK.
- [ ] Outline appears with the correct size and edge classifications.

### 6.3 Edge classes
- [ ] Edit Venue Properties... classify edges as Wall / Door / Open.
- [ ] Walls render solid; doors as gaps; open edges dashed.

### 6.4 Obstacles
- [ ] Add Obstacle... click points to add a pillar polygon.
- [ ] Renders as a hatched polygon.

### 6.5 Save / load venue
- [ ] Save Venue to Library → give name → listed in the library folder.
- [ ] Load Venue from Library → pick saved venue → applies to the project.

### 6.6 Clear venue
- [ ] Clear Venue confirms, then removes the venue. Sidecar still saves
      cleanly without any venue section.

### 6.7 Venue label sizing
- [ ] Preferences → Appearance → "Venue label size" slider (10-96 px)
      changes the edge measurement label font after OK.
- [ ] Default is 28 px; setting persists across launches.

### 6.8 Walkway validator (non-blocking warnings)
- [ ] With a venue present, the status bar shows "Venue: OK" or
      "Venue: N issue(s)" with tooltip listing the first ~12 violations.
- [ ] Place a brick outside the venue outline — warning count increments.
- [ ] Place a brick within `minWalkwayStuds` of a Door or Open edge —
      warning count increments with a distance-vs-buffer message.
- [ ] Place a brick on top of a VenueObstacle polygon — warning count
      increments, message names the obstacle label when set.
- [ ] Validator is non-blocking: you can still place the brick; only
      the warning changes.
- [ ] Toggling the venue off (`enabled = false`) clears the status
      label; toggling on re-runs validation.

---

## 7. Keyboard shortcuts

- [ ] Ctrl+N — new document
- [ ] Ctrl+O — open
- [ ] Ctrl+S — save
- [ ] Ctrl+Shift+S — save as
- [ ] Ctrl+Z / Ctrl+Y — undo / redo
- [ ] Ctrl+X / C / V — cut / copy / paste
- [ ] Ctrl+D — duplicate selection
- [ ] Ctrl+A — select all
- [ ] Ctrl+Shift+A — deselect all
- [ ] Ctrl+G / Ctrl+Shift+G — group / ungroup
- [ ] Ctrl+F — Find & Replace
- [ ] Ctrl+P — Select Path (extends selection along connected bricks)
- [ ] Ctrl+T — Add Text
- [ ] Ctrl+L — Add Anchored Label
- [ ] Ctrl+, — Preferences
- [ ] Ctrl+Shift+] / [ — bring to front / send to back
- [ ] Delete — remove selection
- [ ] R / Shift+R — rotate CCW / CW
- [ ] Arrow keys (in Move Step submenu) — nudge by 1 stud

---

## 8. Zoom & pan

### 8.0 Scale indicator
- [ ] A scale bar + studs/mm-or-metres label is always visible in the
      lower-left corner of the map view.
- [ ] Zooming changes which round step gets shown (1, 2, 5, 10, 16,
      32, 48, 64… studs) so the bar stays roughly 40-320 viewport px wide.

- [ ] Mouse wheel up — smooth zoom in (cursor-anchored).
- [ ] Mouse wheel down — smooth zoom out.
- [ ] Wheel is ZOOM ONLY. Trackpad two-finger horizontal scroll,
      high-res-wheel partial notches, and any wheel event with no
      vertical delta DO NOT pan the map. Middle-click drag is the
      only pan gesture.
- [ ] Middle-click drag — pan.
- [ ] F — Fit to View, repositions to show everything.
- [ ] Ctrl+= / Ctrl+- — zoom in / out.
- [ ] Zoom clamped at ~0.02× to ~40×; doesn't stop working beyond content.
- [ ] A huge single wheel gesture (trackpad flick, momentum scroll) is
      capped to ~2× per event — you can't accidentally zoom across
      the entire range in one frame and teleport far from the cursor.

---

## 9. Parts panel

- [ ] Fuzzy filter: "plt2" matches "plate2x4". Best match appears first.
- [ ] Category dropdown limits parts to that folder.
- [ ] Drag a part onto the map — dropped at cursor position.
- [ ] Drop near a free compatible connection — connection snap fires.
- [ ] Dropping onto a Table (no conns) grid-snaps instead.
- [ ] Set parts (parts/*/*.set.xml) expand into their constituent bricks
      wrapped in an auto-module on placement.

---

## 10. Layers panel

- [ ] Show all / Solo toggles visibility.
- [ ] +, - add and remove layers.
- [ ] Up/down arrows reorder layers.
- [ ] Right-click a layer → Rename.
- [ ] α80 suffix indicates 80% transparency on that layer.
- [ ] Clicking a layer name in the panel makes it the "active" layer
      (shown bold).

### 10.1 Layer Options dialog (right-click → Options..., or double-click)

Base fields (all layer kinds):
- [ ] Name — rename via the dialog's text field persists.
- [ ] Transparency slider (0-100 %) takes effect on OK.
- [ ] Visible checkbox mirrors the visibility checkbox on the panel row.
- [ ] Display selection hulls + Hull thickness apply to the layer's
      contents on OK.

Grid layer extras:
- [ ] Cell size, Grid-line thickness, Sub-divisions per cell take effect.
- [ ] Display grid / Display sub-grid / Display cell index labels
      toggle render on/off.

Brick layer extras:
- [ ] Display brick elevation labels — toggles the altitude numbers
      under every brick on that layer.

Area layer extras:
- [ ] Paint cell size (studs) — reopen dialog to verify the new value
      persists. (Existing painted cells stay at their old indexing;
      the dialog has a note about this.)

Text / Ruler layers have no kind-specific extras beyond the base fields.

---

## 10.5 Used Parts panel

- [ ] View → Panels → Parts list (or the toolbar's Panels button) toggles the dock on/off; a hidden panel comes back on the right.
- [ ] Table shows an icon, part number, count, description, budget,
      and over-budget delta for every part currently on the map.
- [ ] Default sort is count descending.
- [ ] Clicking any column header re-sorts by that column.
- [ ] Typing in the filter box narrows by part number or description
      substring (case-insensitive).
- [ ] Typing `over` filters to just the over-budget entries.
- [ ] Rows for over-budget parts render with a pink background + red
      "Over" count.
- [ ] Double-click (or context-menu "Select All of This Part") selects
      every brick of that part on the map.
- [ ] Summary line at the bottom shows distinct-part count, total
      brick count, and over-budget-kind count.
- [ ] Refreshes automatically after any undo-stack change (add/delete
      a brick → counts update).

## 11. Module library panel

- [ ] Folder… opens the configured library folder in the OS file browser.
- [ ] Save to Library writes current selection as a module `.bbm`.
- [ ] Drag a library entry onto the map to import it as a module.
- [ ] Re-scan library path from Preferences picks up new `.bbm` files
      without a restart.

---

## 12. Preferences (Ctrl+,)

- [ ] General tab: language combobox persists across launches.
- [ ] Editing tab: snap step / rotation step changes take effect
      immediately on next drag.
- [ ] Appearance tab: accent color updates affect selection outline.
- [ ] Library tab: "Manage Parts Libraries..." opens the library paths
      dialog.
- [ ] Closing Preferences rebuilds the scene so live settings apply
      without needing a reopen.

---

## 12.5 Budget (Ctrl + Budget menu)

- [ ] Budget → Open Budget Editor opens the dialog.
- [ ] Load an existing `.bbb` file; table populates with PartNumber /
      Used / Limit columns.
- [ ] Rows turn pink where Used > Limit.
- [ ] Edit a Limit inline — changes apply immediately; save to `.bbb`
      via Save button.
- [ ] Status-bar "Budget: OK" / "Budget: N over" readout shows the
      current state even when the dialog is closed. Hover for the
      list of over-budget parts (first 15).
- [ ] Last-used budget path auto-loads on app start (QSettings key
      `budget/lastFile`).

## 13. Export PDF

- [ ] File → Export as PDF... prompts for a filename.
- [ ] Page orientation is chosen to match the layout's aspect ratio
      (wider-than-tall → landscape; taller → portrait).
- [ ] Output is A3 by default, fitting the layout with ~12 mm margin.
- [ ] PDF opens in any viewer and the layout is crisp even when zoomed
      in (high-resolution rendering).
- [ ] Watermark toggle / venue background render as they do on screen.

## 13. Export image

- [ ] File → Export as Image... opens a dialog with width/height,
      keep-aspect, watermark, transparent, antialias checkboxes.
- [ ] Path "Browse..." opens a file save dialog.
- [ ] Export writes a PNG of the configured size at the current scene
      bounds.
- [ ] Watermark draws author/LUG/event text in the bottom-right.
- [ ] Transparent: PNG has alpha; opens correctly in a viewer that
      supports transparency.

---

## 14. External format import → library part

Imports do NOT load a whole new map — they turn the imported file into
a single composite part in the user library (`<name>.xml`, plus a
32 px/stud `.png` and an 8 px/stud `.gif` for geometry renders, or just
the `.gif` for BlueBrickParts composites), then drop that part onto the
current map at the view centre.

The part's `<ConnexionList>` inherits every FREE external connection of
the track / road / monorail pieces BlueBrickParts knows (internal joints
are excluded via `rebuildConnectivity`), so it snaps like a
hand-authored library piece.

Useful settings (Preferences → Import): LDraw library root, Studio
library root, LDD install (program folder with `Assets.lif`, or the data
folder with `db.lif`), and an optional newer LDD `ldraw.xml`
(slswww.free.fr/ldraw.xml — LDD's own one has no train track).

### 14.1 LDraw (.ldr / .dat / .mpd)
- [ ] Tools → Import → LDraw... pick a `.ldr` file; the preview opens.
- [ ] With an LDraw library set, the sprite is seen from above: printed
      tiles read correctly, a left switch branches left, studs visible.
- [ ] A track model shows red connection rings on its free ends; they
      sit between the rails at the ends of the rendered track.
- [ ] Current LDraw numbers (e.g. 9V track `74746` / `74747`, switch
      `75542-f1`) still get connection points.
- [ ] Without an LDraw library, parts BlueBrickParts knows are
      composited at 8 px/stud; unknown parts are reported as missing.
- [ ] After saving, the part appears in the Parts panel under the
      chosen category and one copy is placed at the view centre.

### 14.2 Studio (.io)
- [ ] Tools → Import → Studio... pick a `.io` file. Same flow as LDraw;
      uses the Studio library root when set (its `UnOfficial/` parts too).
- [ ] A file saved by current Studio (every entry encrypted) imports; a
      big set (e.g. 70620 Ninjago City, 83 MB) shows progress and takes
      seconds, with its submodels and `CustomParts/` drawn.
- [ ] An archive without a `model.ldr` entry, an AES-protected file and
      a file that isn't a ZIP each surface a clear error.
- [ ] After saving, a notice offers "Send to server…" for the new part.

### 14.3 LDD (.lxf / .lxfml)
- [ ] With the LDD install set, the sprite comes from LDD's own
      geometry (both the program folder and the `db.lif` data folder
      work), seen from above and not mirrored.
- [ ] With the community `ldraw.xml`, 9V / RC track in an LDD model gets
      connection points (with or without an LDraw library).
- [ ] With only `ldraw.xml` (no LDD database) plus an LDraw library, the
      model renders from LDraw geometry instead.
- [ ] .lxfml (raw XML without the surrounding ZIP) also works.
- [ ] Minifigures (assemblies such as torso 76382) are drawn, not
      reported missing.

### 14.4 Preview edits
- [ ] Rotate ⟲ / ⟳ turns sprite, footprint and connection rings.
- [ ] Unchecking a connection drops it (dashed grey ring); the saved
      part has only the checked ones.
- [ ] Category picker lists `imports` and its sub-folders; a new name
      creates a folder and a new Parts-panel category.
- [ ] Re-importing under an existing name offers "Replace"; replacing
      updates copies already on the map, otherwise a `-2` copy is saved.

### 14.5 Batch import
- [ ] Tools → Import → Batch Import... select several model files, pick
      a category; one progress dialog, cancellable between files.
- [ ] Summary lists imported parts (size, connection count) and any
      failures with reasons.

---

## 15. Dock panel layout persistence

- [ ] Re-dock / undock any panel. Close + relaunch.
- [ ] Layout restores to the saved configuration.
- [ ] View menu shows each dock's visible state correctly.
- [ ] "Map Scroll Bars" toggle: on = scrollbars visible, off = hidden
      (middle-click pan always works regardless).

---

## 16. Find & Replace

- [ ] Ctrl+F opens the modeless dialog.
- [ ] Type in the text search box — matches highlight in the map as
      you type (200 ms debounce on keystrokes).
- [ ] Changing the scope (Text content / Part number) or the case
      checkbox re-runs the search automatically.
- [ ] Match count label ("N match(es).") updates live.
- [ ] Editing the map while the dialog is open re-runs the search on
      the next undo-stack tick (so the match list stays accurate).
- [ ] Replace button swaps text on the currently-matched item.
- [ ] Replace All sweeps every match.
- [ ] Dialog stays open while you edit the map; editing doesn't crash
      or orphan the dialog.

---

## 17. Cross-platform checks

Before tagging a release, verify each platform-specific item on the
actual target OS (not just via CI artifacts):

### 17.1 Linux (Ubuntu 22.04+ / Manjaro)
- [ ] AppImage / tarball runs without extra dependency install.
- [ ] Double-clicking a `.bbm` opens it (desktop entry + MIME type).
- [ ] Wayland + X11 both work.
- [ ] File dialogs use the native GTK/KDE picker when available.

### 17.2 Windows (10 + 11)
- [ ] `.exe` runs self-contained (no "missing Qt DLL" / "VCRUNTIME140.dll" dialog)
      from both the `.zip` and the MSI, and finds the bundled parts library.
- [ ] MSI upgrade over the previous release replaces it (no second entry
      in Apps & Features); double-clicking a `.bbm` opens it.
- [ ] UNC paths in recent files work.
- [ ] Drag-out-to-delete doesn't break on the Windows shell.

### 17.3 macOS (universal: arm64 + x86_64)
- [ ] The one universal `.app` opens on Apple Silicon and Intel.
- [ ] Double-clicking a `.bbm` in Finder opens it (QFileOpenEvent).
- [ ] UI appears translated when the language preference is non-English.
- [ ] File → Open dialog uses the native macOS picker.
- [ ] Ctrl-click = right-click still triggers context menus.
- [ ] macdeployqt bundled Qt libraries, not pulled from system.

---

## 18. Stress / edge cases

- [ ] Very large layout (1000+ bricks): pan and zoom remain responsive.
- [ ] Very small zoom (0.02×): bricks still render (cosmetic pens
      prevent hairline disappearance).
- [ ] Paste into a document that doesn't have the clipboard's source
      layer — layers auto-created, bricks land on them.
- [ ] Import module that uses parts not in the library — modal listing
      the missing parts appears; import proceeds with those as
      placeholder rects.
- [ ] Save document to a read-only location → error dialog, not a crash.
- [ ] Open a corrupted `.bbm` (truncate a fixture) → error dialog, not
      a crash.

---

## 19. Render-goldens (regression vs parity)

[`fixtures/render-goldens/`](../fixtures/render-goldens/) holds the
reference PNG for every `.bbm` in the corpus. The automated test
`RenderGoldens.*` renders each fixture at 1600 × 1200 via the
offscreen Qt platform and pixel-diffs vs the reference.

**Opt-in**: the harness skips unless `BLD_ENABLE_RENDER_GOLDENS=1`
is set when you run `ctest`. Cross-environment pixel parity isn't
achievable (Qt version + font hinting + Mesa differences), so CI
doesn't gate on these. Treat them as a local regression check on
the same box you captured from. Run with:

```sh
BLD_ENABLE_RENDER_GOLDENS=1 ctest --test-dir build -R RenderGoldens
```

The current references are captures of what *we* render today —
they're a **regression** gate (did anything drift?), not a parity
gate (does it match BlueBrick?). Turning them into a parity gate is
a manual one-time capture step:

- [ ] On Windows with vanilla BlueBrick 1.9.2 installed, for each
      fixture under `fixtures/bbm-corpus/*.bbm`:
      - Open in BlueBrick.
      - Export / screen-capture the rendering at a size matching
        the test harness (`kGoldenWidth × kGoldenHeight` in
        [`tests/rendering/RenderGoldensTest.cpp`](../tests/rendering/RenderGoldensTest.cpp)).
      - Save as `<fixture-stem>.png` under `fixtures/render-goldens/`,
        overwriting the existing capture.
- [ ] Run `ctest -R RenderGoldens` locally. Failures are diffs
      against the new vanilla reference — each one is either a
      genuine parity gap to fix or a tolerance to widen (tweak
      `kChannelTolerance` / `kMaxPixelDiffFraction` in the test).
- [ ] Commit the PNGs. From now on the harness enforces vanilla
      parity rather than just "no internal drift".

When making an intentional renderer change:

- [ ] `cmake --build build` locally.
- [ ] `scripts/capture-render-goldens.sh` regenerates every
      reference from the current build.
- [ ] Eyeball the diffs (git diff on a PNG won't help — use an image
      diff tool, or open old vs new side-by-side).
- [ ] Commit only after you've confirmed the new renders are what
      you intended.

---

## When you find a regression

1. Note the checklist line that failed and the steps.
2. If it's visual, take a before/after screenshot.
3. If it's tied to a specific `.bbm`, stash a copy in `fixtures/bbm-corpus/`
   with a filename describing the bug.
4. File an issue, or if you're comfortable with the codebase, open a
   PR with both a fix and a regression test that would have caught it.

Reminder: this fork is AI-assisted. Every reported bug is valuable —
the automated tests cover the parts of the system that are testable
without a GUI, but the rest of the surface area is only as
well-verified as this checklist keeps it.
