import { chromium } from '@playwright/test';
import path from 'path';

async function main() {
  const browser = await chromium.launch({ headless: true });
  const context = await browser.newContext({
    viewport: { width: 1920, height: 1080 }
  });
  const page = await context.newPage();

  // Route console messages to terminal
  page.on('console', msg => console.log('PAGE LOG:', msg.text()));
  page.on('pageerror', exception => console.error('PAGE EXCEPTION:', exception.toString()));

  console.log('Navigating to http://localhost:5173/#/scenario?dev=1...');
  await page.goto('http://localhost:5173/#/scenario?dev=1', { waitUntil: 'networkidle' });

  console.log('Waiting for Scenario Library container...');
  await page.waitForSelector('[data-testid="simulation-scenario"]', { timeout: 30000 });

  // Let the list render
  await page.waitForTimeout(2000);

  // Find and click the "IMAZU标准测试" folder
  console.log('Clicking IMAZU folder...');
  const imazuFolder = page.locator('text="IMAZU标准测试"');
  await imazuFolder.click();

  // Wait for the folder to expand and children to populate
  await page.waitForTimeout(3000);

  // Find and click the first scenario "Imazu 01 Ho"
  console.log('Selecting Imazu 01 Ho scenario...');
  const scenarioItem = page.locator('span:has-text("Imazu 01 Ho")').first();
  await scenarioItem.waitFor({ state: 'visible', timeout: 5000 });
  await scenarioItem.click();

  // Confirm scenario
  console.log('Confirming scenario...');
  const confirmBtn = page.getByRole('button', { name: '确认场景' });
  await confirmBtn.waitFor({ state: 'visible', timeout: 5000 });
  await confirmBtn.click();

  // Wait for screen to transition to preflight
  await page.waitForTimeout(2000);

  // Run preflight checks
  console.log('Running preflight checks...');
  const runBtn = page.getByRole('button', { name: '🚀 进行仿真检查' });
  await runBtn.waitFor({ state: 'visible', timeout: 5000 });
  await runBtn.click();

  // Wait for checks to complete
  await page.waitForTimeout(8000);

  // Check if we got redirected, if not, check for NO-GO fallback
  const currentUrl = page.url();
  if (currentUrl.includes('/#/check/')) {
    console.log('Preflight returned NO-GO. Bypassing using Force Skip...');
    const reasonInput = page.getByPlaceholder('请说明强制跳过原因...');
    if (await reasonInput.isVisible()) {
      await reasonInput.fill('Bypassing container and clock drift limits in dev sandbox selfcheck.');
      await page.waitForTimeout(1000);
      const forceSkipBtn = page.getByRole('button', { name: '强制跳过' });
      await forceSkipBtn.click();
    }
  }

  // Wait for automatic redirect to Simulation Monitor
  console.log('Waiting for redirect to Simulation Monitor...');
  await page.waitForURL(/#\/monitor\/imazu-01-ho/, { timeout: 20000 });
  await page.waitForSelector('[data-testid="simulation-monitor"]', { timeout: 15000 });

  console.log('Successfully navigated to Simulation Monitor! Waiting for telemetry...');
  // Wait for telemetry to populate (AWAITING TELEMETRY overlay disappears)
  await page.waitForFunction(() => {
    return !document.body.textContent.includes('AWAITING TELEMETRY');
  }, { timeout: 30000 });

  console.log('Telemetry connected! Let the simulation run for 5 seconds...');
  await page.waitForTimeout(5000);

  console.log('Clicking the M4 bottom card...');
  await page.locator('span:has-text("M4")').first().click({ force: true });

  console.log('M4 card clicked! Let it run for 5 seconds to capture potential crashes...');
  await page.waitForTimeout(5000);

  const screenshotPath = '/Users/marine/.gemini/antigravity/brain/202e854e-757b-4ed0-9dd6-5e8c2d953e13/decision_tab_crash.png';
  console.log(`Taking screenshot and saving to ${screenshotPath}...`);
  await page.screenshot({ path: screenshotPath, fullPage: true });

  console.log('Screenshot saved successfully!');
  await browser.close();
}

main().catch(err => {
  console.error('Error capturing screenshot:', err);
  process.exit(1);
});
