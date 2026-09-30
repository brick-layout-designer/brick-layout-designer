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
| `manifest.json` | yes | `{"format":"bld-layout","version":1,"generator":"…"}`. Writers put it first and store it uncompressed. |
| `layout.bbm` | yes | The map exactly as a `.bbm` file holds it ([bbm-schema.md](bbm-schema.md)). |
| `sidecar.json` | no | Labels, modules, venue and background image. Same shape as the `.bbm.bld` sidecar ([bbm-bld-schema.md](bbm-bld-schema.md)), with two differences listed below. Left out when the layout has none of these. |
| `background.<ext>` | no | The background image's bytes, named by `sidecar.json`'s `backgroundImage.file`. |

`sidecar.json` differs from a `.bbm.bld` sidecar in two ways:

- It has no `bbmHashSha256`. The layout is in the same file, so it can't drift.
- In `backgroundImage`, `file` (the entry name) replaces `path`, a location on
  one machine. Only a writer that couldn't read the image keeps `path`, and it
  warns that it did.

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

## Fixture

[`fixtures/layouts/corner-lobby.bld-layout`](../fixtures/layouts/corner-lobby.bld-layout)
contains:

- `tight-corner.bbm`
- a label
- a module
- the Grand Lobby venue
- a 4×4 background image

The desktop tests read it (tests/import/LayoutFileTest.cpp), and the web app
has the same file in its repository and reads it too.
