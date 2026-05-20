import { test, expect } from '@playwright/test';

test('Gate 4 ODD alignment passes for imazu-01-ho', async ({ page }) => {
  await page.goto('/#/check/imazu-01-ho');

  // Wait for all gates to finish (up to 30s)
  await page.waitForFunction(
    () => {
      const texts = [...document.querySelectorAll('*')].map(el => el.textContent ?? '');
      return texts.some(t => t.includes('GATE 4')) &&
             (texts.some(t => t.includes('PASSED') || t.includes('FAILED')));
    },
    { timeout: 30000 }
  );

  // Gate 4 row should NOT contain FAILED
  const gate4Row = page.locator('[data-gate-id="4"], .gate-row').filter({ hasText: 'GATE 4' }).first();
  await expect(gate4Row).not.toContainText('FAILED', { timeout: 5000 });

  // Rationale should not mention ODD bounds violation
  const logPanel = page.locator('.log-panel, [class*="log"], [class*="diagnostic"]').first();
  const pageText = await page.content();
  expect(pageText).not.toContain('ODD bounds violation');
});
