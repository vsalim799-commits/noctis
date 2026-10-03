// Headless capture of the clay creature page: builds the mesh once, then renders each view.
// Usage: node shoot_clay.mjs <outDir> [view ...] [--mode=clay|region|cavity|fold|occl|weights] [--bone=thighR]
//        [--voxel=0.03] [--pose=1] [--w=1600 --h=900] [--prefix=clay_]
import { chromium } from 'playwright-core';
import fs from 'node:fs';
import path from 'node:path';

const args = process.argv.slice(2);
const outDir = args.find((a) => !a.startsWith('--')) || '.';
const views = args.filter((a) => !a.startsWith('--')).slice(1);
const opt = Object.fromEntries(args.filter((a) => a.startsWith('--')).map((a) => a.slice(2).split('=')));
const w = +(opt.w || 1600), h = +(opt.h || 900);
const q = new URLSearchParams({ w, h, mode: opt.mode || 'clay', voxel: opt.voxel || '0.03', bone: opt.bone || 'thighR', pose: opt.pose || '0', view: views[0] || 'side' });
fs.mkdirSync(outDir, { recursive: true });

const browser = await chromium.launch({ executablePath: '/opt/pw-browsers/chromium_headless_shell-1194/chrome-linux/headless_shell', args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
const page = await browser.newPage({ viewport: { width: w, height: h } });
page.on('pageerror', (e) => console.log('[pageerror]', e.message));
page.on('console', (m) => { if (m.type() === 'error' || m.type() === 'warning') console.log(`[${m.type()}]`, m.text()); });
const t0 = Date.now();
await page.goto(`http://127.0.0.1:8765/Tools/Preview/creature_clay.html?${q}`);
await page.waitForFunction(() => window.__ready === true, null, { timeout: 180000 });
const stats = await page.evaluate(() => window.__stats);
const bb = stats.bbox;
console.log(JSON.stringify({ ...stats, bbox: undefined }));
console.log(`length ${(bb.max[0] - bb.min[0]).toFixed(3)} m (x ${bb.min[0].toFixed(2)}..${bb.max[0].toFixed(2)}), max height ${bb.max[1].toFixed(3)} m, ` +
  `min y ${bb.min[1].toFixed(3)}, width ${(bb.max[2] - bb.min[2]).toFixed(3)} m, volume ${stats.volume.toFixed(2)} m3 ` +
  `(~${(stats.volume * 0.95).toFixed(2)} t at 950 kg/m3), page ready in ${((Date.now() - t0) / 1000).toFixed(1)} s`);
const prefix = opt.prefix ?? (opt.mode && opt.mode !== 'clay' ? `${opt.mode}_` : 'clay_');
for (const v of views.length ? views : ['side']) {
  await page.evaluate((name) => window.renderView(name), v);
  const file = path.join(outDir, `${prefix}${v}${opt.pose === '1' ? '_pose' : ''}.png`);
  await page.locator('canvas').screenshot({ path: file });
  console.log('wrote', file);
}
await browser.close();
