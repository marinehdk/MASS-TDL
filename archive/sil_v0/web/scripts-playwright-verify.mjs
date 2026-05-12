import { chromium } from '@playwright/test';

const URL = process.env.URL || 'http://localhost:8000/';
const OUT = process.env.OUT || '/tmp/m8-hmi.png';
const VIEWPORT = { width: 1600, height: 1000 };

const browser = await chromium.launch({
  headless: true,
  args: [
    '--use-gl=angle',
    '--use-angle=swiftshader',
    '--enable-webgl',
    '--ignore-gpu-blocklist',
    '--enable-features=Vulkan',
  ],
});
const ctx = await browser.newContext({ viewport: VIEWPORT, deviceScaleFactor: 1 });
const page = await ctx.newPage();

const consoleMsgs = [];
const failedReqs = [];
const tileReqs = { ok: 0, fail: 0 };

page.on('console', (msg) => {
  consoleMsgs.push(`[${msg.type()}] ${msg.text()}`);
});
page.on('pageerror', (err) => {
  consoleMsgs.push(`[pageerror] ${err.message}`);
});
page.on('requestfailed', (req) => {
  failedReqs.push(`${req.failure()?.errorText} ${req.url()}`);
});
page.on('response', (resp) => {
  const url = resp.url();
  if (url.includes('/tiles/trondheim/') && url.endsWith('.pbf')) {
    if (resp.status() === 200) tileReqs.ok++;
    else tileReqs.fail++;
  }
});

await page.goto(URL, { waitUntil: 'networkidle', timeout: 30000 });

// 1. Wait for MapLibre Canvas
await page.waitForSelector('.maplibregl-canvas', { timeout: 15000 });

// 2. Wait for Bridge Connection (Text indicator in Topbar)
console.log('Checking for Bridge Connection...');
await page.waitForFunction(
  () => {
    const statusText = document.querySelector('#bridge-status-text');
    return statusText && statusText.textContent?.includes('SIMULATION ACTIVE');
  },
  { timeout: 10000 }
);
console.log('Bridge: CONNECTED');

// 3. Settle and check for Vessel tracks (Introspection via MapLibre)
await page.waitForTimeout(5000);

const vesselCheck = await page.evaluate(() => {
  // Check if map data source for targets has features
  // @ts-ignore
  const map = window.map; // Assuming MapView exposes map to window for debug
  if (!map) return { error: 'Map object not found on window' };
  
  const sources = map.getStyle().sources;
  const targetSource = sources['targets'];
  if (!targetSource) return { error: 'Target source not found' };
  
  // Best effort: check if any target symbols are rendered
  const layers = map.getStyle().layers;
  const targetLayer = layers.find(l => l.id === 'targets-layer');
  return {
    hasTargetLayer: !!targetLayer,
    sourceLoaded: map.isSourceLoaded('targets'),
  };
});

await page.screenshot({ path: OUT, fullPage: false });

console.log(JSON.stringify({
  url: URL,
  out: OUT,
  tileReqs,
  bridgeConnected: true,
  vesselCheck,
  failedReqsCount: failedReqs.length,
  consoleErrorCount: consoleMsgs.filter((m) => m.startsWith('[error]') || m.startsWith('[pageerror]')).length,
}, null, 2));

await browser.close();
