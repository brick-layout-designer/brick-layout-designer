# Parser fuzzing

libFuzzer harnesses for every file the app reads, built with clang,
AddressSanitizer and UndefinedBehaviorSanitizer:

| Target | Parser |
|---|---|
| `bbm` | `.bbm` maps (`saveload::readBbm`) |
| `sidecar` | `.bbm.bld` sidecars |
| `venue` | venue files |
| `part_xml` | part XML in the parts library (plus footprint) |
| `budget` | `.bbb` budgets (read, then written back) |
| `ldraw_map` | `.ldr` / `.mpd` maps (read, then written back) |
| `tdl` | TrackDesigner maps (read, then written back) |
| `ncp` | 4DBrix maps (read, then written back) |
| `ldraw_import` | LDraw models for part import |
| `lxfml` | LDD models |
| `studio` | Studio `.io` archives |
| `ydoc` | live-sync documents (Yjs updates, `sync::summarizeDoc`), seed `fixtures/sync/*.ydoc` |
| `sync_message` | y-websocket messages from the server (`sync::protocol::decode`) |

Run them all (60 s each by default):

```sh
fuzz/run.sh [seconds per target] [build dir]
```

Starting inputs come from `fixtures/` and the parts library, plus a few
written by `bld_fuzz_seeds`. A crash leaves the input in
`<build dir>/fuzz-artifacts/<target>/`; replay it with
`<build dir>/fuzz/fuzz_<target> <that file>`.

CI runs every target for a short while on each pull request (the
*Fuzz parsers* job) and uploads any crash inputs.
