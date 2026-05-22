// SPDX-License-Identifier: MIT
// E2E: D2.5 Grounding Hazard Detection — DoD #17
// Injects own-ship telemetry and verifies the grounding-hazard-highlight layer
// is created and its visibility is queryable (not null).

import { test, expect } from '@playwright/test';

const ENGINEER_URL = '/#/monitor/imazu-01-ho?dev=1';

test.describe('D2.5 — Grounding hazard detection', () => {

  test('grounding-hazard-highlight layer exists after own-ship injection', async ({ page }) => {
    await page.goto(ENGINEER_URL);
    await page.waitForSelector('[data-testid="simulation-monitor"]', { timeout: 15_000 });

    // Inject own-ship position heading east (COG 90°) at 12 kn
    await page.evaluate(() => {
      const store = (window as any).__ZUSTAND_TELEMETRY_STORE__;
      if (!store) return;
      store.getState().updateOwnShip({
        stamp: { seconds: Math.floor(Date.now() / 1000) },
        pose: { lat: 63.42, lon: 10.38, heading: Math.PI / 2 },
        kinematics: { sog: 6.17, cog: Math.PI / 2, rot: 0, u: 6.17, v: 0, r: 0 },
      });
    });

    // Wait for the 10 Hz interval to fire and create the layer
    await page.waitForTimeout(500);

    // Check that the highlight layer was created by the grounding effect
    const visibility = await page.evaluate(() => {
      const map = (window as any).__maplibre_map;
      if (!map) return null;
      try {
        return map.getLayoutProperty('grounding-hazard-highlight', 'visibility');
      } catch {
        return null;
      }
    });

    expect(visibility).not.toBeNull();

    await page.screenshot({
      path: '../docs/Design/Phase 2/D2.5-sil-m1-m6-integration/evidence/grounding_hazard_layer.png',
    });
  });

});
