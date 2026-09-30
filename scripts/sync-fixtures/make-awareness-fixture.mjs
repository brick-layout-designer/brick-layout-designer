// Builds fixtures/sync/awareness.bin: an awareness update as the web editor
// sends it (y-protocols encodeAwarenessUpdate) for two peers, one with a
// cursor and a selection and one that has left, plus awareness.expected.json
// with the states the desktop must read from it.
//
//   node scripts/sync-fixtures/make-awareness-fixture.mjs <web repo> <out dir>

import { writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { join, resolve } from 'node:path';

const [web, outDir] = process.argv.slice(2);
if (!web || !outDir) {
  console.error('usage: make-awareness-fixture.mjs <web repo> <out dir>');
  process.exit(2);
}
const require = createRequire(resolve(web, 'apps', 'web', 'package.json'));
const Y = require('yjs');
const { Awareness, encodeAwarenessUpdate } = require('y-protocols/awareness');

const withId = (id) => {
  const d = new Y.Doc();
  d.clientID = id;
  return new Awareness(d);
};
const a = withId(1234567);
const b = withId(7654321);
const alice = {
  user: { id: 'u-alice', name: 'Alice', color: '#e8590c' },
  cursor: { x: 120.5, y: -40, layerId: 'L1' },
  selection: { brickIds: ['b1', 'b2'] },
  tool: 'select',
  lastActivityMs: 1790000000000,
};
a.setLocalState(alice);
b.setLocalState({ user: { id: 'u-bob', name: 'Bob', color: '#1c7ed6' }, cursor: null, selection: { brickIds: [] }, tool: 'select', lastActivityMs: 1 });
b.setLocalState(null); // Bob left
// One update holding both, as a server relay combines them.
const both = withId(1);
const { applyAwarenessUpdate } = require('y-protocols/awareness');
applyAwarenessUpdate(both, encodeAwarenessUpdate(a, [a.clientID]), 'test');
applyAwarenessUpdate(both, encodeAwarenessUpdate(b, [b.clientID]), 'test');
const update = encodeAwarenessUpdate(both, [a.clientID, b.clientID]);
writeFileSync(join(outDir, 'awareness.bin'), update);
writeFileSync(
  join(outDir, 'awareness.expected.json'),
  JSON.stringify({ clients: [{ id: a.clientID, state: alice }, { id: b.clientID, state: null }] }, null, 2) + '\n',
);
// Awareness keeps a timer running.
process.exit(0);
