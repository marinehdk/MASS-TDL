import { chromium } from '@playwright/test';

async function main() {
  const browser = await chromium.launch({ headless: true });
  const context = await browser.newContext({
    viewport: { width: 1920, height: 1080 }
  });
  const page = await context.newPage();

  page.on('console', msg => {
    console.log(`[PAGE ${msg.type().toUpperCase()}]`, msg.text());
  });

  page.on('pageerror', err => {
    console.error('[PAGE ERROR]', err.message);
  });

  page.on('requestfailed', request => {
    console.error(`[REQUEST FAILED] ${request.url()} - ${request.failure()?.errorText || 'unknown error'}`);
  });

  console.log('Navigating to http://localhost:5173...');
  await page.goto('http://localhost:5173', { waitUntil: 'networkidle' });

  // Wait for map to initialize
  await page.waitForTimeout(5000);

  // Jump the map directly to the coordinates [10.3633, 63.42497] at different zoom levels
  // We can execute code in the browser to interact with the map libre instance
  console.log('Jumping map to user coordinates...');
  await page.evaluate(() => {
    if (window.__maplibre_map) {
      window.__maplibre_map.jumpTo({
        center: [10.3633, 63.42497],
        zoom: 15
      });
    } else {
      console.warn('__maplibre_map not found on window!');
    }
  });

  console.log('Waiting for tiles to render at Zoom 15...');
  await page.waitForTimeout(5000);

  const screenshotPath15 = '/Users/marine/.gemini/antigravity/brain/82f7895c-33f0-4369-a475-16f2d6135ea0/debug_zoom_15.png';
  console.log(`Saving zoom 15 screenshot to ${screenshotPath15}...`);
  await page.screenshot({ path: screenshotPath15, fullPage: true });

  // Zoom in to 16
  await page.evaluate(() => {
    if (window.__maplibre_map) {
      window.__maplibre_map.setZoom(16);
    }
  });
  console.log('Waiting for tiles to render at Zoom 16...');
  await page.waitForTimeout(5000);

  const screenshotPath16 = '/Users/marine/.gemini/antigravity/brain/82f7895c-33f0-4369-a475-16f2d6135ea0/debug_zoom_16.png';
  console.log(`Saving zoom 16 screenshot to ${screenshotPath16}...`);
  await page.screenshot({ path: screenshotPath16, fullPage: true });

  await browser.close();
  console.log('Done!');
}

main().catch(err => {
  console.error('Error debugging tiles:', err);
  process.exit(1);
});
