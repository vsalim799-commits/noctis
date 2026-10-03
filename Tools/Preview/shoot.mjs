// Headless capture of the WebGL valley preview.
// Usage: node shoot.mjs [baseUrl] [outDir] [views...]
// The page must be served over HTTP (it fetches the exported JSON).
import { chromium } from 'playwright-core';
import fs from 'node:fs';
import path from 'node:path';

const base = process.argv[2] || 'http://127.0.0.1:8765/Tools/Preview/valley.html';
const outDir = process.argv[3] || '../../Output/preview';
const views = process.argv.length > 4 ? process.argv.slice(4) : ['overview', 'herd', 'low', 'river'];
const exe = process.env.CHROMIUM_PATH || '/opt/pw-browsers/chromium_headless_shell-1194/chrome-linux/headless_shell';

fs.mkdirSync(outDir, { recursive: true });
const browser = await chromium.launch({
  executablePath: exe,
  args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', '--enable-webgl'],
});
const page = await browser.newPage({ viewport: { width: 1600, height: 900 } });
page.on('console', (m) => console.log('[page]', m.type(), m.text()));
page.on('pageerror', (e) => console.log('[pageerror]', e.message));
for (const v of views) {
  const url = `${base}${base.includes('?') ? '&' : '?'}still=1&view=${v}`;
  await page.goto(url, { waitUntil: 'load' });
  await page.waitForFunction(() => window.__ready === true, null, { timeout: 180000 });
  await page.waitForTimeout(1500);
  const file = path.join(outDir, `valley_${v}.png`);
  try {
    await page.screenshot({ path: file, timeout: 120000 });
  } catch (e) {
    // Fallback: read the WebGL canvas directly (no HUD overlay).
    const dataUrl = await page.evaluate(() => document.querySelector('canvas').toDataURL('image/png'));
    fs.writeFileSync(file, Buffer.from(dataUrl.split(',')[1], 'base64'));
  }
  console.log('wrote', file);
}
await browser.close();
