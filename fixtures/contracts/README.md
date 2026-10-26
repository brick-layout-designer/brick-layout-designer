# Web/desktop contracts

The same files live in `fixtures/contracts/` in both repos (desktop:
brick-layout-designer, web: collaborative-brick-layout-designer). Tests in
both read them, and both check every file against `MANIFEST.sha256`, so a
change in one repo fails until the other has the same copy.

- `imported-part.xml`: what the desktop's ImportToPart writes for a 12 x 12
  stud L with a 2 x 2 hole (its import date replaced by `(date)`).
- `imported-part.json`: what that XML reads as in both apps (connections,
  SnapMargin, PickShape, Designer, ImportedFrom), what the server must
  never store or show (`neverOnTheServer`), and hit points both apps' pick
  tests must agree on.
- `sidecar-hash.json`: the sidecarItems blob hash of each JSON text.
- `update-guard.json`: updates and state vectors (hex) and whether the
  update guard refuses them (web apps/server/src/ws/updateGuard.ts, desktop
  src/sync/UpdateGuard.cpp), with the limits both use.
  Its `gridSize` part: updates (on a base document) and whole documents,
  and whether the server refuses them for a grid size both apps can't
  draw (web apps/server/src/ws/gridGuard.ts); the desktop checks it reads
  them all.
- `part-naming.json`: a server custom part, placed on the website as
  `custom:<id>`, which every export names by its part number and the
  desktop's parts cache finds by either.

To change them: edit (or, for the imported part, run the desktop's
`bld_sync_tests --gtest_filter='Contracts.*'` with `BLD_WRITE_CONTRACTS=1`),
then `sha256sum imported-part.json imported-part.xml part-naming.json sidecar-hash.json update-guard.json > MANIFEST.sha256`
and copy the folder to the other repo.
