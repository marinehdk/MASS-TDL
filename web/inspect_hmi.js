import { chromium } from '@playwright/test';
import path from 'path';
import fs from 'fs';

async function run() {
  console.log('Launching browser...');
  const browser = await chromium.launch({ headless: true });
  const context = await browser.newContext({
    viewport: { width: 1920, height: 1080 }
  });
  const page = await context.newPage();

  console.log('Setting up event listeners...');
  
  // Capture console logs
  page.on('console', msg => {
    console.log(`[BROWSER CONSOLE] [${msg.type()}] ${msg.text()}`);
  });

  // Capture page errors
  page.on('pageerror', err => {
    console.error(`[BROWSER ERROR] ${err.toString()}`);
  });

  // Capture failed network requests
  page.on('requestfailed', request => {
    console.log(`[NETWORK FAILED] ${request.url()} - ${request.failure()?.errorText || 'Unknown failure'}`);
  });

  page.on('request', request => {
    const url = request.url();
    if (url.includes('3000') || url.includes('/mvt')) {
      console.log(`[TILE REQUEST] ${url}`);
    }
  });

  page.on('response', response => {
    const status = response.status();
    if (status >= 400) {
      console.log(`[NETWORK ERROR] ${response.url()} returned status ${status}`);
    }
  });

  console.log('Navigating to HMI...');
  await page.goto('http://localhost:5173', { waitUntil: 'networkidle' });

  console.log('Page loaded. Waiting 8 seconds for map layers and tiles to render...');
  await page.waitForTimeout(8000);

  const screenshotPath = './hmi_debug_screenshot.png';
  console.log(`Taking screenshot to ${screenshotPath}...`);
  await page.screenshot({ path: screenshotPath });

  console.log('Done!');
  await browser.close();
}

run().catch(err => {
  console.error('Script failed:', err);
  process.exit(1);
});
