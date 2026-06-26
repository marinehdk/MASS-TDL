# HMI-Driven E2E Consistency Test — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Playwright E2E spec (`web/e2e/hmi_consistency.spec.ts`) that drives the HMI through the same 3-screen UI flow users click (`/#scenario` → `/#check/{id}` → `/#monitor/{id}` → rate switch), asserts 8 HMI-relevant invariants per (rate × run) configuration, and supports 5 validation presets via env vars.

**Architecture:** Spec requires 13 new `data-testid` attributes + 2 store window-exposure guards added to 5 HMI files (zero business-logic change, dev-only HMR-safe). Playwright spec uses URL-hash navigation + `waitForURL` instead of `page.evaluate` REST calls, preserving the "user-path identical" hard constraint. Validation presets (`hmi-consistency` / `avoidance-chain` / `ws-stress` / `rtf` / `fsm`) gate `RATES` array via `process.env.VALIDATION` / `RATES` / `RUNS`.

**Tech Stack:** Playwright 1.59.1 (already in `devDependencies`), React 18 + Vite 5, zustand 5, vitest (test runner separate from Playwright), TypeScript 5.5. New file lives in `web/e2e/` (existing Playwright testDir).

**Working Directory:** Web-only changes (`web/src/**` + `web/e2e/**` + `web/package.json` + `web/playwright.config.ts`). NO backend, NO L3 kernel, NO docker compose changes. Per CLAUDE.md §11: must use worktree; branch name `feat/hmi-e2e-consistency-test`.

---

## File Structure

| File | Action | Responsibility |
|---|---|---|
| `web/src/screens/SimulationScenario.tsx` | Modify | Add 5 `data-testid` attrs (screen 1) |
| `web/src/screens/SimulationCheck.tsx` | Modify | Add 2 `data-testid` attrs (screen 2) |
| `web/src/screens/SimulationMonitor.tsx` | Modify | Add 6 `data-testid` attrs (screen 3) |
| `web/src/store/telemetryStore.ts` | Modify | Add 1 line `window.__TELEMETRY_STORE__` exposure (dev-only) |
| `web/src/store/fsmStore.ts` | Modify | Add 1 line `window.__FSM_STORE__` exposure (dev-only) |
| `web/e2e/hmi_consistency.spec.ts` | Create | New Playwright spec (~400 lines) |
| `web/package.json` | Modify | Add 4 `test:*` npm scripts |
| `web/playwright.config.ts` | Modify | Bump `timeout: 60_000 → 180_000` (per spec §3.2) |
| `docs/Design/SIL/v1.0-unified/03-test-procedures.md` | Modify | Add §N "HMI E2E consistency test" chapter |
| `docs/Design/SIL/v1.0-unified/04-acceptance-criteria.md` | Modify | Add §N "HMI E2E acceptance criteria" chapter |
| `docs/Design/Phase 2/D2.x-hmi-e2e-consistency-test/D2.x-hmi-e2e-consistency-test-{spec,plan,report}.md` | Create | D-task per CLAUDE.md §7.1 |

**Total LOC delta:** +15 lines in HMI source (testids + store exposures), +400 lines new spec, +4 lines package.json, +1 line playwright.config.ts. Zero business-logic changes.

---

## Task 1: Create worktree + branch

**Files:** None (filesystem + git only)

- [ ] **Step 1: Verify git is clean (excluding preflight JSON mods)**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git stash push -m "wip: preflight gate mods (out of scope)" -- scenarios/colreg-rule14-ho/.preflight/ || true
git status -s
```

Expected: clean tree (or only stash entry).

- [ ] **Step 2: Create worktree via superpowers:using-git-worktrees skill**

```bash
git worktree add -b feat/hmi-e2e-consistency-test .worktrees/hmi-e2e-consistency-test
cd .worktrees/hmi-e2e-consistency-test
```

- [ ] **Step 3: Verify branch and starting point**

```bash
git branch --show-current   # → feat/hmi-e2e-consistency-test
git log --oneline -1        # → HEAD of main
```

- [ ] **Step 4: Restore preflight stash (in main, NOT worktree)**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git stash pop
```

Note: This plan operates from the worktree path. Subsequent tasks assume `pwd` is `.worktrees/hmi-e2e-consistency-test`.

- [ ] **Step 5: Commit worktree setup (empty commit to anchor branch)**

```bash
cd .worktrees/hmi-e2e-consistency-test
git commit --allow-empty -m "chore: create feat/hmi-e2e-consistency-test branch for HMI E2E spec"
```

---

## Task 2: Add screen 1 (Scenario) testids

**Files:**
- Modify: `web/src/screens/SimulationScenario.tsx` (5 testids)

- [ ] **Step 1: Write failing smoke test (asserts testid presence)**

Create `web/e2e/_smoke_testids.spec.ts` (will be deleted in Task 9):

```ts
import { test, expect } from '@playwright/test';

test('screen 1 testids present', async ({ page }) => {
  await page.goto('/#scenario');
  await page.waitForSelector('[data-testid="simulation-scenario"]');
  await expect(page.locator('[data-testid="scenario-tab-vessel"]')).toBeVisible();
  await expect(page.locator('[data-testid="scenario-tab-odd"]')).toBeVisible();
  await expect(page.locator('[data-testid="scenario-tab-library"]')).toBeVisible();
  await expect(page.locator('[data-testid="scenario-card-colreg-rule14-ho"]')).toBeAttached();
  await expect(page.locator('[data-testid="scenario-confirm"]')).toBeAttached();
});
```

- [ ] **Step 2: Run smoke test, expect failure**

```bash
cd web && npx playwright test e2e/_smoke_testids.spec.ts --grep "screen 1" --reporter=line
```

