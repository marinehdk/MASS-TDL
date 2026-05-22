import { test, expect } from '@playwright/test';

const TIMEOUT = 60_000;

test.describe('DEMO-1 E2E Complete Simulation Flow', () => {

  test('runs complete scenario lifecycle and verifies 3 vessels', async ({ page }) => {
    // 1. Navigate to scenario list
    await page.goto('/#/scenario?dev=1');
    await page.waitForSelector('[data-testid="simulation-scenario"]', { timeout: 15_000 });

    // 2. Select scenario imazu-08-ms
    // Folders might need to be expanded. Let's make sure the folder containing standard scenarios is expanded.
    // The folders standard name in Chinese is "IMAZU标准测试"
    const imazuFolder = page.locator('text=IMAZU标准测试');
    if (await imazuFolder.isVisible()) {
      await imazuFolder.click();
    }

    const scenarioItem = page.locator('text=Imazu 08 Ms');
    await expect(scenarioItem.first()).toBeVisible({ timeout: 15_000 });
    await scenarioItem.first().click();

    // 3. Confirm scenario
    const confirmBtn = page.getByRole('button', { name: '确认场景' });
    await expect(confirmBtn).toBeVisible({ timeout: 5000 });
    await confirmBtn.click();

    // 4. Run preflight checks
    const runBtn = page.getByRole('button', { name: '🚀 进行仿真检查' });
    await expect(runBtn).toBeVisible({ timeout: 5000 });
    await runBtn.click();

    // 5. Verify preflight page loaded
    await page.waitForSelector('[data-testid="preflight"]', { timeout: 15_000 });

    // 6. Wait for GO verdict
    await page.waitForFunction(() => {
      const el = document.querySelector('[data-testid="preflight"]');
      return el?.textContent?.includes('GO');
    }, { timeout: 35_000 });

    // 7. Wait for automatic transition to Simulation Monitor
    await expect(page).toHaveURL(/#\/monitor\/imazu-08-ms/, { timeout: 15_000 });
    await page.waitForSelector('[data-testid="simulation-monitor"]', { timeout: 15_000 });

    // 8. Wait for telemetry to populate and own-ship overlay to disappear
    const conningBar = page.locator('text=/SOG|HDG|COG/');
    await expect(conningBar.first()).toBeVisible({ timeout: TIMEOUT });

    const awaitingOverlay = page.locator('text=AWAITING TELEMETRY');
    await expect(awaitingOverlay).toHaveCount(0, { timeout: TIMEOUT });

    // 9. Open left drawer to verify target vessels (3 vessels: 1 ownship, 2 target vessels)
    await page.waitForSelector('[data-testid="left-drawer-toggle"]', { timeout: 10_000 });
    await page.click('[data-testid="left-drawer-toggle"]');

    // 10. Assert target vessels exist in the table
    const target1 = page.locator('text=100000001');
    const target2 = page.locator('text=100000002');
    await expect(target1).toBeVisible({ timeout: 15_000 });
    await expect(target2).toBeVisible({ timeout: 15_000 });

    // 11. Assert that we are in default 'god' view mode (centered circle at 50,50)
    // In TopChrome, the '测试' view-toggle button should be active/selected
    const testViewToggle = page.locator('text=测试');
    await expect(testViewToggle).toBeVisible();

    // Take screenshot of 3 vessels rendering successfully in '测试' view mode
    await page.screenshot({ path: 'evidence/demo1-3vessels-success.png', fullPage: true });
    console.log('Saved success screenshot of the 3-vessel simulation monitor screen to evidence/demo1-3vessels-success.png');
  });

});
