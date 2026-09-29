// Cross-language check of desktop writes (sync phase P3b): applies an
// update the desktop wrote (SyncDoc::writeMap) to the server's document
// with the web's own Yjs, and compares the web's .bbm export with the
// desktop's. Run after the desktop test exports them:
//
//   BLD_SYNC_EXPORT_DIR=/tmp/x build/tests/sync/bld_sync_tests --gtest_filter=*ExportsAnUpdate*
//   node scripts/sync-fixtures/check-desktop-update.mjs <web repo> fixtures/sync/layers.ydoc /tmp/x
//
// Needs the web repo's packages built (pnpm -r build).

import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { pathToFileURL } from 'node:url';

const [web, baseDoc, dir] = process.argv.slice(2);
if (!web || !baseDoc || !dir) {
  console.error('usage: check-desktop-update.mjs <web repo> <base .ydoc> <export dir>');
  process.exit(2);
}
const load = (pkg) => import(pathToFileURL(join(web, 'packages', pkg, 'dist', 'index.js')).href);
const { writeBbm } = await load('bbm');
const { docToBbm } = await load('ydoc');
// The same yjs module the web's ydoc package imports (its ESM entry): a
// second copy would fail the web code's instanceof checks.
const yjsPkg = createRequire(join(web, 'packages', 'ydoc', 'package.json')).resolve('yjs/package.json');
const yjsMeta = JSON.parse(readFileSync(yjsPkg, 'utf8'));
const yjsEntry = yjsMeta.exports?.['.']?.import?.default ?? yjsMeta.exports?.['.']?.import ?? yjsMeta.module;
const Y = await import(pathToFileURL(join(dirname(yjsPkg), yjsEntry)).href);

const doc = new Y.Doc();
Y.applyUpdate(doc, readFileSync(baseDoc));
Y.applyUpdate(doc, readFileSync(join(dir, 'desktop-update.bin')));
// nbItems differs by design: the web recounts it on write, the desktop
// keeps the stored value (BbmWriter.cpp).
const mask = (bbm) => bbm.replace(/<nbItems>\d+<\/nbItems>/, '<nbItems/>');
const got = mask(writeBbm(docToBbm(doc)));
const want = mask(readFileSync(join(dir, 'desktop-expected.bbm'), 'utf8'));
if (got === want) {
  console.log('ok: the web reads the desktop update as the desktop does');
} else {
  const a = got.split('\n');
  const b = want.split('\n');
  const i = a.findIndex((l, n) => l !== b[n]);
  console.error(`MISMATCH at line ${i + 1}:\n  web:     ${a[i]}\n  desktop: ${b[i]}`);
  process.exit(1);
}
