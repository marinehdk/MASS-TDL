import { test, expect } from '@playwright/test';
import * as fs from 'fs';

const MONITOR_URL = '/#/monitor/imazu-22-ms?dev=1';
const MIN_FPS = 30;
const MEASUREMENT_DURATION_MS = 3000;

test.describe('D2.5 — Multi-target >=5 ships FPS >= 30 (DoD #19)', () => {
  test('renders >=5 targets at >=30 FPS', async ({ page }) => {
    await page.goto(MONITOR_URL);
    await page.waitForSelector('[data-testid="simulation-monitor"]', { timeout: 15_000 });

    await page.evaluate(() => {
      const store = (window as any).__ZUSTAND_TELEMETRY_STORE__;
      if (!store) return;
      const targets = Array.from({ length: 5 }, (_, i) => ({
        mmsi: 999000000 + i,
        pose: { lat: 34.68 + i * 0.01, lon: 136.90 + i * 0.005, heading: i * 30 * Math.PI / 180 },
        kinematics: { sog: 3.0, cog: i * 0.5, rot: 0, u: 3.0, v: 0, r: 0 },
        shipType: 'Cargo', mode: 'synthetic',
      }));
      store.getState().updateTargets(targets);
    });

    const fps = await page.evaluate(async (duration) => {
      const frames: number[] = [];
      let lastTime = performance.now();
      return new Promise<number>((resolve) => {
        let rafId: number;
        const measure = () => { const now = performance.now(); frames.push(now - lastTime); lastTime = now; };
        const loop = () => { measure(); rafId = requestAnimationFrame(loop); };
        rafId = requestAnimationFrame(loop);
        setTimeout(() => {
          cancelAnimationFrame(rafId);
          if (frames.length < 2) { resolve(0); return; }
          const avgFrameMs = frames.slice(1).reduce((a, b) => a + b, 0) / (frames.length - 1);
          resolve(Math.round(1000 / avgFrameMs));
        }, duration);
      });
    }, MEASUREMENT_DURATION_MS);

    fs.mkdirSync('test-results', { recursive: true });
    fs.writeFileSync('test-results/fps_multiship.json', JSON.stringify({
      target_count: 5, fps_measured: fps, threshold: MIN_FPS,
      pass: fps >= MIN_FPS,
    }, null, 2));

    if (fps < MIN_FPS) {
      console.warn('[ACTION] FPS < 30 -> enable canvas render mode (GAP-008)');
    }

    expect(fps).toBeGreaterThanOrEqual(MIN_FPS);
  });
});
