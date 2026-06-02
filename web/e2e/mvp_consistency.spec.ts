import { test, expect, Page, APIRequestContext } from '@playwright/test';
import * as fs from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

/**
 * MVP consistency probe.
 *
 * Purpose: prove that THREE assertions, driven through the real HMI path,
 * correctly reflect physical ground truth — so an agent's test verdict and
 * the user's web-page experience cannot diverge.
 *
 *   A_rtf     — measured Δsim/Δwall ≈ nominal rate   (catches "倍速失效")
 *   A_turn    — net heading change from initial      (catches "船不转 / 0°↔360° 振荡")
 *   A_recon   — HMI-store value == backend-trace value (catches test/web split itself)
 *   A_stateful — re-activate within the same sil-nodes process must also turn
 *               (catches "cold-start works, warm-start stuck" — the real bug)
 *
 * Hard rule: NO `?? 0` masking. A missing field is a real divergence and must
 * throw, not silently default to a passing value.
 */

const SCENARIO = process.env.SCENARIO ?? 'colreg-rule14-ho';
const RATE = parseInt(process.env.RATE ?? '10');
const SAMPLE_WALL_MS = parseInt(process.env.SAMPLE_WALL_MS ?? '60000'); // need enough sim-time to see heading change ~200s into run
const SAMPLE_INTERVAL_MS = 400;

const RUNS_DIR = path.resolve(__dirname, '../../runs/mvp_consistency');

// On the dedicated A4000 server (no desktop contention) HMI-path RTF tracks
// headless ~10× (measured 10.00×, deterministic). Band catches both stuck-at-1×
// and runaway. If running locally on Mac, the local-host CPU contention will
// cap HMI-path RTF near 1.6× — use the A4000 for deterministic band.
const RTF_BAND: [number, number] = [7.0, 12.0];
const RTF_SKIP_SAMPLES = 2;     // drop the rate-switch / activation catch-up transient (samples are ~3s apart)
const TURN_MIN_DEG = 5;        // legacy threshold; A_turn_net uses the strict one
const TURN_NET_MIN_DEG = 60;   // net angular change from first sample (NOT range).
                                // 60° catches "ship oscillates 0↔360" (range=360, net=0)
                                // AND "ship frozen at 0°" (range=0, net=0).
const RECON_MEDIAN_TOL_DEG = 10; // store-vs-backend heading median |Δ| (1-tick WS skew during turns)

test.describe.configure({ mode: 'serial', timeout: 720_000 });

type HmiSample = { wall: number; sim_t: number; hdg: number };
type BackendSample = { wall: number; sim_t: number; hdg: number; backend_wall_t: number; oss_sim_t: number };

// Least-squares slope of ys vs xs (robust to the 2s trace-flush staircase;
// endpoint deltas alias badly on the bursty snapshot, regression does not).
function slope(xs: number[], ys: number[]): number {
  const n = xs.length;
  if (n < 2) throw new Error('slope needs >= 2 points');
  const mx = xs.reduce((a, b) => a + b, 0) / n;
  const my = ys.reduce((a, b) => a + b, 0) / n;
  let num = 0, den = 0;
  for (let i = 0; i < n; i++) { num += (xs[i] - mx) * (ys[i] - my); den += (xs[i] - mx) ** 2; }
  if (den === 0) throw new Error('slope: zero variance in x (wall clock did not advance)');
  return num / den;
}

function median(xs: number[]): number {
  if (!xs.length) throw new Error('median of empty series — no samples collected');
  const s = [...xs].sort((a, b) => a - b);
  return s[Math.floor(s.length / 2)];
}

// Read HMI store — throw loud if the field path is absent (no default masking).
async function readHmiSample(page: Page): Promise<HmiSample> {
  const raw = await page.evaluate(() => {
    const st = (window as any).__TELEMETRY_STORE__?.getState?.();
    if (!st) return { err: 'no __TELEMETRY_STORE__' };
    return {
      sim_t: st.lifecycleStatus?.sim_time,
      hdg: st.ownShip?.pose?.heading,
    };
  });
  if ((raw as any).err) throw new Error(`HMI store unreadable: ${(raw as any).err}`);
  const { sim_t, hdg } = raw as { sim_t: unknown; hdg: unknown };
  if (typeof sim_t !== 'number') throw new Error(`HMI lifecycleStatus.sim_time not a number: ${JSON.stringify(sim_t)}`);
  if (typeof hdg !== 'number') throw new Error(`HMI ownShip.pose.heading not a number: ${JSON.stringify(hdg)}`);
  // ROS2 msg heading is in RADIANS (the bridge converts to deg only for the
  // trace file).  Convert here so A_recon compares rad→deg against the trace.
  return { wall: Date.now(), sim_t, hdg: hdg * 57.29577951308232 };
}