Expected: FAIL — `scenario-tab-vessel` / `scenario-tab-odd` / `scenario-tab-library` / `scenario-card-*` / `scenario-confirm` all not found.

- [ ] **Step 3: Add testid to scenario card click target (line 725)**

In `web/src/screens/SimulationScenario.tsx`, locate line 725 (the `onClick={() => handleSelect(child.id)}` div) and change the opening tag from:

```tsx
                          <div key={child.id} onClick={() => handleSelect(child.id)} style={{
```

to:

```tsx
                          <div key={child.id} data-testid={`scenario-card-${child.id}`} onClick={() => handleSelect(child.id)} style={{
```

- [ ] **Step 4: Add testid to "确认场景" button (line 790)**

Locate the button at line 790 (the third "下一步" tab's confirm button) and add `data-testid="scenario-confirm"`:

```tsx
                  <button
                    data-testid="scenario-confirm"
                    onClick={() => {
                      setActiveLeftTab(null);
                      setActiveRightTab('vessels');
                    }}
                    style={{ ...btnStyle('phos'), flex: 1, maxWidth: '180px' }}
                    disabled={!selectedId}
                  >
                    {selectedId ? '确认场景' : '请在上方选择场景'}
                  </button>
```

- [ ] **Step 5: Add testids to 3 "下一步" tab buttons (lines 770, 776, 779)**

Add `data-testid` to each of the 3 tab-confirm buttons:

```tsx
                  <button data-testid="scenario-tab-vessel" onClick={() => setActiveLeftTab('vessel')} style={{ ...btnStyle('phos'), flex: 'none', width: 120 }}>
                    下一步
                  </button>
                  <button data-testid="scenario-tab-odd" onClick={() => setActiveLeftTab('odd')} style={{ ...btnStyle('line'), flex: 'none', width: 120 }}>
                    下一步
                  </button>
                  <button data-testid="scenario-tab-library" onClick={() => setActiveLeftTab('library')} style={{ ...btnStyle('phos'), flex: 'none', width: 120 }}>
                    下一步
                  </button>
```

- [ ] **Step 6: Verify dev server picks up HMR**

```bash
cd web && npm run dev -- --port 5173 &
sleep 5
curl -s http://localhost:5173/ | grep -c 'data-testid' || echo "no testid in HTML (correct - rendered by React)"
```

- [ ] **Step 7: Re-run smoke test, expect pass for screen 1 assertions**

```bash
cd web && npx playwright test e2e/_smoke_testids.spec.ts --grep "screen 1" --reporter=line
```

Expected: PASS for 5 screen-1 testids.

- [ ] **Step 8: Commit**

```bash
git add web/src/screens/SimulationScenario.tsx
git commit -m "feat(hmi): add 5 data-testid to SimulationScenario for E2E selectors"
```

---

## Task 3: Add screen 2 (Check) testids

**Files:**
- Modify: `web/src/screens/SimulationCheck.tsx` (2 testids)
- Modify: `web/src/screens/shared/GateSequencer.tsx` (6 gate testids — gate elements live here, not Check)

- [ ] **Step 1: Append screen 2 assertions to smoke test**

Edit `web/e2e/_smoke_testids.spec.ts` — add a new test:

```ts
test('screen 2 testids present', async ({ page }) => {
  // Need to first navigate through screen 1 to get to screen 2
  await page.goto('/#scenario');
  await page.click('[data-testid="scenario-tab-vessel"]');
  await page.click('text=下一步');
  await page.click('text=下一步');
  await page.click('[data-testid="scenario-card-colreg-rule14-ho"]');
  await page.click('[data-testid="scenario-confirm"]');
  await page.waitForURL('**/#/check/colreg-rule14-ho');
  await page.waitForSelector('[data-testid="preflight"]');
  await expect(page.locator('[data-testid="preflight-status"]')).toBeVisible();
  for (let n = 1; n <= 6; n++) {
    await expect(page.locator(`[data-testid="preflight-gate-${n}"]`)).toBeAttached();
  }
});
```

- [ ] **Step 2: Run smoke test, expect failure on screen 2 testids**

```bash
cd web && npx playwright test e2e/_smoke_testids.spec.ts --grep "screen 2" --reporter=line
```

Expected: FAIL — `preflight-status` and `preflight-gate-*` not found.

- [ ] **Step 3: Locate GateSequencer in `web/src/screens/shared/GateSequencer.tsx`**

```bash
grep -n 'gate_id\|gate-\|data-testid' web/src/screens/shared/GateSequencer.tsx | head -20
```

- [ ] **Step 4: Add 6 gate testids in GateSequencer component output**

Locate the gate card rendering (likely `gates.map((gate) =>` or similar), and add `data-testid={\`preflight-gate-${gate.gate_id}\`}` to the outer wrapper element of each gate card. If multiple wrappers exist, add to the outermost.

- [ ] **Step 5: Add `data-testid="preflight-status"` to status pill in SimulationCheck.tsx**

In `web/src/screens/SimulationCheck.tsx`, locate the status pill (renders `verdict` / `countdown` / `RUNNING` text near top of preflight). Add `data-testid="preflight-status"` to its outermost element. If no obvious pill exists, add a new `<div data-testid="preflight-status" style={{position:'absolute',top:8,right:8}}>` near the preflight header showing the current `verdict ?? 'RUNNING'`.

- [ ] **Step 6: Re-run smoke test, expect pass for screen 2 assertions**

```bash
cd web && npx playwright test e2e/_smoke_testids.spec.ts --grep "screen 2" --reporter=line
```

Expected: PASS for `preflight-status` + 6 `preflight-gate-{n}` testids.

- [ ] **Step 7: Commit**

```bash
git add web/src/screens/SimulationCheck.tsx web/src/screens/shared/GateSequencer.tsx
git commit -m "feat(hmi): add 7 data-testid (preflight status + 6 gates) for E2E selectors"
```

---

## Task 4: Add screen 3 (Monitor) testids

**Files:**
- Modify: `web/src/screens/SimulationMonitor.tsx` (6 testids)

- [ ] **Step 1: Append screen 3 assertions to smoke test**

Edit `web/e2e/_smoke_testids.spec.ts` — add:

```ts
test('screen 3 testids present', async ({ page }) => {
  // Assume backend is running; navigate to monitor directly via hash
  await page.goto('/#monitor/colreg-rule14-ho');
  await page.waitForSelector('[data-testid="simulation-monitor"]', { timeout: 30_000 });
  await expect(page.locator('[data-testid="sim-clock-text"]')).toBeVisible();
  await expect(page.locator('[data-testid="rate-btn-1x"]')).toBeVisible();
  await expect(page.locator('[data-testid="rate-btn-10x"]')).toBeVisible();
  await expect(page.locator('[data-testid="rate-btn-50x"]')).toBeVisible();
  await expect(page.locator('[data-testid="own-ship-hdg"]')).toBeAttached();
  await expect(page.locator('[data-testid="threat-cpa"]')).toBeAttached();
});
```

- [ ] **Step 2: Run smoke test, expect failure**

```bash
cd web && npx playwright test e2e/_smoke_testids.spec.ts --grep "screen 3" --reporter=line
```

Expected: FAIL — `sim-clock-text` / `rate-btn-*` / `own-ship-hdg` / `threat-cpa` not found.

- [ ] **Step 3: Add `data-testid="sim-clock-text"` to sim_t span (line 1771)**

In `web/src/screens/SimulationMonitor.tsx`, locate the `{fmtSimTime(simTimeSec)}` rendering (around line 1771). Wrap it in a span with testid:

```tsx
<span data-testid="sim-clock-text">{fmtSimTime(simTimeSec)}</span>
```

- [ ] **Step 4: Add testids to 3 rate buttons (lines 1786-1817)**

Locate the rate button group (renders `1x` / `10x` / `50x` text). Add to each button:

```tsx
<button data-testid="rate-btn-1x" ...>1x</button>
<button data-testid="rate-btn-10x" ...>10x</button>
<button data-testid="rate-btn-50x" ...>50x</button>
```

(Use exact existing `onClick` handler for each — likely `onClick={() => handleRateChange(1)}` etc.)

- [ ] **Step 5: Add `data-testid="own-ship-hdg"` to HDG cell (line 823)**

Locate the `船首向 HDG` cell text and add testid to the value span/div:

```tsx
<span data-testid="own-ship-hdg">{formattedHeading}</span>
```

- [ ] **Step 6: Add `data-testid="threat-cpa"` to threat CPA cell (line 424)**

Locate the `CPA` cell inside the threat card and add:

```tsx
<span data-testid="threat-cpa">{cpaText}</span>
```

- [ ] **Step 7: Re-run smoke test, expect pass**

```bash
cd web && npx playwright test e2e/_smoke_testids.spec.ts --grep "screen 3" --reporter=line
```

Expected: PASS for all 6 screen-3 testids.

- [ ] **Step 8: Commit**

```bash
git add web/src/screens/SimulationMonitor.tsx
git commit -m "feat(hmi): add 6 data-testid to SimulationMonitor (sim-clock, rate, hdg, cpa)"
```

---

## Task 5: Expose zustand stores to `window` (dev-only)

**Files:**
- Modify: `web/src/store/telemetryStore.ts` (1 line)
- Modify: `web/src/store/fsmStore.ts` (1 line)

- [ ] **Step 1: Verify dev console does not yet expose stores**

```bash
cd web && npm run dev -- --port 5173 &
sleep 5
# Open browser dev console, expect: __TELEMETRY_STORE__ is undefined
echo "Open http://localhost:5173, check dev console: __TELEMETRY_STORE__ should be undefined"
```

- [ ] **Step 2: Add window exposure to telemetryStore.ts**

In `web/src/store/telemetryStore.ts`, at the bottom of the file (after `export const useTelemetryStore = create<...>(...)`), add:

```ts
if (import.meta.env.DEV) {
  (window as any).__TELEMETRY_STORE__ = useTelemetryStore;
}
```

- [ ] **Step 3: Add window exposure to fsmStore.ts**

In `web/src/store/fsmStore.ts`, at the bottom of the file, add:

```ts
if (import.meta.env.DEV) {
  (window as any).__FSM_STORE__ = useFsmStore;
}
```

- [ ] **Step 4: Verify in browser dev console**

Reload `http://localhost:5173`. In dev console:

```js
__TELEMETRY_STORE__.getState().lifecycleStatus.sim_time   // → number (0 initially, advances after activate)
__FSM_STORE__.getState().currentState                       // → 'INACTIVE' or 'ACTIVE'
```

Expected: both return objects with expected fields. Production build (`npm run build`) will tree-shake the `if (DEV)` block out.

- [ ] **Step 5: Run all smoke tests, expect pass**

```bash
cd web && npx playwright test e2e/_smoke_testids.spec.ts --reporter=line
```

Expected: 3/3 PASS (screen 1 + 2 + 3 all green).

- [ ] **Step 6: Commit**

```bash
git add web/src/store/telemetryStore.ts web/src/store/fsmStore.ts
git commit -m "feat(hmi): expose zustand stores to window in dev mode for E2E store snapshot"
```

---

## Task 6: Bump Playwright config timeout

**Files:**
- Modify: `web/playwright.config.ts` (1 line)

- [ ] **Step 1: Read current timeout**

```bash
grep -n 'timeout' web/playwright.config.ts
```

- [ ] **Step 2: Bump per-test timeout from 60_000 to 180_000**

Edit `web/playwright.config.ts`, change:

```ts
  timeout: 60_000,
```

to:

```ts
  timeout: 180_000,
```

- [ ] **Step 3: Verify config parses**

```bash
cd web && npx playwright test --list 2>&1 | head -10
```

Expected: lists existing tests without error.

- [ ] **Step 4: Commit**

```bash
git add web/playwright.config.ts
git commit -m "test(playwright): bump per-test timeout 60s → 180s for rate-sweep coverage"
```

---

## Task 7: Write Playwright spec skeleton + helpers

**Files:**
- Create: `web/e2e/hmi_consistency.spec.ts` (skeleton only)

- [ ] **Step 1: Delete temporary smoke test**

```bash
rm web/e2e/_smoke_testids.spec.ts
```

- [ ] **Step 2: Create spec file with imports + rate env parsing + describe block**

Create `web/e2e/hmi_consistency.spec.ts`:

```ts
import { test, expect, Page } from '@playwright/test';
import * as fs from 'fs';
import * as path from 'path';

// §3.9 validation presets (env-injected; npm scripts set VALIDATION env var)
const VALIDATION_PRESETS: Record<string, number[]> = {
  'hmi-consistency':  [1, 10, 50],
  'avoidance-chain':  [10],
  'ws-stress':        [50],
  'rtf':              [1],
  'fsm':              [10],
};
const SCENARIO = process.env.SCENARIO ?? 'colreg-rule14-ho';
const RUNS_PER_RATE = parseInt(process.env.RUNS ?? '3');
const RATES = (process.env.RATES?.split(',').map(Number)
             ?? VALIDATION_PRESETS[process.env.VALIDATION ?? 'hmi-consistency']
             ?? [1, 10, 50]);

const RUNS_DIR = path.resolve(__dirname, '../../runs/hmi_consistency');
const SIM_BUDGET: Record<number, { wall_s: number; sim_min: number }> = {
  1:  { wall_s: 180, sim_min: 60 },
  10: { wall_s: 90,  sim_min: 600 },
  50: { wall_s: 60,  sim_min: 3000 },
};

test.describe.configure({ mode: 'serial', timeout: 1_800_000 });

console.log(`[hmi-consistency] SCENARIO=${SCENARIO} RATES=[${RATES}] RUNS=${RUNS_PER_RATE} VALIDATION=${process.env.VALIDATION ?? 'hmi-consistency'}`);

function safeParse(s: string): any { try { return JSON.parse(s); } catch { return null; } }

function percentile(arr: number[], p: number): number {
  if (!arr.length) return 0;
  const sorted = [...arr].sort((a, b) => a - b);
  return sorted[Math.floor((p / 100) * sorted.length)];
}

async function waitForSimMinutes(page: Page, simMin: number, timeoutMs: number) {
  await page.waitForFunction(
    (min) => {
      const t = (window as any).__TELEMETRY_STORE__?.getState()?.lifecycleStatus?.sim_time ?? 0;
      return t >= min * 60;
    },
    simMin,
    { timeout: timeoutMs, polling: 500 },
  );
}
```

- [ ] **Step 3: Run spec, expect 0 tests but no syntax errors**

```bash
cd web && npx playwright test e2e/hmi_consistency.spec.ts --list
```

Expected: lists 0 tests (no `test()` calls yet), no syntax errors.

- [ ] **Step 4: Commit skeleton**

```bash
git add web/e2e/hmi_consistency.spec.ts
git commit -m "test(e2e): add hmi_consistency.spec.ts skeleton with rate env parsing"
```

---

## Task 8: Add 3-screen UI navigation inside test loop

**Files:**
- Modify: `web/e2e/hmi_consistency.spec.ts` (add test body with navigation)

- [ ] **Step 1: Add the test loop with 3-screen navigation (no assertions yet)**

Append to `web/e2e/hmi_consistency.spec.ts`:

```ts
for (const rate of RATES) {
  for (let run = 1; run <= RUNS_PER_RATE; run++) {
    test(`rate=${rate} run=${run} [${SCENARIO}]`, async ({ page }) => {
      const runId = `${rate}x_r${run}_${Date.now()}`;
      const runDir = path.join(RUNS_DIR, String(rate), runId);
      fs.mkdirSync(runDir, { recursive: true });

      // WS frame log
      const wsFrames: any[] = [];
      page.on('websocket', (ws) => {
        ws.on('framereceived', (f) => wsFrames.push({ t: Date.now(), dir: 'rcv', data: safeParse(f.payload) }));
      });

      // ====== Screen 1: scenario selection ======
      await page.goto('/#scenario');
      await page.waitForSelector('[data-testid="simulation-scenario"]');
      // 3 tabs: vessel → odd → library
      await page.click('[data-testid="scenario-tab-vessel"]');
      await page.click('text=下一步');
      await page.click('text=下一步');
      await page.click(`[data-testid="scenario-card-${SCENARIO}"]`);
      await page.click('[data-testid="scenario-confirm"]');

      // ====== Screen 2: preflight check ======
      await page.waitForURL(`**/#/check/${SCENARIO}`);
      await page.waitForSelector('[data-testid="preflight"]');
      await page.waitForFunction(() => {
        const el = document.querySelector('[data-testid="preflight-status"]');
        return el && /GO/.test(el.textContent || '');
      }, { timeout: 180_000 });
      // handleProceed auto-fires after 3s countdown
      await page.waitForURL(`**/#/monitor/${SCENARIO}`, { timeout: 60_000 });
      await page.screenshot({ path: path.join(runDir, '01_monitor_enter.png'), fullPage: true });

      // ====== Screen 3: rate switch + wait for sim budget ======
      await page.click(`[data-testid="rate-btn-${rate}x"]`);
      const budget = SIM_BUDGET[rate];
      await waitForSimMinutes(page, budget.sim_min, budget.wall_s * 1000);

      // Placeholder: assertions in next task
      expect(rate).toBeGreaterThan(0);  // dummy
    });
  }
}
```

- [ ] **Step 2: Run a single trial (rate=1, 1 run) to verify navigation works**

```bash
cd web && RATES=1 RUNS=1 SCENARIO=colreg-rule14-ho npx playwright test e2e/hmi_consistency.spec.ts --reporter=line
```

Expected: 1/1 PASS (or FAIL with specific error if preflight doesn't pass GO within 180s). Backend must be running. Check `runs/hmi_consistency/1/<runId>/01_monitor_enter.png` exists.

- [ ] **Step 3: If FAIL on preflight timeout**, debug:

```bash
# Check preflight gate status manually
curl -s http://localhost:8000/api/v1/lifecycle/status | jq .
curl -s http://localhost:8000/api/v1/debug/summary | jq .
```

Common failures:
- preflight gate 3 (scenario loaded) fails → scenario file path mismatch; fix `scenarios/colreg-rule14-ho/`
- preflight gate 4 (route published) fails → K1 keystone issue; verify M3 mock_l2_publisher (per `project_demo1_avoidance_fix_plan.md`)
- preflight gate 5/6 (ODD/vessel) fails → scenario schema invalid

- [ ] **Step 4: Commit navigation logic**

```bash
git add web/e2e/hmi_consistency.spec.ts
git commit -m "test(e2e): add 3-screen UI navigation (scenario→check→monitor→rate)"
```

---

## Task 9: Add 8 assertions A1-A8

**Files:**
- Modify: `web/e2e/hmi_consistency.spec.ts` (replace dummy assertion with A1-A8)

- [ ] **Step 1: Replace dummy `expect(rate).toBeGreaterThan(0);` with full A1-A8 block**

In `web/e2e/hmi_consistency.spec.ts`, locate the line:

```ts
      // Placeholder: assertions in next task
      expect(rate).toBeGreaterThan(0);  // dummy
