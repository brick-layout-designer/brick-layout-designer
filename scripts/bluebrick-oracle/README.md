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

Known BlueBrick behaviours the tests allow for:

- The LDraw header names the map "Untitled" (the harness never sets a file name).
- Parts missing from the library are written at a slightly different position
  (BlueBrick rebuilds their placeholder at a whole-stud size).
- Parts whose name contains a space are dropped when BlueBrick reads LDraw.
- Under Wine, ruler fonts may be substituted (e.g. Arial -> Tahoma).
