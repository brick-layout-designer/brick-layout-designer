# `.bbm.bld` sidecar — fork-only metadata

Companion doc to [bbm-schema.md](bbm-schema.md) (the vanilla `.bbm` format).
This file documents the **`.bbm.bld` sidecar**: where fork-only constructs
(cross-layer modules, anchored text labels, event venues) live so that
vanilla BlueBrick 1.9.2 can still open the `.bbm` cleanly.

The formal schema is in [bbm-bld-schema.json](bbm-bld-schema.json) (JSON
Schema draft-07). This document is the prose reference.

## Why a sidecar

Adding any new element kind to the `.bbm` root would break vanilla
BlueBrick's XML deserializer. So we split the save:

- `foo.bbm` — pure vanilla XML. Byte-faithful with upstream 1.9.2
  (data version 9). See [bbm-schema.md](bbm-schema.md).
- `foo.bbm.bld` — JSON, ignored by vanilla. Holds everything vanilla
  doesn't know about.

On save, we *also* forward-project the fork-only metadata into the
`.bbm` in a vanilla-compatible way:

- **Anchored labels** → written as ordinary `<TextCell>` items at
  their snapshot world position. Vanilla renders them in place; on
  next load here the sidecar re-attaches them to their targets.
- **Modules** → each module's per-layer subset becomes a vanilla
  same-layer `<Group>` on that layer. Vanilla sees sensible grouping
  within each layer; the sidecar reconstructs the cross-layer identity.
- **Venues** → not projected; vanilla has no venue concept. Lives in
  the sidecar only.

## Top-level shape

```json
{
  "schemaVersion": 1,
  "bbmHashSha256": "<lowercase-hex>",
  "anchoredLabels": [ ... ],
  "modules": [ ... ],
  "venue": { ... }
}
```

All fields other than `schemaVersion` are optional. A project with no
fork-only metadata has **no sidecar at all** — the file just isn't
written. Writers MUST NOT emit an empty sidecar.

### `schemaVersion` (integer, required)

The only version shipped is `1`. Readers must reject files with
`schemaVersion` greater than their highest supported version (forward
compat rule — newer schemas might add required fields whose absence
would silently lose data).

Adding a new *optional* field to the schema (e.g. extending a Venue
with a new property) does NOT bump the version; older readers should
tolerate (ignore) unknown keys.

### `bbmHashSha256` (string, optional)

Lowercase hex SHA-256 of the sibling `.bbm`'s bytes at write time.
On load we recompute the current `.bbm` hash and compare:

- **Match** → sidecar is in sync, anchored-label `targetId`s still
  point at the right bricks.
- **Mismatch** → the `.bbm` was edited externally (e.g. re-saved by
  vanilla BlueBrick). Some anchor targets may no longer resolve.
  The UI offers to re-link by spatial proximity.
- **Empty / missing** → drift detection skipped (writer didn't have
  the `.bbm` bytes available, for instance).

## Anchored labels

```json
{
  "id": "uuid-string",
  "text": "Main Station",
  "font":  { "family": "Arial", "size": 12.0, "style": "Bold" },
  "color": { "known": true, "argb": -16777216, "name": "Black" },
  "kind": 1,
  "targetId": "brick-guid",
  "offset":  { "x": 2.0, "y": -2.0 },
  "rot": 0.0,
  "minZoom": 0.0
}
```

### `kind` — `AnchorKind` enum

| Value | Name   | Meaning |
|------:|:-------|:--------|
| 0     | World  | Label lives at `offset` in WORLD stud coords. Doesn't move. |
| 1     | Brick  | `offset` is in the target brick's LOCAL coords (studs). Label translates + rotates with the brick. |
| 2     | Group  | Attached to an upstream same-layer `<Group>` (same model as Brick). |
| 3     | Module | Attached to a cross-layer `Module` (this sidecar's modules array). |

`targetId` is the guid of the anchor target — ignored when `kind == World`.

### Forward-compat projection

On `.bbm` save, every `AnchoredLabel` with `kind != World` gets flattened
to a `<TextCell>` at its CURRENT resolved world position (target position
+ rotated offset). Vanilla reads that text cell and renders it where we
last saw it. When *this* fork re-loads the `.bbm.bld`, we throw away
the flattened world position and use the sidecar target+offset to
recompute the live position — so the label tracks its anchor as the
anchor moves.

## Modules

```json
{
  "id": "uuid-string",
  "name": "Kelly's Corner",
  "members": ["brick-guid-1", "brick-guid-2", "text-guid-3", "..."],
  "transform": [1, 0, 0, 0, 1, 0, 0, 0, 1],
  "sourceFile": "/home/aron/modules/R104-corner.bbm",
  "importedAt": "2026-04-19T18:42:00"
}
```

- `members` is a flat list of item guids, which may span layers.
- `transform` is a 3×3 QTransform in row-major order
  (`[m11, m12, m13, m21, m22, m23, m31, m32, m33]`). Identity is the
  one shown above. The members' stored positions in the `.bbm` are the
  *current* (post-transform) positions — the transform records
  "how far has this module been rotated / translated from its reference
  orientation" so we can undo a group rotate without accumulating
  round-trip drift.
- `sourceFile` is set when the module was imported from a standalone
  `.bbm`. Allows a "re-scan" operation to pull fresh content from the
  source while preserving member identity via the id map
  (`ImportBbmAsModuleCommand` in `src/edit/`).
- `importedAt` is ISO-8601. Absent when the module was created from
  selection rather than imported.
- How the placed module looks and behaves (optional; each is written
  only when it differs from the default, so older files read as before):
  - `showName` (bool, default `true`): its name shows on the map
    (View › Module Names still applies to every module).
  - `outlineColor`, `nameColor` (`"#rrggbb"`, default the light blue
    `rgba(100,180,255)`): drawn at the default look's opacity (0.8 for
    the outline, 0.9 for the name).
  - `sameColor` (bool, default `true`): the two colours change together.
  - `pinned` (bool, default `false`): the module can't be moved or turned
    as a whole; Edit module still changes its parts.