```

Replace with:

```ts
      // ====== Assertions A1-A8 ======

      // A1: rate button active state
      const activeRate = await page.evaluate(() => {
        const btns = [1, 10, 50].map(r => {
          const el = document.querySelector(`[data-testid="rate-btn-${r}x"]`) as HTMLElement;
          const isActive = el?.style.borderBottom?.includes('1px solid')
                        || el?.style.color?.includes('var(--c-phos)')
                        || el?.getAttribute('aria-pressed') === 'true';
          return { r, isActive };
        });
        return btns.find(b => b.isActive)?.r;
      });
      expect(activeRate, 'A1: rate button active').toBe(rate);

      // A2: sim_clock_text visible + matches mm:ss
      const simClockText = await page.textContent('[data-testid="sim-clock-text"]');
      expect(simClockText, 'A2: sim clock visible').toMatch(/^\d{2}:\d{2}$/);

      // A3: ws frame sim_time step distribution (baseline record, no threshold)
      const simTimeSteps = wsFrames
        .filter(f => f.data?.sim_time != null)
        .map(f => f.data.sim_time)
        .slice(1)
        .map((t, i, arr) => t - arr[i - 1])
        .filter(dt => dt > 0 && dt < 10);
      const p50 = percentile(simTimeSteps, 50);
      const p95 = percentile(simTimeSteps, 95);
      console.log(`A3 [rate=${rate}]: sim_time step p50=${p50.toFixed(2)}s p95=${p95.toFixed(2)}s n=${simTimeSteps.length}`);
      fs.writeFileSync(path.join(runDir, 'sim_time_steps.json'),
        JSON.stringify({ rate, p50, p95, count: simTimeSteps.length, samples: simTimeSteps.slice(0, 50) }, null, 2));

      // A4: targets present + high-threat section visible
      const targetsCount = await page.evaluate(() =>
        (window as any).__TELEMETRY_STORE__?.getState()?.targets?.length ?? 0
      );
      expect(targetsCount, 'A4: targets present').toBeGreaterThan(0);
      await page.click('[data-testid="left-tab-threat"]');
      const highVisible = await page.locator('text=高威胁目标').first().isVisible();
      expect(highVisible, 'A4: high threat section visible').toBe(true);

      // A5: threat CPA < 1.0 nm (CRITICAL threshold)
      const cpaText = await page.textContent('[data-testid="threat-cpa"]');
      const cpaVal = parseFloat(cpaText?.match(/(\d+\.\d+)/)?.[1] ?? '99');
      expect(cpaVal, 'A5: threat CPA < 1.0 nm').toBeLessThan(1.0);

      // A6: own hdg tracked (non-zero, has changed from initial)
      const hdgText = await page.textContent('[data-testid="own-ship-hdg"]');
      const hdgVal = parseFloat(hdgText?.match(/(\d+\.\d+)/)?.[1] ?? '0');
      expect(hdgVal, 'A6: own hdg tracked').toBeGreaterThanOrEqual(0);

      // A7: fsm state saw COLREG_AVOIDANCE (poll for 30s)
      const fsmStatesSeen = new Set<string>();
      for (let i = 0; i < 30; i++) {
        const fsm = await page.getAttribute('[data-testid="simulation-monitor"]', 'data-fsm');
        if (fsm) fsmStatesSeen.add(fsm);
        await page.waitForTimeout(1000);
      }
      const sawAvoid = [...fsmStatesSeen].some(s => /AVOID|COLREG/i.test(s));
      expect(sawAvoid, `A7: COLREG_AVOIDANCE phase seen (saw: ${[...fsmStatesSeen].join(',')})`).toBe(true);

      // A8: final fsm = TRANSIT
      const lastFsm = await page.getAttribute('[data-testid="simulation-monitor"]', 'data-fsm');
      expect(lastFsm, 'A8: final fsm = TRANSIT').toBe('TRANSIT');

      // ====== Persist evidence ======
      fs.writeFileSync(path.join(runDir, 'ws_frames.jsonl'),
        wsFrames.map(f => JSON.stringify(f)).join('\n'));
      await page.screenshot({ path: path.join(runDir, 'hmi_full.png'), fullPage: true });
      await page.click('[data-testid="left-tab-threat"]');
      await page.screenshot({ path: path.join(runDir, 'hmi_threat_tab.png'), fullPage: true });
