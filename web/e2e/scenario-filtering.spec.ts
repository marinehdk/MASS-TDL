import { test, expect } from '@playwright/test';

test.describe('Scenario List Filtering by ODD', () => {
  test.beforeEach(async ({ page }) => {
    // Mock the scenarios list API response
    await page.route('**/api/v1/scenarios', async (route) => {
      await route.fulfill({
        contentType: 'application/json',
        status: 200,
        body: JSON.stringify([
          {
            id: 'colreg-rule14-ho',
            name: 'Colreg Rule14 Ho',
            encounter_type: 'head-on',
            folder: 'COLREGs测试',
            is_baseline: true,
            folder_tags: [],
            last_ci_result: null,
            latitude: 63.44,
            longitude: 10.38,
            odd_domain: 'harbour_approach'
          },
          {
            id: 'colreg-malacca-test',
            name: 'Colreg Malacca Test',
            encounter_type: 'crossing',
            folder: 'Malacca测试',
            is_baseline: false,
            folder_tags: [],
            last_ci_result: null,
            latitude: -2.5,
            longitude: 106.4,
            odd_domain: 'coastal_archipelago'
          }
        ])
      });
    });
  });

  test('scenario library filters out incompatible Norway scenarios when coastal_archipelago is selected', async ({ page }) => {
    // Go to Scenario Builder page
    await page.goto('/#/scenario');

    // Wait for the scenario screen to load
    await expect(page.locator('[data-testid="simulation-scenario"]')).toBeVisible({ timeout: 10000 });

    // Open the ODD tab (should be default anyway, but make sure)
    const selectDropdown = page.locator('select').filter({ has: page.locator('option[value="coastal_archipelago"]') }).first();
    const isDropdownVisible = await selectDropdown.isVisible();
    if (!isDropdownVisible) {
      await page.click('[data-testid="scenario-tab-odd"]');
    }
    await expect(selectDropdown).toBeVisible({ timeout: 5000 });

    // Verify default region is harbour_approach
    await expect(selectDropdown).toHaveValue('harbour_approach');

    // Switch to Scenario Library tab
    await page.click('[data-testid="scenario-tab-library"]');

    // We expect COLREGs测试 folder to exist and be visible
    const colregsFolder = page.locator('span', { hasText: 'COLREGs测试' });
    await expect(colregsFolder).toBeVisible({ timeout: 5000 });
    
    // Toggle folder if children are not visible
    const rule14Scenario = page.locator('[data-testid="scenario-card-colreg-rule14-ho"]');
    if (!await rule14Scenario.isVisible()) {
      await colregsFolder.click();
    }
    await expect(rule14Scenario).toBeVisible({ timeout: 5000 });

    // Verify Malacca tests are hidden in Norway mode
    const malaccaFolder = page.locator('span', { hasText: 'Malacca测试' });
    await expect(malaccaFolder).not.toBeVisible();

    // Now go back ODD tab and switch to Malacca Strait
    await page.click('[data-testid="scenario-tab-odd"]');
    await selectDropdown.selectOption('coastal_archipelago');

    // Go back to Scenario Library tab
    await page.click('[data-testid="scenario-tab-library"]');

    // Verify COLREGs测试 is no longer present since all of its scenarios are Norway coordinates
    await expect(colregsFolder).not.toBeVisible();
    await expect(rule14Scenario).not.toBeVisible();

    // Verify Malacca folder is visible
    await expect(malaccaFolder).toBeVisible({ timeout: 5000 });
    const malaccaScenario = page.locator('[data-testid="scenario-card-colreg-malacca-test"]');
    if (!await malaccaScenario.isVisible()) {
      await malaccaFolder.click();
    }
    await expect(malaccaScenario).toBeVisible({ timeout: 5000 });

    // Switch back to harbour_approach
    await page.click('[data-testid="scenario-tab-odd"]');
    await selectDropdown.selectOption('harbour_approach');

    // Go back to Scenario Library tab
    await page.click('[data-testid="scenario-tab-library"]');
    
    // Verify Norway folder is back and Malacca is gone
    await expect(colregsFolder).toBeVisible({ timeout: 5000 });
    await expect(malaccaFolder).not.toBeVisible();
  });
});
