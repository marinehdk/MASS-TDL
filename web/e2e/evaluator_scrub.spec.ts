import { test, expect } from '@playwright/test';

test.describe('Simulation Evaluator - Time Scrubbing E2E', () => {
  test('interactive timeline scrubbing syncs map and ledger', async ({ page }) => {
    // Intercept and mock last_run scoring API
    await page.route('**/api/v1/scoring/last_run', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          run_id: 'test-run-id-12345',
          verdict: 'pass',
          kpis: {
            min_cpa_nm: 0.35,
            tcpa_min_s: 120,
            avg_rot_dpm: 5.2,
            max_rudder_deg: 25.0,
            grounding_risk_score: 0.95,
            route_deviation_nm: 0.12,
            time_to_mrm_s: 0,
          },
          scoring_dimensions: {
            safety: 1.0,
            rule_compliance: 1.0,
            delay_penalty: 0.0,
            action_magnitude_penalty: 0.0,
            phase_score: 1.0,
            plausibility: 1.0,
            total: 1.0,
          },
          rule_chain: [],
        }),
      });
    });

    // Intercept and mock ASDR events/ledger API
    await page.route('**/api/v1/asdr/events', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          events: [
            { t: 0.0, type: 'INIT', module: 'M1', payload: { scene: 'TRANSIT' } },
            { t: 25.0, type: 'T01_DET', module: 'M2', payload: {} },
            { t: 38.0, type: 'CPA_PROJ', module: 'M2', payload: {} },
            { t: 47.0, type: 'SCENE_CHG', module: 'M1', payload: {} },
            { t: 49.0, type: 'COLREG_R14', module: 'M6', payload: {} },
            { t: 52.0, type: 'MPC_BRANCH', module: 'M5', payload: {} },
            { t: 140.0, type: 'CPA_MIN', module: 'M2', payload: {} },
            { t: 152.0, type: 'SCENE_CHG', module: 'M1', payload: {} },
            { t: 600.0, type: 'END', module: 'M1', payload: {} },
          ],
          ledger: [
            { time: 'T+00:00', type: 'INIT', module: 'M1', payload: { scene: 'TRANSIT' }, hash: 'hash1' },
            { time: 'T+00:25', type: 'T01_DET', module: 'M2', payload: {}, hash: 'hash2' },
            { time: 'T+00:38', type: 'CPA_PROJ', module: 'M2', payload: {}, hash: 'hash3' },
            { time: 'T+00:47', type: 'SCENE_CHG', module: 'M1', payload: {}, hash: 'hash4' },
            { time: 'T+00:49', type: 'COLREG_R14', module: 'M6', payload: {}, hash: 'hash5' },
            { time: 'T+00:52', type: 'MPC_BRANCH', module: 'M5', payload: {}, hash: 'hash6' },
            { time: 'T+02:20', type: 'CPA_MIN', module: 'M2', payload: {}, hash: 'hash7' },
            { time: 'T+02:32', type: 'SCENE_CHG', module: 'M1', payload: {}, hash: 'hash8' },
            { time: 'T+10:00', type: 'END', module: 'M1', payload: {}, hash: 'hash9' },
          ],
        }),
      });
    });

    // Navigate to evaluator page
    await page.goto('/#/evaluator/latest');
    
    // Confirm container components render
    await expect(page.locator('[data-testid="asdr-ledger"]')).toBeVisible({ timeout: 10000 });
    await expect(page.locator('[data-testid="trajectory-replay"]')).toBeVisible({ timeout: 10000 });
    await expect(page.locator('[data-testid="timeline-6lane"]')).toBeVisible({ timeout: 10000 });
    await expect(page.locator('[data-testid="decision-tree"]')).toBeVisible({ timeout: 10000 });
    await expect(page.locator('[data-testid="boundary-diagnostics"]')).toBeVisible({ timeout: 10000 });

    // Scrub timeline to 20% progress (which is 120s)
    const track = page.locator('[data-testid="timeline-playback"]');
    const box = await track.boundingBox();
    if (box) {
      // Click at 20% of timeline width
      const x = box.x + box.width * 0.2;
      const y = box.y + box.height / 2;
      await page.mouse.click(x, y);
      
      // Highlighted row should be visible inside the ASDR ledger
      // 120s is closest to 140s (T+02:20) in the ledger, which is on page 1 (first 50 items)
      await expect(page.locator('[data-testid="active-row"]')).toBeVisible();
      
      const activeText = await page.locator('[data-testid="active-row"]').textContent();
      expect(activeText).toContain('T+02:20');
    }
  });
});
