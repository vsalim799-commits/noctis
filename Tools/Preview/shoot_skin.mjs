// Headless capture of the skin test bench.
// Usage: node shoot_skin.mjs <outDir> [shot ...] [--q "seed=1&bump=0.8"]
// Shots: close neck head foot mid far pose2 flat (default: close head foot far).
import { chromium } from 'playwright-core';
import { mkdirSync } from 'node:fs';

const args = process.argv.slice(2);
const outDir = args.shift() || 'skin_shots';
let extra = '';
const qi = args.indexOf('--q');
if (qi >= 0) { extra = '&' + args[qi + 1]; args.splice(qi, 2); }
const shots = args.length ? args : ['close', 'head', 'foot', 'far'];
mkdirSync(outDir, { recursive: true });

const browser = await chromium.launch({
  executablePath: '/opt/pw-browsers/chromium_headless_shell-1194/chrome-linux/headless_shell',
  args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
});
for (const shot of shots) {
  const page = await browser.newPage({ viewport: { width: 1600, height: 900 } });
  page.on('pageerror', (e) => console.log('[pageerror]', e.message));
  page.on('console', (m) => { if (m.type() === 'error' || m.type() === 'warning' || m.text().startsWith('skin_test')) console.log('[console]', m.text().slice(0, 4000)); });
  const t = Date.now();
  await page.goto(`http://127.0.0.1:8765/Tools/Preview/skin_test.html?shot=${shot}&w=1600&h=900${extra}`);
  await page.waitForFunction(() => window.__ready === true, null, { timeout: 180000 });
  await page.screenshot({ path: `${outDir}/skin_${shot}.png` });
  console.log(shot, 'done in', ((Date.now() - t) / 1000).toFixed(1), 's');
  await page.close();
}
await browser.close();