// Read backend trace snapshot via the SAME origin proxy the HMI uses.
async function readBackendSample(req: APIRequestContext): Promise<BackendSample> {
  const res = await req.get('/api/v1/debug/snapshot');
  if (!res.ok()) throw new Error(`/debug/snapshot HTTP ${res.status()}`);
  const body = await res.json();
  const oss = body?.topics?.['/sil/own_ship_state'];
  if (!oss) throw new Error('snapshot missing /sil/own_ship_state topic');
  if (typeof body.sim_t !== 'number') throw new Error(`snapshot sim_t not a number: ${JSON.stringify(body.sim_t)}`);
  if (typeof oss.heading_deg !== 'number') throw new Error(`snapshot heading_deg not a number: ${JSON.stringify(oss.heading_deg)}`);
  if (typeof oss.sim_t !== 'number') throw new Error(`snapshot oss.sim_t not a number: ${JSON.stringify(oss.sim_t)}`);
  if (typeof oss.wall_t !== 'number') throw new Error(`snapshot oss.wall_t not a number: ${JSON.stringify(oss.wall_t)}`);
  // oss.sim_t and oss.wall_t come from the SAME own_ship record → no cross-field skew.
  return { wall: Date.now(), sim_t: body.sim_t, hdg: oss.heading_deg, backend_wall_t: oss.wall_t, oss_sim_t: oss.sim_t };
}

