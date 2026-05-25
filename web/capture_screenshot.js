import { chromium } from '@playwright/test';

async function main() {
  const browser = await chromium.launch({ headless: true });
  const context = await browser.newContext({
    viewport: { width: 1920, height: 1080 }
  });
  const page = await context.newPage();

  // Route console messages to terminal
  page.on('console', msg => console.log('PAGE LOG:', msg.text()));

  console.log('Navigating to http://localhost:5173...');
  await page.goto('http://localhost:5173', { waitUntil: 'networkidle' });

  console.log('Waiting for Scenario Library container...');
  await page.waitForSelector('[data-testid="simulation-scenario"]', { timeout: 15000 });

  // Let the list render
  await page.waitForTimeout(2000);

  // Find and click the "IMAZU标准测试" folder
  console.log('Clicking IMAZU folder...');
  const imazuFolder = page.locator('text="IMAZU标准测试"');
  await imazuFolder.click();

  // Wait for the folder to expand and children to populate
  await page.waitForTimeout(3000);

  // Find and click the first scenario starting with "Imazu 1" (e.g., Imazu 11 Ms)
  console.log('Selecting first Imazu 1x scenario...');
  const scenarioItem = page.locator('span:has-text("Imazu 1")').first();
  await scenarioItem.waitFor({ state: 'visible', timeout: 5000 });
  const scenarioName = await scenarioItem.textContent();
  console.log(`Clicking scenario: ${scenarioName}`);
  await scenarioItem.click();

  // Wait for map to fly to coordinate and tiles to render
  console.log('Waiting for map to load S-57 vector tiles for this scenario...');
  await page.waitForTimeout(6000);

  const screenshotPath = '/Users/marine/.gemini/antigravity/brain/82f7895c-33f0-4369-a475-16f2d6135ea0/hmi_resolved_screenshot.png';
  console.log(`Taking screenshot and saving to ${screenshotPath}...`);
  await page.screenshot({ path: screenshotPath, fullPage: true });

  console.log('Screenshot saved successfully!');
  await browser.close();
}

main().catch(err => {
  console.error('Error capturing screenshot:', err);
  process.exit(1);
});
