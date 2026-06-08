import { test, expect } from '@playwright/test';

test.describe('Scenario Builder Map Navigation', () => {
  test('selecting coastal_archipelago switches tileset and jumps camera center to Malacca Strait', async ({ page }) => {
    // Go to Scenario Builder page
    await page.goto('/#/scenario');

    // Wait for the scenario screen to load
    await expect(page.locator('[data-testid="simulation-scenario"]')).toBeVisible({ timeout: 10000 });

    // Open the Operational Domain (ODD) tab
    await page.click('[data-testid="scenario-tab-odd"]');

    // Wait for the region selection select dropdown to be visible
    const selectDropdown = page.locator('select').filter({ has: page.locator('option[value="coastal_archipelago"]') }).first();
    await expect(selectDropdown).toBeVisible({ timeout: 5000 });

    const options = await selectDropdown.evaluate((el: HTMLSelectElement) =>
      Array.from(el.options).map(o => ({ value: o.value, text: o.text }))
    );
    console.log('Found options:', options);

    // Select "近海群岛" (coastal_archipelago)
    await selectDropdown.selectOption('coastal_archipelago');

    // Wait for the map to re-center on Malacca Strait
    await page.waitForFunction(
      () => {
        const map = (window as any).__maplibre_map;
        if (!map) return false;
        const center = map.getCenter();
        return center.lng > 90 && center.lng < 120 && center.lat > -15 && center.lat < 15;
      },
      { timeout: 15000 }
    );

    // Get final map center to verify
    const center = await page.evaluate(() => {
      const map = (window as any).__maplibre_map;
      const c = map.getCenter();
      return { lng: c.lng, lat: c.lat };
    });

    console.log('Map jumped successfully. New center:', center);
    expect(center.lng).toBeCloseTo(104.0, 1);
    expect(center.lat).toBeCloseTo(-2.5, 1);

    // Select "港口水域" (harbour_approach) back and verify it jumps back to Norway
    await selectDropdown.selectOption('harbour_approach');

    // Wait for the map to re-center on Norway
    await page.waitForFunction(
      () => {
        const map = (window as any).__maplibre_map;
        if (!map) return false;
        const center = map.getCenter();
        return center.lng > 0 && center.lng < 30 && center.lat > 50 && center.lat < 70;
      },
      { timeout: 15000 }
    );

    const centerNorway = await page.evaluate(() => {
      const map = (window as any).__maplibre_map;
      const c = map.getCenter();
      return { lng: c.lng, lat: c.lat };
    });

    console.log('Map jumped back to Norway. New center:', centerNorway);
    expect(centerNorway.lng).toBeCloseTo(10.38, 1);
    expect(centerNorway.lat).toBeCloseTo(63.44, 1);
  });
});
