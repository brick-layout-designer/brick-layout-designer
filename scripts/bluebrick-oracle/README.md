# BlueBrick oracle

`bbconv.cs` drives vanilla BlueBrick 1.9.2's own `SaveLoadManager` headless:
it loads each input map and saves it in the format given by the output's
extension (`.bbm`, `.ldr`, `.mpd`, `.tdl`, `.ncp`). The files in
[`fixtures/bluebrick-oracle/`](../../fixtures/bluebrick-oracle/) were made
with it and are what the map-format tests compare against.

Linux, with Wine and a .NET Framework 4 prefix:

```sh
cp -r /path/to/BlueBrick.1.9.2 /tmp/bb        # a copy: the harness goes next to BlueBrick.exe
cp scripts/bluebrick-oracle/bbconv.cs /tmp/bb/
cd /tmp/bb
wine 'C:\windows\Microsoft.NET\Framework\v4.0.30319\csc.exe' /nologo /r:System.Windows.Forms.dll bbconv.cs
xvfb-run -a wine bbconv.exe "$(winepath -w in.bbm)" "$(winepath -w out.ldr)" [more in/out pairs...]
```

On Windows run `csc` and `bbconv.exe` directly.

`bbflex.cs` (built the same way, adding `/r:System.Drawing.dll`) runs a
flex move instead: it selects every brick of the grabbed brick's layer and
drags that brick through the given points, without connection snapping:

```sh
xvfb-run -a wine bbflex.exe in.bbm out.bbm <brick id> <grab x> <grab y> <x1> <y1> [<x2> <y2> ...]
```

`flex-a/b/c.bbm` were made from `flex-in.bbm` this way (see
`tests/ui/FlexMoveTest.cpp` for the arguments).

Known BlueBrick behaviours the tests allow for:

- The LDraw header names the map "Untitled" (the harness never sets a file name).
- Parts missing from the library are written at a slightly different position
  (BlueBrick rebuilds their placeholder at a whole-stud size).
- Parts whose name contains a space are dropped when BlueBrick reads LDraw.
- Under Wine, ruler fonts may be substituted (e.g. Arial -> Tahoma).
- `fourdbrix.bbm` links some track to BrickTracks / TrixBrix parts that only
  vanilla's installed library has, so the 4DBrix write test keeps the links
  stored in the file instead of rebuilding them.
- BlueBrick reads a 4DBrix segment's `index` as an integer, so groups in
  `.ncp` files it wrote itself (brick GUIDs) are lost when it reads them
  back; this port matches the ids as text and keeps them.