test(`MVP consistency rate=${RATE} [${SCENARIO}]`, async ({ page, request }) => {
  const runDir = path.join(RUNS_DIR, `${RATE}x_${Date.now()}`);
  fs.mkdirSync(runDir, { recursive: true });

  // ===== Drive the real 3-screen UI (path proven in hmi_consistency.spec.ts) =====
  await page.goto('/#scenario');
  await page.waitForSelector('[data-testid="simulation-scenario"]');
  await page.click('[data-testid="scenario-tab-vessel"]');
  await page.click('text=下一步');
  // The scenario card lives inside the COLREGs folder — expand it explicitly
  // (waitFor, not a racy isVisible() snapshot) and bound the card wait so a
  // navigation break fails fast with a clear message instead of hanging 600s.
  const colregsFolder = page.locator('text=COLREGs测试').first();
  await colregsFolder.waitFor({ state: 'visible', timeout: 30_000 });
  await colregsFolder.click();
  const card = page.locator(`[data-testid="scenario-card-${SCENARIO}"]`);
  await card.waitFor({ state: 'visible', timeout: 30_000 });
  await card.click();
  await page.click('[data-testid="scenario-confirm"]', { timeout: 15_000 });
  const runBtn = page.getByRole('button', { name: /进行仿真检查/ });
  await runBtn.waitFor({ state: 'visible' });
  await runBtn.click();

  // ===== Preflight → monitor =====
  await page.waitForURL(`**/#/check/${SCENARIO}`);
  await page.waitForSelector('[data-testid="preflight"]');
  await page.waitForFunction(() => {
    const el = document.querySelector('[data-testid="preflight-status"]');
    return el && /GO/.test(el.textContent || '');
  }, { timeout: 180_000 });
  await page.waitForURL(`**/#/monitor/${SCENARIO}`, { timeout: 120_000 });

  // ===== Switch rate via the real button (same as a user click) =====
  await page.click(`[data-testid="rate-btn-${RATE}x"]`);
  // Advance simulation to well PAST the expected avoidance time (200s sim per
  // scenario config) so A_turn samples during the active maneuver.
  const AVOIDANCE_SIM_SEC = 250;
  await page.waitForFunction(
    (target) => {
      const t = (window as any).__TELEMETRY_STORE__?.getState?.()?.lifecycleStatus?.sim_time;
      return typeof t === 'number' && t >= target;
    },
    AVOIDANCE_SIM_SEC,
    { timeout: 180_000, polling: 500 },
  );
  await page.waitForTimeout(1000);  // let one more trace flush land

  // ===== Sample DURING the avoidance maneuver =====
  const hmi: HmiSample[] = [];
  const backend: BackendSample[] = [];
  const t0 = Date.now();
  while (Date.now() - t0 < SAMPLE_WALL_MS) {
    // backend first, then HMI, back-to-back to minimise skew per pair
    backend.push(await readBackendSample(request));
    hmi.push(await readHmiSample(page));
    await page.waitForTimeout(SAMPLE_INTERVAL_MS);
  }
  fs.writeFileSync(path.join(runDir, 'series.json'),
    JSON.stringify({ rate: RATE, hmi, backend }, null, 2));
  await page.screenshot({ path: path.join(runDir, 'monitor.png'), fullPage: true });

  expect(backend.length, 'collected backend samples').toBeGreaterThan(5);
  expect(hmi.length, 'collected hmi samples').toBeGreaterThan(5);

  // ===== A_rtf : backend own_ship sim_t vs its own wall_t → true RTF =====
  // Regress over the SETTLED segment (drop the rate-switch catch-up transient).
  // Endpoint deltas alias on the 2s trace-flush staircase; least-squares slope
  // over many samples recovers the true real-time factor (verified ~8.8 @ 10x
  // via independent REST regression).
  const settled = backend.slice(RTF_SKIP_SAMPLES)
    .filter(s => Number.isFinite(s.oss_sim_t) && Number.isFinite(s.backend_wall_t));
  expect(settled.length, `A_rtf: enough settled samples (got ${settled.length})`).toBeGreaterThan(6);
  const rtf = slope(settled.map(s => s.backend_wall_t), settled.map(s => s.oss_sim_t));
  console.log(`A_rtf: regressed RTF=${rtf.toFixed(2)} over ${settled.length} settled samples (band [${RTF_BAND[0].toFixed(1)},${RTF_BAND[1].toFixed(1)}], nominal ${RATE}x)`);
  expect(rtf, `A_rtf: regressed RTF ${rtf.toFixed(2)} >= ${RTF_BAND[0].toFixed(1)} (would catch stuck-at-1x)`)
    .toBeGreaterThanOrEqual(RTF_BAND[0]);
  expect(rtf, `A_rtf: regressed RTF ${rtf.toFixed(2)} <= ${RTF_BAND[1].toFixed(1)}`)
    .toBeLessThanOrEqual(RTF_BAND[1]);

  // ===== A_turn : own-ship actually changed heading (real maneuver) =====
  // Use NET angular change from first sample, NOT max-min range.
  //  - max-min range: passes on 0°↔360° open-loop oscillation (p50=0, net=0)
  //    — the false-green we saw on the Mac on 2026-06-02 morning.
  //  - net change: passes only if the ship commits to a sustained turn.
  const bHdgs = backend.map(s => s.hdg);
  const turnRange = Math.max(...bHdgs) - Math.min(...bHdgs);
  const hdg0 = bHdgs[0];
  const lastFromStart = bHdgs.map(h => {
    let d = Math.abs(h - hdg0) % 360;
    if (d > 180) d = 360 - d;
    return d;
  });
  const netTurnDeg = Math.max(...lastFromStart);
  console.log(`A_turn: range=${turnRange.toFixed(1)}° net|max-from-start|=${netTurnDeg.toFixed(1)}° (min=${Math.min(...bHdgs).toFixed(1)} max=${Math.max(...bHdgs).toFixed(1)} first=${hdg0.toFixed(1)})`);
  expect(netTurnDeg, `A_turn: net heading change from initial must be > ${TURN_NET_MIN_DEG}° (rule14 avoidance; range-based assertion passed on the 0°↔360° oscillation bug)`)
    .toBeGreaterThan(TURN_NET_MIN_DEG);

  // ===== A_recon : HMI store == backend trace, matched by sim_t =====
  // For each HMI sample, find the backend sample with nearest sim_t and compare heading.
  const diffs: number[] = [];
  for (const h of hmi) {
    let best: BackendSample | null = null;
    let bestDt = Infinity;
    for (const b of backend) {
      const dt = Math.abs(b.sim_t - h.sim_t);
      if (dt < bestDt) { bestDt = dt; best = b; }
    }
    // 12s sim_t window: HMI store ticks ~10s sim (TS sim_clock propagation
    // through WebSocket + zustand); backend OSS samples every ~2s sim.
    // Tighter (< 2s) gives 0/75 pairs and spurious failure.
    if (best && bestDt < 12.0) {
      // angular difference, wrapped to [0,180]
      let d = Math.abs(h.hdg - best.hdg) % 360;
      if (d > 180) d = 360 - d;
      diffs.push(d);
    }
  }
  const medianDiff = median(diffs);
  console.log(`A_recon: matched=${diffs.length}/${hmi.length} median|Δhdg|=${medianDiff.toFixed(2)}° max=${Math.max(...diffs).toFixed(2)}°`);
  expect(diffs.length, 'A_recon: enough sim_t-matched pairs').toBeGreaterThan(3);
  expect(medianDiff, `A_recon: HMI heading vs backend median |Δ| ${medianDiff.toFixed(2)}° must be < ${RECON_MEDIAN_TOL_DEG}°`)
    .toBeLessThan(RECON_MEDIAN_TOL_DEG);

  fs.writeFileSync(path.join(runDir, 'verdict.json'), JSON.stringify({
    rate: RATE, rtf, rtf_band: RTF_BAND,
    turn_range_deg: turnRange, turn_net_deg: netTurnDeg, turn_net_min_deg: TURN_NET_MIN_DEG,
    recon_matched: diffs.length, recon_median_diff_deg: medianDiff, recon_tol_deg: RECON_MEDIAN_TOL_DEG,
  }, null, 2));
});

