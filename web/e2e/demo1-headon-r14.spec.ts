import { test, expect } from '@playwright/test';

const MONITOR_URL = '/#/monitor/imazu-08?dev=1';
const TIMEOUT = 60_000;

test.describe('DEMO-1 R14 Head-On — Captain View End-to-End', () => {

  test('own-ship telemetry populates within 30s', async ({ page }) => {
    await page.goto(MONITOR_URL);
    await page.waitForSelector('[data-testid="simulation-monitor"]', { timeout: 15_000 });

    const conningBar = page.locator('text=/SOG|HDG|COG/');
    await expect(conningBar.first()).toBeVisible({ timeout: TIMEOUT });

    const awaitingOverlay = page.locator('text=AWAITING TELEMETRY');
    await expect(awaitingOverlay).toHaveCount(0, { timeout: TIMEOUT });
  });

  test('target vessel populates in ArpaTargetTable with real BRG/RNG', async ({ page }) => {
    await page.goto(MONITOR_URL);
    await page.waitForSelector('[data-testid="simulation-monitor"]', { timeout: 15_000 });
    await page.waitForSelector('[data-testid="left-drawer-toggle"]', { timeout: 10_000 });

    await page.click('[data-testid="left-drawer-toggle"]');

    const targetRow = page.locator('table td').filter({ hasText: /\d+\.\d+°|\d+\.\d+ nm/ });
    await expect(targetRow.first()).toBeVisible({ timeout: TIMEOUT });
  });

  test('ModulePulseBar shows M1-M8 heartbeat dots', async ({ page }) => {
    await page.goto(MONITOR_URL);
    await page.waitForSelector('[data-testid="simulation-monitor"]', { timeout: 15_000 });

    const pulseBar = page.locator('text=/M1.*M2.*M3.*M4.*M5.*M6.*M7.*M8/');
    await expect(pulseBar.first()).toBeVisible({ timeout: TIMEOUT });
  });

  test('ConningBar shows 7 fields in captain mode', async ({ page }) => {
    await page.goto(MONITOR_URL);
    await page.waitForSelector('[data-testid="simulation-monitor"]', { timeout: 15_000 });

    const fields = ['HDG', 'COG', 'SOG', 'ROT', 'RUD', 'RPM', 'PITCH'];
    for (const field of fields) {
      await expect(page.getByText(field, { exact: true }).first()).toBeVisible({ timeout: TIMEOUT });
    }
  });

  test('ThreatRibbon shows CPA chips', async ({ page }) => {
    await page.goto(MONITOR_URL);
    await page.waitForSelector('[data-testid="simulation-monitor"]', { timeout: 15_000 });

    const cpaChip = page.locator('text=/\\d+\\.\\d+ nm/');
    await expect(cpaChip.first()).toBeVisible({ timeout: TIMEOUT });
  });

  // Out of scope for DEMO-1: ToR Modal hard-acceptance deferred to DEMO-3.
  test.skip('ToR Modal triggers on R14 head-on encounter', async ({ page }) => {
    await page.goto(MONITOR_URL);
    await page.waitForSelector('[data-testid="simulation-monitor"]', { timeout: 15_000 });

    await page.waitForFunction(() => {
      const el = document.querySelector('[data-fsm]');
      return el && (el.getAttribute('data-fsm') === 'COLREG_AVOIDANCE' || el.getAttribute('data-fsm') === 'TOR');
    }, { timeout: TIMEOUT });

    const fsmState = await page.locator('[data-fsm]').getAttribute('data-fsm');
    expect(fsmState).toMatch(/COLREG_AVOIDANCE|TOR/);
  });

  test('NO demo_telemetry fallback — WS connected throughout', async ({ page }) => {
    await page.goto(MONITOR_URL);
    await page.waitForSelector('[data-testid="simulation-monitor"]', { timeout: 15_000 });

    await page.waitForFunction(() => {
      return document.body.textContent?.includes('WS CONNECTED');
    }, { timeout: 10_000 });

    const demoTag = page.locator('text=DEMO MODE');
    await expect(demoTag).toHaveCount(0, { timeout: TIMEOUT });
  });

  // Out of scope for DEMO-1: ASDR take_over validation deferred to DEMO-2/3.
  test.skip('ASDR Ledger receives take_over event after TOR→OVERRIDE', async ({ page }) => {
    await page.goto(MONITOR_URL);
    await page.waitForSelector('[data-testid="simulation-monitor"]', { timeout: 15_000 });
    await page.waitForSelector('[data-testid="right-drawer-toggle"]', { timeout: 10_000 });

    await page.click('[data-testid="right-drawer-toggle"]');

    await page.waitForFunction(() => {
      const text = document.body.textContent ?? '';
      return text.includes('take_over') || text.includes('TOR') || text.includes('OVERRIDE');
    }, { timeout: TIMEOUT });

    const asdrSection = page.locator('text=/④ ASDR Ledger/');
    await expect(asdrSection).toBeVisible();
  });

});