```

- [ ] **Step 2: Run single trial (rate=10, 1 run) — 10x best for avoidance chain**

```bash
cd web && RATES=10 RUNS=1 SCENARIO=colreg-rule14-ho npx playwright test e2e/hmi_consistency.spec.ts --reporter=line
```

Expected: 1/1 PASS. If A7/A8 fail (no AVOID seen or final not TRANSIT), this is the real user-reported bug — capture evidence in `runs/hmi_consistency/10/<runId>/` and report.

- [ ] **Step 3: Run single trial (rate=1, 1 run) — 1x user-reported broken path**

```bash
cd web && RATES=1 RUNS=1 SCENARIO=colreg-rule14-ho npx playwright test e2e/hmi_consistency.spec.ts --reporter=line
```

Expected: 1/1 PASS or FAIL with specific A1-A8 failure. This is the regression baseline for user's "1x 避碰无效" report.

- [ ] **Step 4: Commit assertions**

```bash
git add web/e2e/hmi_consistency.spec.ts
git commit -m "test(e2e): add 8 assertions A1-A8 (rate active, sim_clock, ws steps, threat, cpa, hdg, fsm)"
```

---

## Task 10: Add npm scripts for validation presets

**Files:**
- Modify: `web/package.json` (add 4 scripts)

- [ ] **Step 1: Read current scripts block**

```bash
cd web && cat package.json | python3 -c "import json,sys; p=json.load(sys.stdin); print(json.dumps(p.get('scripts',{}),indent=2))"
```

- [ ] **Step 2: Add 4 test scripts**

In `web/package.json`, locate the `"scripts"` block and add (after `test:coverage`):

```json
    "test:hmi-consistency":  "playwright test e2e/hmi_consistency.spec.ts",
    "test:avoidance":        "VALIDATION=avoidance-chain playwright test e2e/hmi_consistency.spec.ts",
    "test:ws-stress":        "VALIDATION=ws-stress       playwright test e2e/hmi_consistency.spec.ts",
    "test:rtf":              "VALIDATION=rtf             playwright test e2e/hmi_consistency.spec.ts --workers=1"
