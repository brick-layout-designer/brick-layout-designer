// Builds fixtures/sync/<name>.ydoc: the Yjs document the collaborative web
// server holds for a .bbm (seedFromBbm, then upgradeDoc as its doc hub
// does), saved as a v1 state update, plus <name>.expected.json with what
// the desktop must read back from it (layer ids and kinds, brick counts,
// the first brick of each brick layer).
//
//   node scripts/sync-fixtures/make-ydoc-fixture.mjs <web repo> <in.bbm> <out dir>
//
// Needs the web repo's packages built (pnpm -r build).

import { readFileSync, writeFileSync } from 'node:fs';
import { basename, join } from 'node:path';
import { pathToFileURL } from 'node:url';

const [web, bbmPath, outDir] = process.argv.slice(2);
if (!web || !bbmPath || !outDir) {
  console.error('usage: make-ydoc-fixture.mjs <web repo> <in.bbm> <out dir>');
  process.exit(2);
}
const load = (pkg) => import(pathToFileURL(join(web, 'packages', pkg, 'dist', 'index.js')).href);
const { readBbm } = await load('bbm');
const { seedFromBbm, upgradeDoc, encodeDoc } = await load('ydoc');

const map = readBbm(readFileSync(bbmPath, 'utf8')).map;
const doc = seedFromBbm(map);
upgradeDoc(doc);
const name = basename(bbmPath, '.bbm');
writeFileSync(join(outDir, `${name}.ydoc`), encodeDoc(doc));

const expected = {
  source: basename(bbmPath),
  schemaVersion: doc.getMap('meta').get('schemaVersion'),
  layers: map.layers.map((l) => {
    const out = { id: l.id, type: l.type, name: l.name };
    if (l.type === 'brick') {
      out.bricks = l.bricks.length;
      const b = l.bricks[0];
      if (b) out.firstBrick = { id: b.id, partNumber: b.partNumber, displayArea: b.displayArea, orientation: b.orientation };
    }
    return out;
  }),
};
writeFileSync(join(outDir, `${name}.expected.json`), `${JSON.stringify(expected, null, 2)}\n`);
console.log(`wrote ${name}.ydoc and ${name}.expected.json`);
