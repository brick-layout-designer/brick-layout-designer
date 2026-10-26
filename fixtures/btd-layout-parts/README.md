# Brick Track Designer layout parts

"Layout parts" exports from Brick Track Designer
(https://aronwk-aaron.github.io/brick-track-designer/), as it writes them:
README.txt beside one folder named after the design, holding one
`<Key>.<part>.xml` + `.gif` per printed part, `<Key>.whole.set.xml` (the
whole design as a set), `_partlist.txt` and `config/About.txt`.

- `Left switch cut 150.zip`: 3 parts, rail connections (type 1).
- `Yard.zip`: 15 parts; 394 connection points, 195 pairs and 4 track ends.
- `Monorail loop.zip`: 8 parts, monorail connections (type 3).

Used by Add Parts from a Zip (tests/ui/AddPartsFromZipTest.cpp) and the
connection check (tests/edit/ConnectionCheckTest.cpp).
