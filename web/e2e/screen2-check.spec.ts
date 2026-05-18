import { test, expect } from '@playwright/test';

test.describe('Screen 2 - Simulation Check', () => {
  test('three-pane layout renders', async ({ page }) => {
    await page.goto('/#/check/test_demo');
    await expect(page.locator('text=GATE PROGRESS')).toBeVisible({ timeout: 5000 });
    await expect(page.locator('text=ACTIONS & LOGS')).toBeVisible({ timeout: 5000 });
  });

  test('gate rows render 6 items', async ({ page }) => {
    await page.goto('/#/check/test_demo');
    await page.waitForSelector('text=GATE 1', { timeout: 10000 });
    for (let i = 1; i <= 6; i++) {
      await expect(page.locator(`text=GATE ${i}`).first()).toBeVisible();
    }
  });

  test('click gate row switches diagnostic view', async ({ page }) => {
    await page.goto('/#/check/test_demo');
    await page.waitForSelector('text=GATE 3', { timeout: 10000 });
    await page.locator('text=GATE 3').first().click();
    await expect(page.locator('text=YAML Diff')).toBeVisible({ timeout: 5000 });
  });

  test('abort button returns to scenario screen', async ({ page }) => {
    await page.goto('/#/check/test_demo');
    await page.waitForSelector('text=ABORT', { timeout: 10000 });
    await page.locator('text=ABORT').first().click();
    await expect(page).toHaveURL(/#\/scenario/);
  });

  test('keyboard R triggers re-run', async ({ page }) => {
    await page.goto('/#/check/test_demo');
    await page.waitForTimeout(2000);
    await page.keyboard.press('r');
    await page.waitForTimeout(1000);
  });
});
