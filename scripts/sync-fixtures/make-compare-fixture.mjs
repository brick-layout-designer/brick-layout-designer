// Builds fixtures/sync/compare/: a three-way reconnect case (the copy the
// desktop went offline with, its offline edits and the server's layout
// since) from tight-corner.bbm, as shared documents (Yjs v1 updates, what
// the desktop holds), and expected.json with what the web server's compare
// (apps/server/src/sync/compare.ts) reports for them. The desktop's
// LayoutMerge must report the same keys and statuses.
//
//   cd <web repo>/apps/server && npx tsx <desktop repo>/scripts/sync-fixtures/make-compare-fixture.mjs <web repo> <out dir>

import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const [web, outDir] = process.argv.slice(2);
if (!web || !outDir) {
  console.error('usage: make-compare-fixture.mjs <web repo> <out dir>');
  process.exit(2);
}
const load = (p) => import(pathToFileURL(resolve(web, p)).href);
const { readBbm } = await load('packages/bbm/src/index.ts');
const { seedFromBbm, encodeDoc, exportBbmFromDoc, exportSidecarFromDoc } = await load('packages/ydoc/src/index.ts');
const { compareLayouts } = await load('apps/server/src/sync/compare.ts');

const xml = readFileSync(resolve(web, 'packages/bbm/tests/fixtures/tight-corner.bbm'), 'utf-8');
// Text cells get ids here, so both sides key them by id.
const read = () => {
  const map = readBbm(xml).map;
  for (const l of map.layers) if (l.type === 'text') l.textCells.forEach((c, i) => (c.id = `T${i}`));
  return map;
};
const bricksOf = (map) => map.layers.filter((l) => l.type === 'brick').flatMap((l) => l.bricks);
const textLayer = (map) => map.layers.find((l) => l.type === 'text');
const label = (id, text) => ({
  id,
  text,
  font: { family: 'Arial', size: 12, style: 'Regular' },
  color: { known: true, argb: 4278190080, name: 'Black' },
  kind: 0,
  targetId: '',
  offset: { x: 0, y: 0 },
  rot: 0,
  minZoom: 0,
});
const sidecar = (labels, extra = {}) => ({ schemaVersion: 1, bbmHashSha256: '', anchoredLabels: labels, modules: [], ...extra });

const base = read();
const baseSidecar = sidecar([label('L-both', 'Both edit this'), label('L-gone', 'Server deletes this')]);

// Offline edits.
const mine = read();
{
  const b = bricksOf(mine);
  b[0].orientation += 90; // only mine
  b[2].displayArea.x += 8; // conflict: the server moves it elsewhere
  b[3].displayArea.y += 16; // the same move on both sides
  const layer = mine.layers.find((l) => l.type === 'brick' && l.bricks.some((x) => x.id === b[1].id));
  layer.bricks = layer.bricks.filter((x) => x.id !== b[1].id); // only mine deletes
  layer.bricks.push({ ...structuredClone(b[4]), id: 'mine-new-brick', connexions: [] }); // mine adds
  mine.author = 'Offline me';
  const t = textLayer(mine);
  if (t?.textCells[0]) t.textCells[0].text = 'Edited offline';
}
const mineSidecar = sidecar([label('L-both', 'Mine says this'), label('L-gone', 'Server deletes this'), label('L-mine', 'Added offline')]);

// The server's layout since.
const server = read();
{
  const b = bricksOf(server);
  b[2].displayArea.y -= 8; // conflict with mine
  b[3].displayArea.y += 16; // same as mine
  const gone = b[5].id;
  for (const l of server.layers) if (l.type === 'brick') l.bricks = l.bricks.filter((x) => x.id !== gone);
  server.event = 'Server show';
  // Links alone aren't a change: they follow positions.
  const linked = b.slice(6).find((x) => x.connexions.some((c) => c.linkedTo));
  for (const c of linked.connexions) c.linkedTo = '';
}
const serverSidecar = sidecar([label('L-both', 'Server says this')], {
  venue: { name: 'Hall', outline: [{ x: 0, y: 0 }, { x: 100, y: 0 }, { x: 100, y: 50 }], edges: [], obstacles: [] },
});

mkdirSync(outDir, { recursive: true });
const snapshots = { base: [base, baseSidecar], mine: [mine, mineSidecar], server: [server, serverSidecar] };
const parsed = {};
for (const [name, [map, sc]] of Object.entries(snapshots)) {
  const doc = seedFromBbm(map);
  doc.getMap('meta').set('cache', sc); // as the server keeps a layout's sidecar
  writeFileSync(join(outDir, `${name}.bin`), encodeDoc(doc));
  // As the server's compare sees the layout now.
  parsed[name] = { map: exportBbmFromDoc(doc), sidecar: exportSidecarFromDoc(doc) };
}
const changes = compareLayouts(parsed.base, parsed.mine, parsed.server).map(({ key, kind, status, mine, server }) => ({
  key,
  kind,
  status,
  mine,
  server,
}));
writeFileSync(join(outDir, 'expected.json'), JSON.stringify({ changes }, null, 2) + '\n');
console.log(`${changes.length} changes`);