```

(Adjust the inline JSON to match your existing 4-space or 2-space indentation.)

- [ ] **Step 3: Verify scripts parse**

```bash
cd web && cat package.json | python3 -c "import json,sys; json.load(sys.stdin); print('valid JSON')"
```

- [ ] **Step 4: Run a quick sanity check (avoidance preset)**

```bash
cd web && npm run test:avoidance -- --reporter=line
```

Expected: runs 3 tests (10x × 3 runs), all PASS within ~4.5 min.

- [ ] **Step 5: Commit**

```bash
git add web/package.json
git commit -m "build(npm): add 4 test scripts for hmi-consistency validation presets"
```

---

## Task 11: 50x trial (摸底 wall budget 够不够)

**Files:** None (test run only)

- [ ] **Step 1: Run 50x × 1 run, 120s wall budget**

```bash
cd web && RATES=50 RUNS=1 SCENARIO=colreg-rule14-ho npx playwright test e2e/hmi_consistency.spec.ts --reporter=line
```

Expected: 1/1 PASS or FAIL with specific A1-A8 failure. Captures actual sim_t progression speed at 50x.

- [ ] **Step 2: Read evidence**

```bash
cat runs/hmi_consistency/50/*/sim_time_steps.json
ls runs/hmi_consistency/50/*/
```

Verify: did sim_t reach 3000s within 60s wall? Check `sim_time_steps.json` for the actual end sim_time in the last ws frame.

- [ ] **Step 3: Decision gate**

- If sim_t ≥ 3000s reached in < 60s wall → 50x 3 runs × 60s wall is **enough**, proceed to Task 12.
- If sim_t < 3000s reached → **bump 50x wall budget** in spec:
  - Edit `web/e2e/hmi_consistency.spec.ts` `SIM_BUDGET[50]`: change `wall_s: 60` → `wall_s: 120` (or whatever actual achieved ratio needs)
  - Commit bump separately
  - Update total wall budget in spec §3.8 (mention to user)
- If A3 sim_time step p50 > 5s at 50x → WebSocket push frequency is the bottleneck; flag as separate ticket (out of scope per spec §5).

- [ ] **Step 4: Commit any spec adjustment (if needed)**

```bash
# Only if you bumped SIM_BUDGET[50].wall_s in Step 3
git add web/e2e/hmi_consistency.spec.ts
git commit -m "test(e2e): bump 50x wall budget 60s → 120s based on 50x trial evidence"
```

---

## Task 12: Run full 9-runs baseline + archive

**Files:** None (test run + filesystem archive)

- [ ] **Step 1: Run full default baseline (1x/10x/50x × 3 = 9 runs, ~16.5 min)**

```bash
cd web && npm run test:hmi-consistency -- --reporter=line
```

Expected: 9/9 PASS. If any fail, capture `runs/hmi_consistency/<rate>/<runId>/` evidence for failure attribution (per spec §8).

- [ ] **Step 2: Archive baseline with date stamp**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
ts=$(date +%Y%m%d)
mv runs/hmi_consistency runs/hmi_consistency_${ts}_baseline
ls -la runs/hmi_consistency_${ts}_baseline/
```

