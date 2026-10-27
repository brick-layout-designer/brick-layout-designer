# Brick Track Designer layout parts

"Layout parts" exports from Brick Track Designer
(https://aronwk-aaron.github.io/brick-track-designer/), as it writes them:
README.txt beside one folder named after the design, holding one
`<Key>.<part>.xml` + `.gif` per part, `<Key>.whole.set.xml` (the whole
design as a set), `_partlist.txt` and `config/About.txt`. Every piece
carries `<NotListedInLibrary>true</NotListedInLibrary>` (it only makes
sense in the set); the set doesn't.

Made from Brick Track Designer's main branch at 5520aa4, its
`buildDesign` and `layoutParts` run (with `npx tsx`) on the three designs
below: the Left switch (LEGO) template cut for a 150 mm bed, the
two-crossovers-plates test design, and a 4-stud monorail loop.

- `Left switch cut 150.zip`: 3 parts; 11 connection points, 4 pairs and
  3 track ends (rail, type 1).
- `Yard.zip`: 19 parts (14 printed, 5 bought LEGO straights); 46
  connection points, 21 pairs and 4 track ends.
- `Monorail loop.zip`: 8 parts; 16 connection points, 8 pairs, closed
  (monorail, type 3).

Used by Add Parts from a Zip (tests/ui/AddPartsFromZipTest.cpp,
tests/import/PartsZipTest.cpp) and the connection check
(tests/edit/ConnectionCheckTest.cpp, Cli.CheckConnectionsYard).
