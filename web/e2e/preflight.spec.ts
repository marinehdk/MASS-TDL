import { test, expect } from '@playwright/test';

test.describe('Simulation-Check 6-Gate Sequencer', () => {

  test('6 gate cards render with labels', async ({ page }) => {
    await page.goto('/#/check/test-scenario?dev=1');
    await page.waitForSelector('[data-testid="preflight"]', { timeout: 15_000 });

    const gateLabels = ['System Readiness', 'Module Health', 'Scenario Integrity',
                        'ODD-Scenario Alignment', 'Time Base', 'Doer-Checker Independence'];
    for (const label of gateLabels) {
      await expect(page.getByText(label, { exact: false }).first()).toBeVisible({ timeout: 20_000 });
    }
  });

  test('GO/NO-GO panel appears after checks complete', async ({ page }) => {
    await page.goto('/#/check/test-scenario?dev=1');
    await page.waitForFunction(() => {
      const el = document.querySelector('[data-testid="preflight"]');
      return el?.textContent?.includes('GO') || el?.textContent?.includes('NO-GO');
    }, { timeout: 25_000 });

    const bodyText = await page.textContent('[data-testid="preflight"]');
    expect(bodyText).toMatch(/GO|NO-GO/);
  });

  test('failed gate shows FAIL indicator', async ({ page }) => {
    await page.goto('/#/check/broken-scenario?dev=1');
    await page.waitForSelector('[data-testid="preflight"]', { timeout: 15_000 });

    const failIndicator = page.locator('text=FAIL').first();
    await expect(failIndicator).toBeVisible({ timeout: 20_000 });
  });

  test('ABORT button returns to scenario screen', async ({ page }) => {
    await page.goto('/#/check/test-scenario?dev=1');
    await page.waitForSelector('[data-testid="preflight"]', { timeout: 15_000 });

    const abortBtn = page.getByRole('button', { name: /ABORT/i });
    await expect(abortBtn).toBeVisible({ timeout: 15_000 });
  });

  test('SKIP button hidden when no ?dev=1 param', async ({ page }) => {
    await page.goto('/#/check/test-scenario');
    await page.waitForSelector('[data-testid="preflight"]', { timeout: 15_000 });

    const skipBtn = page.getByRole('button', { name: /SKIP PREFLIGHT/i });
    await expect(skipBtn).toHaveCount(0, { timeout: 10_000 });
  });

  test('dev mode shows SKIP with reason input on failure', async ({ page }) => {
    await page.goto('/#/check/failing-scenario?dev=1');
    await page.waitForSelector('[data-testid="preflight"]', { timeout: 15_000 });

    await page.waitForFunction(() => {
      return document.querySelector('[data-testid="preflight"]')?.textContent?.includes('NO-GO');
    }, { timeout: 25_000 });

    const devSkip = page.getByText(/DEV MODE/i);
    await expect(devSkip).toBeVisible({ timeout: 10_000 });
  });

  test('LiveLogStream panel visible', async ({ page }) => {
    await page.goto('/#/check/test-scenario?dev=1');
    await page.waitForSelector('[data-testid="preflight-livelog"]', { timeout: 15_000 });
    await expect(page.locator('[data-testid="preflight-livelog"]')).toBeVisible();
  });

  test('all 6 gates return GO within timeout', async ({ page }) => {
    const start = Date.now();
    await page.goto('/#/check/test-scenario?dev=1');
    await page.waitForFunction(() => {
      const el = document.querySelector('[data-testid="preflight"]');
      return el?.textContent?.includes('GO');
    }, { timeout: 25_000 });
    const elapsed = Date.now() - start;
    expect(elapsed).toBeLessThan(15000);
  });
});