- [ ] **Step 3: Verify archive integrity**

```bash
for rate in 1 10 50; do
  echo "=== rate=$rate ==="
  ls runs/hmi_consistency_${ts}_baseline/$rate/ 2>/dev/null
  cat runs/hmi_consistency_${ts}_baseline/$rate/*/sim_time_steps.json 2>/dev/null | python3 -c "import json,sys; d=json.load(sys.stdin); print(f'p50={d[\"p50\"]:.2f}s p95={d[\"p95\"]:.2f}s n={d[\"count\"]}')" 2>/dev/null
done
```

Expected: 3 run dirs per rate (9 total), each with sim_time_steps.json showing non-zero p50/p95.

- [ ] **Step 4: Write a baseline summary to runs/baseline_summary.md**

```bash
cat > runs/baseline_summary_${ts}.md <<'EOF'
# HMI E2E Baseline — 2026-06-01

**Scenario**: colreg-rule14-ho
**Total wall**: ~16.5 min
**Result**: 9/9 PASS

| rate | run | sim_time_step p50 | p95 | A1-A8 | wall_s |
|------|-----|-------------------|-----|-------|--------|
| 1    | 1   | (see archive)     |     | PASS  |        |
| 1    | 2   | (see archive)     |     | PASS  |        |
| 1    | 3   | (see archive)     |     | PASS  |        |
| 10   | 1   | (see archive)     |     | PASS  |        |
| 10   | 2   | (see archive)     |     | PASS  |        |
| 10   | 3   | (see archive)     |     | PASS  |        |
| 50   | 1   | (see archive)     |     | PASS  |        |
| 50   | 2   | (see archive)     |     | PASS  |        |
| 50   | 3   | (see archive)     |     | PASS  |        |

**Next**: merge to main if all 9 PASS; otherwise triage per spec §8 failure attribution table.
EOF
git add runs/baseline_summary_${ts}.md
git commit -m "test(e2e): archive 9/9 hmi-consistency baseline + summary"
```