// ----------------------------------------------------------------------------
// A_stateful: same scenario, second lifecycle cycle in the SAME sil-nodes process.
// Bug probed: cold-start (first activate) works, warm-start (subsequent
// activates in the same process) leaves the ship at heading 0° for the full
// 600s sim. First run turned 337.7°, retries all 0.0° (2026-06-02 A4000).
//
// This test runs in the SAME sil-nodes container that the cold-start test used.
// It does: cleanup → configure → activate (again) → wait for sim_t≥250 →
// sample. If A_stateful fails, the system has a STATEFUL avoidance bug.
// ----------------------------------------------------------------------------
test(`A_stateful warm-start rate=${RATE} [${SCENARIO}]`, async ({ page, request }) => {
  const runDir = path.join(RUNS_DIR, `${RATE}x_stateful_${Date.now()}`);
  fs.mkdirSync(runDir, { recursive: true });

  // Re-use the same UI path but skip the navigation — go straight to the
  // scenario + preflight + monitor flow as if the user pressed "再次仿真".
  await page.goto('/#scenario');
  await page.waitForSelector('[data-testid="simulation-scenario"]');
  await page.click('[data-testid="scenario-tab-vessel"]');
  await page.click('text=下一步');
  const colregsFolder = page.locator('text=COLREGs测试').first();
  await colregsFolder.waitFor({ state: 'visible', timeout: 30_000 });
  await colregsFolder.click();
  const card = page.locator(`[data-testid="scenario-card-${SCENARIO}"]`);
  await card.waitFor({ state: 'visible', timeout: 30_000 });
  await card.click();
  await page.click('[data-testid="scenario-confirm"]', { timeout: 15_000 });
  const runBtn = page.getByRole('button', { name: /进行仿真检查/ });
  await runBtn.waitFor({ state: 'visible' });
  await runBtn.click();

  await page.waitForURL(`**/#/check/${SCENARIO}`);
  await page.waitForSelector('[data-testid="preflight"]');
  await page.waitForFunction(() => {
    const el = document.querySelector('[data-testid="preflight-status"]');
    return el && /GO/.test(el.textContent || '');
  }, { timeout: 180_000 });
  await page.waitForURL(`**/#/monitor/${SCENARIO}`, { timeout: 120_000 });

  await page.click(`[data-testid="rate-btn-${RATE}x"]`);
  await page.waitForFunction(
    (target) => {
      const t = (window as any).__TELEMETRY_STORE__?.getState?.()?.lifecycleStatus?.sim_time;
      return typeof t === 'number' && t >= target;
    },
    250,
    { timeout: 180_000, polling: 500 },
  );
  await page.waitForTimeout(1000);

  const hmi: HmiSample[] = [];
  const backend: BackendSample[] = [];
  const t0 = Date.now();
  while (Date.now() - t0 < SAMPLE_WALL_MS) {
    backend.push(await readBackendSample(request));
    hmi.push(await readHmiSample(page));
    await page.waitForTimeout(SAMPLE_INTERVAL_MS);
  }
  fs.writeFileSync(path.join(runDir, 'series.json'),
    JSON.stringify({ rate: RATE, hmi, backend }, null, 2));

  const bHdgs = backend.map(s => s.hdg);
  const hdg0 = bHdgs[0];
  const lastFromStart = bHdgs.map(h => {
    let d = Math.abs(h - hdg0) % 360;
    if (d > 180) d = 360 - d;
    return d;
  });
  const netTurnDeg = Math.max(...lastFromStart);
  console.log(`A_stateful (warm-start): net|max-from-start|=${netTurnDeg.toFixed(1)}° (first=${hdg0.toFixed(1)} min=${Math.min(...bHdgs).toFixed(1)} max=${Math.max(...bHdgs).toFixed(1)})`);
  expect(netTurnDeg, `A_stateful: warm-start (second activate in same process) must also produce a real turn > ${TURN_NET_MIN_DEG}°. Got net=${netTurnDeg.toFixed(1)}° (0°↔360° oscillation passes range-based, net-from-start does not).`)
    .toBeGreaterThan(TURN_NET_MIN_DEG);

  fs.writeFileSync(path.join(runDir, 'verdict.json'), JSON.stringify({
    rate: RATE, kind: 'stateful_warm_start',
    net_turn_deg: netTurnDeg, net_min_deg: TURN_NET_MIN_DEG,
  }, null, 2));
});
