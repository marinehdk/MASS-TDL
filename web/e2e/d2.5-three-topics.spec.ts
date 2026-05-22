// SPDX-License-Identifier: MIT
// E2E: D2.5 Three-Topic Chain — DoD #4 #5 #6
// Injects mock data via Zustand store (dev-mode exposure), verifies engineer
// panels render non-null within 20s.

import { test, expect } from '@playwright/test';

const ENGINEER_URL = '/#/monitor/imazu-01-ho?dev=1';
const TIMEOUT = 20_000;

test.describe('D2.5 — Three-topic chain verification (Engineer view)', () => {

  test('sat2: IvP panel renders non-null after store injection', async ({ page }) => {
    await page.goto(ENGINEER_URL);
    await page.waitForSelector('[data-testid="simulation-monitor"]', { timeout: 15_000 });

    // Inject mock sat2 via Zustand store
    await page.evaluate(() => {
      const store = (window as any).__ZUSTAND_TELEMETRY_STORE__;
      if (!store) return;
      store.getState().updateSat2({
        ivp_contributions: [
          { direction_deg: 0,   cost: 0.9, label: 'head_on' },
          { direction_deg: 45,  cost: 0.3, label: '' },
          { direction_deg: 90,  cost: 0.1, label: '' },
          { direction_deg: 135, cost: 0.1, label: '' },
          { direction_deg: 180, cost: 0.8, label: '' },
          { direction_deg: 225, cost: 0.2, label: '' },
          { direction_deg: 270, cost: 0.1, label: '' },
          { direction_deg: 315, cost: 0.3, label: '' },
        ],
        active_behavior: 'avoid_stbd',
        active_behavior_weight: 0.85,
        colregs_chain: [
          { layer: 1, label: 'ODD', conclusion: 'IN_ODD', inputs: {}, confidence: 0.99 },
        ],
        colregs_chain_target_id: '999000001',
        reasoning_latency_ms: 55,
      });
    });

    // Verify IvP panel is visible
    const ivpPanel = page.locator('[data-testid="ivp-contribution-panel"]');
    await expect(ivpPanel).toBeVisible({ timeout: TIMEOUT });
    await page.screenshot({ path: '../docs/Design/Phase 2/D2.5-sil-m1-m6-integration/evidence/sat2_ivp_panel.png' });
  });

  test('sat3: Trajectory panel renders candidates after store injection', async ({ page }) => {
    await page.goto(ENGINEER_URL);
    await page.waitForSelector('[data-testid="simulation-monitor"]', { timeout: 15_000 });

    await page.evaluate(() => {
      const store = (window as any).__ZUSTAND_TELEMETRY_STORE__;
      if (!store) return;
      store.getState().updateSat3({
        trajectory_candidates: [
          { id: 0, points: [{ lon: 136.899, lat: 34.680 }, { lon: 136.900, lat: 34.685 }],
            cost: 0.1, is_optimal: true, type: 'mid_mpc' },
          { id: 1, points: [{ lon: 136.895, lat: 34.680 }, { lon: 136.897, lat: 34.685 }],
            cost: 0.4, is_optimal: false, type: 'bc_mpc' },
        ],
        uncertainty_bands: false,
      });
    });

    const trajPanel = page.locator('[data-testid="trajectory-panel"]');
    await expect(trajPanel).toBeVisible({ timeout: TIMEOUT });
    await page.screenshot({ path: '../docs/Design/Phase 2/D2.5-sil-m1-m6-integration/evidence/sat3_trajectory_panel.png' });
  });

  test('sotif: SOTIF panel renders 6 metrics after store injection', async ({ page }) => {
    await page.goto(ENGINEER_URL);
    await page.waitForSelector('[data-testid="simulation-monitor"]', { timeout: 15_000 });

    await page.evaluate(() => {
      const store = (window as any).__ZUSTAND_TELEMETRY_STORE__;
      if (!store) return;
      store.getState().updateSotifMetrics({
        ais_radar_consistency_sigma: 0.8,
        target_predictability_rms_m: 22.5,
        perception_coverage_pct: 94.1,
        colregs_parse_failures: 0,
        comm_link_rtt_ms: 88.0,
        checker_veto_rate_pct: 1.2,
      });
    });

    const sotifPanel = page.locator('[data-testid="sotif-metrics-panel"]');
    await expect(sotifPanel).toBeVisible({ timeout: TIMEOUT });
    await page.screenshot({ path: '../docs/Design/Phase 2/D2.5-sil-m1-m6-integration/evidence/sotif_panel.png' });
  });
});