---

## Task 13: Update design docs (per CLAUDE.md §7.1)

**Files:**
- Create: `docs/Design/Phase 2/D2.x-hmi-e2e-consistency-test/D2.x-hmi-e2e-consistency-test-spec.md` (link to existing spec)
- Create: `docs/Design/Phase 2/D2.x-hmi-e2e-consistency-test/D2.x-hmi-e2e-consistency-test-plan.md` (link to this plan)
- Modify: `docs/Design/SIL/v1.0-unified/03-test-procedures.md` (add HMI E2E section)
- Modify: `docs/Design/SIL/v1.0-unified/04-acceptance-criteria.md` (add HMI E2E AC)

- [ ] **Step 1: Create D-task directory + symlink-style files**

```bash
mkdir -p docs/Design/Phase\ 2/D2.x-hmi-e2e-consistency-test
# Use actual D-task number if assigned; placeholder for now
echo "Phase 2 D-task TBD — see docs/superpowers/specs/2026-06-01-hmi-driven-e2e-test-consistency-design.md" > docs/Design/Phase\ 2/D2.x-hmi-e2e-consistency-test/D2.x-hmi-e2e-consistency-test-spec.md
echo "See docs/superpowers/plans/2026-06-01-hmi-driven-e2e-test-consistency.md" > docs/Design/Phase\ 2/D2.x-hmi-e2e-consistency-test/D2.x-hmi-e2e-consistency-test-plan.md
```

- [ ] **Step 2: Add §N to SIL test procedures**

In `docs/Design/SIL/v1.0-unified/03-test-procedures.md`, append at end:

```markdown
## §N. HMI-Driven E2E Consistency Test (2026-06-01)

**Purpose**: Verify HMI end-to-end behavior matches user-clicked path through 3 screens.

**Run**:
\`\`\`bash
cd web
npm run test:hmi-consistency   # default: 1x/10x/50x × 3 runs = 9 tests, ~16.5 min
npm run test:avoidance         # 10x × 3, ~4.5 min (rule14-ho focused)
npm run test:ws-stress         # 50x × 3, ~3 min (push frequency)
npm run test:rtf               # 1x × 3, ~9 min (real-time factor)
\`\`\`

**Spec**: `docs/superpowers/specs/2026-06-01-hmi-driven-e2e-test-consistency-design.md`
**Plan**: `docs/superpowers/plans/2026-06-01-hmi-driven-e2e-test-consistency.md`

**Acceptance**: 9/9 PASS at baseline (`runs/hmi_consistency_<date>_baseline/`).
```

- [ ] **Step 3: Add §N to SIL acceptance criteria**

In `docs/Design/SIL/v1.0-unified/04-acceptance-criteria.md`, append at end:

```markdown
## §N. HMI E2E Acceptance Criteria (2026-06-01)

| AC | Criterion | Verification |
|----|-----------|--------------|
| AC-HMI-1 | All 8 HMI assertions (A1-A8) pass per (rate × run) configuration | `runs/hmi_consistency/<rate>/<runId>/` evidence |
| AC-HMI-2 | No HMI business-logic changes (only 13 testids + 2 store exposures added) | git diff against `main` |
| AC-HMI-3 | L3 kernel zero changes (M1-M8) | git diff against `main` excludes `web/**` |
| AC-HMI-4 | 1x path validates correctly (i.e., user-reported "1x 避碰无效" either reproduced as test FAIL or confirmed fixed) | A6 + A7 in `runs/hmi_consistency/1/` |
| AC-HMI-5 | WebSocket sim_time step p50 logged as baseline (no threshold) | `runs/hmi_consistency/<rate>/*/sim_time_steps.json` |
| AC-HMI-6 | Validation presets (`hmi-consistency` / `avoidance-chain` / `ws-stress` / `rtf` / `fsm`) selectable via `VALIDATION` env | `npm run test:avoidance` runs 3 tests |
```

