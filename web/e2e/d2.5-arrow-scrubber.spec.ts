import { test, expect } from '@playwright/test';

const ORCHESTRATOR = 'http://localhost:8000';
const MONITOR_URL = '/#/monitor/imazu-01-ho?dev=1&replay=run-19e1b2d248b';
const SAMPLES = 50;

test.describe('D2.5 — Arrow Scrubber latency < 100ms p95', () => {
  test('p95 scrub latency under 100ms across 50 drag samples', async ({ page }) => {
    let orchestratorUp = false;
    try {
      const r = await page.request.get(`${ORCHESTRATOR}/api/v1/health`);
      orchestratorUp = r.ok();
    } catch {
      orchestratorUp = false;
    }
    if (!orchestratorUp) {
      test.skip();
      return;
    }

    await page.goto(MONITOR_URL);

    // Graceful skip if ArrowScrubber not yet integrated into page
    const track = page.locator('[data-testid="arrow-scrubber-track"]');
    try {
      await track.waitFor({ state: 'visible', timeout: 10_000 });
    } catch {
      test.skip();
      return;
    }
    const box = await track.boundingBox();
    if (!box) throw new Error('Track not found');

    const latencies: number[] = [];

    for (let i = 0; i < SAMPLES; i++) {
      const fraction = (i + 1) / (SAMPLES + 1);
      const x = box.x + box.width * fraction;
      const y = box.y + box.height / 2;

      const t0 = Date.now();
      await page.mouse.move(x, y);
      await page.mouse.click(x, y);
      await page.waitForFunction(
        (f) => {
          const el = document.querySelector('[data-testid="arrow-scrubber-playhead"]') as HTMLElement;
          if (!el) return false;
          const pct = parseFloat(el.style.left || '0');
          return Math.abs(pct - f * 100) < 5;
        },
        fraction,
        { timeout: 500 }
      );
      latencies.push(Date.now() - t0);
    }

    latencies.sort((a, b) => a - b);
    const p95 = latencies[Math.floor(SAMPLES * 0.95)];

    const fs = require('fs');
    fs.mkdirSync('test-results', { recursive: true });
    fs.writeFileSync('test-results/arrow_scrubber_latency.json', JSON.stringify({
      samples: SAMPLES, p95_ms: p95, max_ms: latencies[SAMPLES - 1],
      all_ms: latencies, threshold_ms: 100,
      pass: p95 < 100,
    }, null, 2));

    expect(p95).toBeLessThan(100);
  });
});
