# The layout file (`.bld-layout`)

Brick Layout Designer saves a layout as a single `.bld-layout` file. Both the
desktop and the web app read and write it. A BlueBrick `.bbm` is still
available as an export (File › Export as BlueBrick Map), and it keeps only
what BlueBrick supports.

## Container

A `.bld-layout` file is a plain ZIP archive (PKZIP 2.0: no ZIP64 and no
encryption). Entry names are UTF-8. Each entry is either stored or deflated.
Readers must accept both, and must ignore entries they don't know.

| Entry | Required | Contents |
|---|---|---|
| `manifest.json` | yes | `{"format":"bld-layout","version":1,"generator":"…"}`, and `source` when the layout is on a server (see below). Writers put it first and store it uncompressed. |
| `layout.bbm` | yes | The map exactly as a `.bbm` file holds it ([bbm-schema.md](bbm-schema.md)). |
| `sidecar.json` | no | Labels, modules, venue and background image. Same shape as the `.bbm.bld` sidecar ([bbm-bld-schema.md](bbm-bld-schema.md)), with two differences listed below. Left out when the layout has none of these. |
| `background.<ext>` | no | The background image's bytes, named by `sidecar.json`'s `backgroundImage.file`. |
| `parts/<file>` | no | The parts the layout uses that aren't in the bundled BlueBrick library. Each is a `<PartNumber>.<Color>.xml` (`.set.xml` for a set) with the sprites beside it (`.png`, `.gif`, `.jpg`). Sets bring their subparts too. |

`sidecar.json` differs from a `.bbm.bld` sidecar in two ways:

- It has no `bbmHashSha256`. The layout is in the same file, so it can't drift.
- In `backgroundImage`, `file` (the entry name) replaces `path`, a location on
  one machine. Only a writer that couldn't read the image keeps `path`, and it
  warns that it did.

## Parts

`parts/` names are plain file names: no folders, no leading dot, and one of
the part extensions. Readers skip any other name and warn.

- **Saving.** The desktop carries every part the layout uses from outside the
  bundled library: imported parts, your own folders and server parts.
- **Opening.** It writes the parts its library lacks to `layout-parts/` in its
  app data folder, which joins the library paths, before the map loads.
  Where it already has a part of that number with different XML, it shows
  both before the map loads: each sprite, description and author side by
  side, with a choice for each part.
  - **Keep mine** (the default) leaves the library as it is.
  - **Use the layout's** copies your part's files to
    `replaced-parts/<yyyyMMdd-HHmmss>/` in the app data folder, then writes
    the layout's in their place.
  - **Keep both** adds the layout's under the next free number,
    `<PartNumber>-2.<Color>` (then `-3` and on), in `layout-parts/`. The
    opened layout's bricks and groups switch to that number.
  - **Keep All Mine** closes the window with nothing changed. The status bar
    says what was done.
- **The web app** carries the custom parts a layout uses. When it opens a
  file, it uploads the parts the server lacks as custom parts.

## Where it came from (`source`)

A file saved from a layout on a server says which one, in `manifest.json`:

```json
"source": {
  "server": "https://collab.aronwk.com",
  "layoutId": "…",
  "title": "Show 2026",
  "exportedAt": "2026-10-01T12:00:00.000Z"
}
```

- `server` is the server's base address: scheme, host and port, with no path
  and no trailing slash.
- `layoutId` is the layout's id on that server.
- `title` is the layout's title when the file was saved.
- `exportedAt` is when it was saved, as ISO 8601 in UTC with milliseconds.

**Writing.**
- The web app writes `source` on every download and on Save's offline copy,
  because every web layout is a server layout.
- The desktop writes it when it saves a live layout, and when it saves a file
  that already had a `source`. That `source` is kept unchanged, with its
  `exportedAt`.
- Purely local layouts have no `source`.

**Reading.**
- `source` is optional and additive. A reader that doesn't know it ignores
  it, and the layout opens exactly as it does without it.
- Readers ignore a `source` that has no http(s) `server` or no `layoutId`.

**Opening.**
- **The desktop** shows a card and never connects by itself.
  - When `server` is one of your servers (File › Servers), the card says
    "This layout is on *name*" and offers **Open the live version** or
    **Keep working on this copy**.
  - Otherwise it offers **Add *host* to your servers**. After that it offers
    the live version.
- **The web app**, importing a file whose `server` is the same server and
  whose layout you can open, offers **Open the original** above the
  import form. Importing it as a copy stays the default.

**Unknown fields.** Writers keep the `manifest.json` fields they don't know
when they save a file they opened. Each writer always sets its own `format`,
`version`, `generator` and `source`.

## Versions

`version` is 1. A reader opens a file with a higher version but warns that
anything it doesn't understand is left out. A reader refuses a file whose
`manifest.json` is missing or has a different `format`.

## Opening and saving

- **Opening.** The desktop unpacks the background image into its app data
  folder (`layout-assets/<sha256>.<ext>`) and points the layout at that file.
- **Saving.** New layouts save as `.bld-layout`. Saving a layout that was
  opened from a `.bbm` (or another map format) asks once whether to switch to
  `.bld-layout` or keep the old format.
- **Autosave.** Autosave writes a `.bld-layout`, so a restore brings back the
  labels, modules and venue too.

## Fixtures

These fixtures are in `fixtures/layouts/` here and in the web repository's
`packages/bbm/tests/fixtures/`, and both apps' tests read both:

- **`corner-lobby.bld-layout`**, made by the desktop (tests/import/LayoutFileTest.cpp
  with `BLD_UPDATE_FIXTURES=1`). It holds `tight-corner.bbm`, a label, a
  module, the Grand Lobby venue and a 4×4 background image.
- **`web-made.bld-layout`**, the same layout opened by the web app and
  downloaded again (the web's `apps/web/scripts/make-web-made-layout.ts`).
- **`with-parts.bld-layout`**, one brick of `CLDTEST.1`, a part the file
  carries. The web's e2e opens it and gets `CLDTEST.1` as a custom part.
- **`web-made-source.bld-layout`** and **`desktop-made-source.bld-layout`**
  hold `tight-corner.bbm` saved by each app as server layout `L-42` on
  `https://collab.example.org`, with a `futureField` neither app knows. The web
  one comes from `apps/web/scripts/make-web-made-source-layout.ts`, the desktop
  one from LayoutFileTest with `BLD_UPDATE_FIXTURES=1`. Each app reads both and
  checks the same `source`. The desktop also checks that a re-save keeps
  `futureField` and `source`, and that the layout opens the same with
  `source` stripped.