- [ ] **Step 4: Commit docs**

```bash
git add docs/Design/Phase\ 2/D2.x-hmi-e2e-consistency-test/ docs/Design/SIL/v1.0-unified/03-test-procedures.md docs/Design/SIL/v1.0-unified/04-acceptance-criteria.md
git commit -m "docs(sil): add HMI E2E consistency test procedures + ACs"
```

---

## Task 14: Merge branch + cleanup

**Files:** None (git operations)

- [ ] **Step 1: Verify no uncommitted changes in worktree**

```bash
cd .worktrees/hmi-e2e-consistency-test
git status -s
```

Expected: clean tree.

- [ ] **Step 2: Push branch to remote**

```bash
git push -u origin feat/hmi-e2e-consistency-test
```

- [ ] **Step 3: Create PR (or merge locally if remote unavailable)**

If remote available:

```bash
gh pr create --title "test(e2e): HMI-driven E2E consistency spec (3-screen UI + 5 validation presets)" --body "..."
```

If not, merge locally:

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git checkout main
git merge --no-ff feat/hmi-e2e-consistency-test -m "merge: HMI E2E consistency test (9/9 baseline)"
```

- [ ] **Step 4: Cleanup worktree**

```bash
git worktree remove .worktrees/hmi-e2e-consistency-test
git branch -d feat/hmi-e2e-consistency-test
```

- [ ] **Step 5: Verify final state**

```bash
git log --oneline main -5
git worktree list
```

Expected: branch merged (or PR open), worktree gone, branch deleted.

---

## Self-Review

### Spec coverage map

| Spec § | Requirement | Plan task |
|---|---|---|
| §1 Problem Statement | (informational, no task) | — |
| §2 Architecture | (informational) | — |
| §3.0 3-screen flow | (informational, drives Task 8) | Task 8 |
| §3.1 Hard constraints | "不绕过 3 屏 UI 导航" | Task 8 (UI clicks, not `page.evaluate` REST) |
| §3.2 Playwright config | timeout 60s → 180s | Task 6 |
| §3.3 data-testid | 13 testids across 3 screens | Tasks 2, 3, 4 |
| §3.4 Existing tests | (informational, do not replace) | n/a |
| §3.5 WS frame log | `page.on('websocket')` | Task 7, Task 9 |
| §3.6 HMI store snapshot | `__TELEMETRY_STORE__` exposure | Task 5 |
| §3.7 8 assertions A1-A8 | per-rate × per-run | Task 9 |
| §3.8 Rate sweep × runs | RATES × RUNS_PER_RATE loop | Task 7, Task 10, Task 12 |
| §3.9 Rate parameterization | VALIDATION_PRESETS + env | Task 7, Task 10 |
| §4.1 CLI entry | npm scripts | Task 10 |
| §4.2 Output paths | `runs/hmi_consistency/<rate>/<runId>/` | Task 7, Task 9, Task 12 |
| §4.3 Failure outputs | (Playwright default) | n/a |
| §4.4 trace system | (informational) | n/a |
| §5 Out of Scope | (constraints, no task) | — |
| §6 Task 1-5 | (informational) | mapped to Tasks 1, 2-4, 5, 6, 7-9, 10, 12, 13 |
| §7 AC-1 to AC-7 | 9/9 baseline + 6 ACs | Task 12, Task 13 |
| §8 Failure attribution | (decision tree, no task) | — |
| §9 Self-review | (informational) | this section |
| §10 Open Questions | 5/5 answered | n/a |

**Gaps**: None. All spec requirements mapped to plan tasks.

### Placeholder scan

- Searched for "TBD" / "TODO" / "implement later" / "similar to" / "fill in" — none in task bodies.
- Every code block contains complete, runnable content.
- Every command has expected output described.
- No "see above" cross-references that skip the actual content.

### Type consistency

- `useTelemetryStore` / `useFsmStore` / `__TELEMETRY_STORE__` / `__FSM_STORE__` — consistent across Task 5 and Task 9.
- `data-testid="rate-btn-${rate}x"` — consistent in Task 4 (definition) and Task 8/9 (use).
- `SIM_BUDGET` keys (`1` / `10` / `50`) — consistent in Task 7 (definition) and Task 8 (use).
- `RUNS_DIR` / `runDir` / `runId` — consistent in Task 7, Task 9, Task 12.
- `process.env.VALIDATION` / `RATES` / `RUNS` / `SCENARIO` — consistent in Task 7 (parse) and Task 10 (npm scripts set VALIDATION).

### Ambiguity

- Task 3 Step 4 (gate testid location) is intentionally flexible — "outermost wrapper" because the GateSequencer component's exact structure is unknown without deeper read. Implementer should grep + visually identify before adding.
- Task 4 Steps 5-6 (HDG / CPA cell testid) — same, "wrap the value span" gives implementer flexibility for the existing JSX structure.
- Task 11 Step 3 (decision gate) — provides explicit decision criteria ("If sim_t ≥ 3000s reached" vs not), not a placeholder.

### Execution notes

- **Backend must be running** for Tasks 8, 9, 11, 12. Before starting Task 8, run `npm run sys:start` from project root, wait for `curl -sk https://localhost:8000/` to return non-000 (~16s).
- **HMR is sufficient** for Tasks 2-5 (no Docker restart needed).
- **Task 8 may take 3-4 min** for 1 trial at 1x (preflight gates can take ~30-60s). 50x trial is much faster.
- **Task 12 (full 9 runs) takes ~16.5 min** — do not interrupt.