- Fields a reader doesn't know are kept when it writes the module again.

### Forward-compat projection

Each module writes one vanilla `<Group>` per affected layer, listing
that layer's member items. Vanilla users see a clean per-layer group
per layer. The sidecar `modules` array preserves the cross-layer
identity so the fork reconstitutes the single `Module` on re-open.

If the sidecar is missing or stale, the fork treats the per-layer
groups as independent (no Module entry) and surfaces a
"reconstruct module?" prompt based on co-naming / co-creation heuristics.

## Venue

```json
{
  "venue": {
    "name": "Smith Hall, SE corner",
    "enabled": true,
    "minWalkwayStuds": 112.5,
    "bounds": { "x": 0, "y": 0, "w": 800, "h": 600 },
    "edges": [
      {
        "kind": 0,
        "doorWidthStuds": 0.0,
        "label": "North wall",
        "poly": [ { "x": 0, "y": 0 }, { "x": 800, "y": 0 } ]
      },
      {
        "kind": 1,
        "doorWidthStuds": 36.0,
        "label": "Main entrance",
        "poly": [ { "x": 0, "y": 300 }, { "x": 0, "y": 336 } ]
      }
    ],
    "obstacles": [
      {
        "label": "Pillar",
        "poly": [ { "x": 300, "y": 200 }, { "x": 320, "y": 200 },
                  { "x": 320, "y": 220 }, { "x": 300, "y": 220 } ]
      }
    ]
  }
}
```

### `kind` — `EdgeKind` enum

| Value | Name | Meaning |
|------:|:-----|:--------|
| 0     | Wall | Solid barrier. Bricks may butt against it (no walkway buffer). |
| 1     | Door | Opening. Requires `doorWidthStuds > 0`. Walkway buffer applies. |
| 2     | Open | No barrier — indicates a viewing side. Walkway buffer applies. |

### Units

Everything in venue geometry is in **studs** (1 stud = 8 mm). The
Venue Dimensions dialog accepts feet or inches and converts to studs
on input.

`minWalkwayStuds` is the minimum clearance from the inside of any
non-Wall edge (so walls don't need a walkway buffer). The walkway
validator flags bricks that encroach on this buffer; non-blocking —
the user can ignore warnings and place bricks anywhere.

### Forward-compat projection

None. Vanilla has no venue concept, so the venue lives in the sidecar
exclusively. A `.bbm` from a project with a venue, opened in vanilla,
shows the bricks and a blank background where the venue would be.

## Standalone `.bld-venue`

Venues can be exported / imported as standalone `.bld-venue` files so
users can share templates across projects. Format:

```json
{
  "schemaVersion": 1,
  "venue": { ... same shape as the .bbm.bld venue ... }
}
```

See [`src/saveload/VenueIO.cpp`](../src/saveload/VenueIO.cpp). Not a
pair with a `.bbm` — just a venue, on its own.

## Versioning

`schemaVersion = 1` is the only version shipped. Handled the same as
`.bbm` versioning: reject files newer than the highest supported
version; tolerate (ignore) unknown fields in otherwise-compatible
files.

## Round-trip guarantees

- **`.bbm` byte-faithful** — vanilla BlueBrick opens any `.bbm` this
  fork writes, identically to what vanilla itself would write for the
  same data. Enforced by the round-trip test suite against a corpus.
- **`.bbm.bld` not byte-stable** — JSON key order, whitespace, and
  number-format details are not guaranteed stable across writes. The
  *content* round-trips losslessly; the bytes do not. Sidecar hashing
  is done against the `.bbm`, not the `.bbm.bld` itself.

## Open questions / future work

- **Path normalization** for `Module.sourceFile` — currently stored as
  whatever path the user provided. Cross-machine sharing would need a
  relative-path convention or a workspace-relative root.
- **Anchor re-link by proximity** on hash mismatch — the UI prompt
  exists in design but the actual nearest-brick fallback isn't wired up
  yet.
- **Multi-venue** — reserved; current schema has `venue` as at most one
  per project (hence the single `"venue": ...` key). If we ever support
  multiple venues we'd add a parallel `"venues": [...]` array and bump
  `schemaVersion`.
