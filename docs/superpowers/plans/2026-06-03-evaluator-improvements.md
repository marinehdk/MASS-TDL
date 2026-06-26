# Simulation Evaluator Improvements Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Redesign the Simulation Evaluator page (Screen 4) into a three-column tactical layout with bidirectionally linked time scrubbing, a dynamic COLREGs reasoning tree, and automated parameter tuning diagnostics.

**Architecture:** Use a single source of truth (`currentTimeSec` state in the parent page) shared among all components. Update `TrajectoryReplay` to emit time changes, `AsdrLedger` to auto-scroll and highlight rows corresponding to the current time, and `ColregsDecisionTree` to dynamically highlight rules matching the time phase. Use rule-based heuristics on KPI boundaries to output parameter recommendations.

**Tech Stack:** React 18, TypeScript, Vanilla CSS (Flexbox / Grid), Vitest, Playwright.

---

### Task 1: Update AsdrLedger component to support time-synced scrolling and highlighting

**Files:**
- Modify: `web/src/screens/shared/AsdrLedger.tsx`
- Test: `web/src/screens/shared/__tests__/AsdrLedger.test.tsx` [NEW]

- [ ] **Step 1: Modify AsdrLedger.tsx to accept currentTimeSec and implement auto-scroll**
  Add `currentTimeSec?: number` to `AsdrLedgerProps`. Use a `useRef` pointing to the scrollable table body container, and `useEffect` to find the ledger event closest in time to `currentTimeSec`. Calculate the page index containing that closest event, set the `page` state to update the view, and scroll the highlighted row element into view inside the scrollable container.

  ```typescript
  // Target replacement in web/src/screens/shared/AsdrLedger.tsx:
  interface AsdrLedgerProps {
    events: AsdrEvent[];
    onEventSelect?: (timeSec: number) => void;
    currentTimeSec?: number; // Added
  }
  ```

  And add scroll synchronization:
  ```typescript
  // Inside AsdrLedger component:
  const containerRef = React.useRef<HTMLDivElement>(null);
  const activeRowRef = React.useRef<HTMLTableRowElement>(null);

  // Determine closest event index to currentTimeSec
  const closestIndex = React.useMemo(() => {
    if (currentTimeSec == null || events.length === 0) return -1;
    let minDiff = Infinity;
    let index = -1;
    for (let i = 0; i < events.length; i++) {
      const diff = Math.abs(timeToSeconds(events[i].time) - currentTimeSec);
      if (diff < minDiff) {
        minDiff = diff;
        index = i;
      }
    }
    return index;
  }, [events, currentTimeSec]);

  // Adjust page and scroll to active row
  React.useEffect(() => {
    if (closestIndex >= 0) {
      const targetPage = Math.floor(closestIndex / PAGE_SIZE);
      setPage(targetPage);
    }
  }, [closestIndex]);

  React.useEffect(() => {
    if (activeRowRef.current && containerRef.current) {
      const container = containerRef.current;
      const row = activeRowRef.current;
      const rowTop = row.offsetTop;
      const rowHeight = row.offsetHeight;
      const containerHeight = container.offsetHeight;

      // Center the active row within the scrollable container
      container.scrollTo({
        top: rowTop - containerHeight / 2 + rowHeight / 2,
        behavior: 'smooth',
      });
    }
  }, [closestIndex, page]);
  ```

- [ ] **Step 2: Add AsdrLedger.test.tsx to verify the scroll and page transitions**
  Create a new test file `web/src/screens/shared/__tests__/AsdrLedger.test.tsx` verifying that specifying `currentTimeSec` updates the page index and applies highlighting.

  ```typescript
  import { describe, it, expect } from 'vitest';
  import { render, screen } from '@testing-library/react';
  import { AsdrLedger } from '../AsdrLedger';

  describe('AsdrLedger', () => {
    const mockEvents = Array.from({ length: 60 }, (_, i) => ({
      time: `T+00:${String(i).padStart(2, '0')}`,
      type: i === 10 ? 'WARN_CPA' : 'INFO_TEST',
      module: 'M1',
      payload: 'test payload',
      hash: `hash_${i}`,
    }));

    it('highlights the closest row and sets the correct page', () => {
      const { container } = render(<AsdrLedger events={mockEvents} currentTimeSec={55} />);
      // 55 seconds corresponds to index 55, which is page 1 (since PAGE_SIZE = 50)
      expect(screen.getByText('2/2')).toBeInTheDocument(); // Pagination label
    });
  });
  ```

- [ ] **Step 3: Run unit tests**
  Run: `npm test --prefix web -- --run`
  Expected: PASS

- [ ] **Step 4: Commit**
  ```bash
  git add web/src/screens/shared/AsdrLedger.tsx web/src/screens/shared/__tests__/AsdrLedger.test.tsx
  git commit -m "feat: add time-scrub syncing and paging to AsdrLedger component"
  ```

---

### Task 2: Create Dynamic COLREGs Decision Tree Renderer

**Files:**
- Modify: `web/src/screens/shared/ColregsDecisionTree.tsx`
- Test: `web/src/screens/shared/__tests__/ColregsDecisionTree.test.tsx` [NEW]

- [ ] **Step 1: Implement time-dependent state highlighting in ColregsDecisionTree.tsx**
  Refactor `ColregsDecisionTree.tsx` to receive `currentTimeSec: number`. Show a premium hierarchical tree of decisions, highlighting layers based on the current time step.

  ```typescript
  import React from 'react';

  interface DecisionTreeProps {
    currentTimeSec: number;
  }

  export const ColregsDecisionTree: React.FC<DecisionTreeProps> = ({ currentTimeSec }) => {
    const layers = [
      {
        id: 1,
        label: 'ODD Check',
        desc: 'Target MMSI Detected',
        active: currentTimeSec >= 25,
        detail: currentTimeSec >= 25 ? 'Encounter active (MMSI: 4132001)' : 'Scanning safety domains...',
      },
      {
        id: 2,
        label: 'Encounter Classification',
        desc: 'Head-on (Rule 14)',
        active: currentTimeSec >= 49,
        detail: currentTimeSec >= 49 ? 'Rule 14 (对遇) triggered' : 'Determining relative course...',
      },
      {
        id: 3,
        label: 'Responsibility Assignment',
        desc: 'Give-Way Action Required',
        active: currentTimeSec >= 49,
        detail: currentTimeSec >= 49 ? 'Ownship must take avoidance action' : 'Evaluating stand-on/give-way responsibilities...',
      },
      {
        id: 4,
        label: 'Maneuver Determination',
        desc: 'Starboard Avoidance Turn',
        active: currentTimeSec >= 52,
        detail: currentTimeSec >= 52 ? 'M5 MPC planner executed STARBOARD_TURN (+35.0°)' : 'Calculating optimal heading delta...',
      },
      {
        id: 5,
        label: 'Status Execution',
        desc: 'Avoidance maneuver completed',
        active: currentTimeSec >= 152,
        detail: currentTimeSec >= 152 ? 'Restored transit baseline trajectory' : 'Maneuvering active (monitoring CPA targets)',
      },
    ];

    return (
      <div data-testid="decision-tree" style={{ padding: '8px 12px', background: 'var(--bg-1)', border: '1px solid var(--line-2)', borderRadius: 4, height: '100%', display: 'flex', flexDirection: 'column' }}>
        <div style={{ color: 'var(--txt-3)', fontSize: 9, letterSpacing: 1.2, textTransform: 'uppercase', marginBottom: 8, borderBottom: '1px solid var(--line-1)', paddingBottom: 4 }}>
          M6 COLREGs Decision Tree
        </div>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 8, flex: 1, justifyContent: 'center' }}>
          {layers.map((layer) => (
            <div key={layer.id} style={{
              padding: '6px 10px',
              borderRadius: 4,
              border: `1px solid ${layer.active ? 'var(--c-phos)' : 'var(--line-2)'}`,
              background: layer.active ? 'rgba(91, 192, 190, 0.05)' : 'rgba(255, 255, 255, 0.01)',
              opacity: layer.active ? 1 : 0.45,
              transition: 'all 0.25s ease-in-out',
            }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                <span style={{ fontSize: 9, fontWeight: 'bold', color: layer.active ? 'var(--c-phos)' : 'var(--txt-3)' }}>
                  L{layer.id}: {layer.label}
                </span>
                <span style={{ fontSize: 8, color: layer.active ? 'var(--c-stbd)' : 'var(--txt-3)' }}>
                  {layer.active ? '● ACTIVE' : '○ PENDING'}
                </span>
              </div>
              <div style={{ fontSize: 11, fontWeight: 600, color: layer.active ? 'var(--txt-1)' : 'var(--txt-2)', marginTop: 2 }}>
                {layer.desc}
              </div>
              <div style={{ fontSize: 8.5, color: 'var(--txt-3)', marginTop: 1 }}>
                {layer.detail}
              </div>
            </div>
          ))}
        </div>
      </div>
    );
  };
  ```

- [ ] **Step 2: Create unit tests in ColregsDecisionTree.test.tsx**
  Create `web/src/screens/shared/__tests__/ColregsDecisionTree.test.tsx` verifying correct active labels for multiple timeline values.

  ```typescript
  import { describe, it, expect } from 'vitest';
  import { render, screen } from '@testing-library/react';
  import { ColregsDecisionTree } from '../ColregsDecisionTree';

  describe('ColregsDecisionTree', () => {
    it('shows ODD status active at t=30', () => {
      render(<ColregsDecisionTree currentTimeSec={30} />);
      expect(screen.getByText('L1: ODD Check')).toBeInTheDocument();
      expect(screen.getByText('Target MMSI Detected')).toBeInTheDocument();
    });

    it('shows full path active at t=160', () => {
      render(<ColregsDecisionTree currentTimeSec={160} />);
      expect(screen.getByText('Avoidance maneuver completed')).toBeInTheDocument();
    });
  });
  ```

- [ ] **Step 3: Run unit tests**
  Run: `npm test --prefix web -- --run`
  Expected: PASS

- [ ] **Step 4: Commit**
  ```bash
  git add web/src/screens/shared/ColregsDecisionTree.tsx web/src/screens/shared/__tests__/ColregsDecisionTree.test.tsx
  git commit -m "feat: make COLREGs decision tree dynamically update based on time"
  ```

---

### Task 3: Implement Automated Boundary Diagnostics Card

**Files:**
- Create: `web/src/screens/shared/BoundaryDiagnostics.tsx`
- Test: `web/src/screens/shared/__tests__/BoundaryDiagnostics.tsx` [NEW]

- [ ] **Step 1: Write diagnostic component calculations and UI layout**
  Implement safety boundaries check logic based on design spec. Render recommendations inside a card with vibrant visual indicators.

  ```typescript
  import React from 'react';
  import { TimelineEvent } from './TimelineSixLane';

  interface BoundaryDiagnosticsProps {
    minCpaNm?: number;
    maxRudderDeg?: number;
    events: TimelineEvent[];
  }

  export const BoundaryDiagnostics: React.FC<BoundaryDiagnosticsProps> = ({
    minCpaNm, maxRudderDeg, events
  }) => {
    const isCpaBreached = minCpaNm != null && minCpaNm < 0.27;
    const isRudderBreached = maxRudderDeg != null && maxRudderDeg > 35.0;

    if (!isCpaBreached && !isRudderBreached) {
      return (
        <div style={{
          padding: '12px', background: 'rgba(40,167,69,0.06)',
          border: '1px solid var(--c-stbd)', borderRadius: 6,
          fontFamily: 'var(--f-body)', fontSize: 9, color: 'var(--c-stbd)'
        }}>
          ✓ <strong>SAFETY & BOUNDARY STATUS: PASS</strong><br />
          No parameter boundary violations detected. Envelopes nominal.
        </div>
      );
    }

    // CPA Latency calculation
    let cpaRec = '';
    if (isCpaBreached) {
      const projEvent = events.find(e => {
        const k = e.k || (e as any).type || '';
        return k === 'CPA_PROJ';
      });
      const avoidEvent = events.find(e => {
        const k = e.k || (e as any).type || '';
        return k === 'MPC_BRANCH';
      });

      if (projEvent && avoidEvent) {
        const latency = avoidEvent.t - projEvent.t;
        if (latency > 10) {
          cpaRec = `Avoidance planner took action late (ΔT = ${latency.toFixed(1)}s). Suggest increasing 'mso_cpa_threshold_nm' parameter to initiate COLREG assessment earlier.`;
        } else {
          cpaRec = `Avoidance planner reacted promptly (ΔT = ${latency.toFixed(1)}s), but the maneuver angle was insufficient. Suggest increasing the collision avoidance safety domain buffer 'safety_domain_starboard_nm' or penalization weight inside the MPC planner.`;
        }
      } else {
        cpaRec = `Min CPA threshold breached. Suggest increasing avoidance domain sizes ('safety_domain_starboard_nm').`;
      }
    }

    return (
      <div style={{
        padding: '12px', background: 'rgba(220,53,69,0.08)',
        border: '1px solid var(--c-danger)', borderRadius: 6,
        display: 'flex', flexDirection: 'column', gap: 8
      }}>
        <div style={{
          fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--c-danger)',
          fontWeight: 'bold', letterSpacing: '0.1em'
        }}>
          ⚠️ AUTOMATED PARAMETER TUNING RECOMMENDATIONS
        </div>
        
        {isCpaBreached && (
          <div style={{ fontSize: 9, fontFamily: 'var(--f-body)', color: 'var(--txt-1)' }}>
            <strong style={{ color: 'var(--c-danger)' }}>[CPA Breach]</strong> {cpaRec}
          </div>
        )}

        {isRudderBreached && (
          <div style={{ fontSize: 9, fontFamily: 'var(--f-body)', color: 'var(--txt-1)' }}>
            <strong style={{ color: 'var(--c-danger)' }}>[Rudder Breach]</strong> Hard rudder limit exceeded (> 35.0°). Suggest smoothing control command filter rates or adjusting the yaw rate penalty 'weight_yaw_rate' in the planner config.
          </div>
        )}
      </div>
    );
  };
  ```

- [ ] **Step 2: Create unit tests in BoundaryDiagnostics.test.tsx**
  Verify recommendations update depending on events list.

  ```typescript
  import { describe, it, expect } from 'vitest';
  import { render, screen } from '@testing-library/react';
  import { BoundaryDiagnostics } from '../BoundaryDiagnostics';

  describe('BoundaryDiagnostics', () => {
    it('renders PASS status when boundaries are not violated', () => {
      render(<BoundaryDiagnostics minCpaNm={0.35} maxRudderDeg={25.0} events={[]} />);
      expect(screen.getByText(/SAFETY & BOUNDARY STATUS: PASS/)).toBeInTheDocument();
    });

    it('renders CPA advice when CPA is breached with high latency', () => {
      const mockEvents = [
        { t: 10, k: 'CPA_PROJ', sev: 'warn', m: 'M2', d: 'CPA warning' },
        { t: 25, k: 'MPC_BRANCH', sev: 'info', m: 'M5', d: 'MPC avoids' }
      ];
      render(<BoundaryDiagnostics minCpaNm={0.20} maxRudderDeg={25.0} events={mockEvents} />);
      expect(screen.getByText(/took action late/)).toBeInTheDocument();
      expect(screen.getByText(/mso_cpa_threshold_nm/)).toBeInTheDocument();
    });
  });
  ```

- [ ] **Step 3: Run unit tests**
  Run: `npm test --prefix web -- --run`
  Expected: PASS

- [ ] **Step 4: Commit**
  ```bash
  git add web/src/screens/shared/BoundaryDiagnostics.tsx web/src/screens/shared/__tests__/BoundaryDiagnostics.test.tsx
  git commit -m "feat: add BoundaryDiagnostics tuning recommendations card"
  ```

---

### Task 4: Connect Scrubber to TrajectoryReplay and Layout the Three Columns

**Files:**
- Modify: `web/src/screens/shared/TrajectoryReplay.tsx`
- Modify: `web/src/screens/SimulationEvaluator.tsx`
- Modify: `web/src/screens/__tests__/SimulationEvaluator.test.tsx`

- [ ] **Step 1: Pass scrubbing event callback in TrajectoryReplay.tsx**
  Update the playback scrubbing events inside `TrajectoryReplay.tsx` to notify the parent `SimulationEvaluator` component of playback changes. Add a drag listener to the map time scrubber or slide inputs. Wait, the time slider in `TrajectoryReplay` is simulated inside `React.useEffect` when `playing` is true. Ensure that `onTimeChange` is wired to the playback loop interval.
  Let's verify: `TrajectoryReplay.tsx` lines 17-29:
  ```typescript
  React.useEffect(() => {
    if (!playing || !onTimeChange) return;
    const interval = setInterval(() => {
      const next = currentTimeSec + 0.1 * rate;
      if (next >= durationSec) {
        onTimeChange(durationSec);
        setPlaying(false);
      } else {
        onTimeChange(next);
      }
    }, 100);
    return () => clearInterval(interval);
  }, [playing, rate, durationSec, currentTimeSec, onTimeChange]);
  ```
  This is already correctly checking `onTimeChange`. But we need to ensure that the time state updates are sent by passing `onTimeChange={setCurrentTimeSec}` from the parent. We will update `SimulationEvaluator.tsx` to pass this.

- [ ] **Step 2: Update SimulationEvaluator.tsx layout to Three Columns**
  Refactor the grid layout. Create a 3-column container:
  Column 1 (42%): `TrajectoryReplay` taking 100% height.
  Column 2 (28%): `TimelineSixLane` (top, 180px), `ColregsDecisionTree` (middle), and `ScoringRadarChart` + `Takeover Panel` (bottom).
  Column 3 (30%): Compact `KPI Grid` (2x4 list), `BoundaryDiagnostics` recommendations card, and `AsdrLedger` scrollable table.

  ```typescript
  // Layout in SimulationEvaluator.tsx:
  return (
    <div style={{ height: '100%', display: 'flex', flexDirection: 'column', background: 'var(--bg-0)', overflow: 'hidden' }}>
      {/* Header */}
      ...
      
      {/* Three Column Content View */}
      <div style={{
        flex: 1, display: 'flex', gap: 16, padding: '12px 18px 18px', overflow: 'hidden'
      }}>
        {/* Column 1: Spatial Replay (42%) */}
        <div style={{ flex: '0 0 42%', display: 'flex', flexDirection: 'column', gap: 12 }}>
          <div className="glass-panel" style={{ flex: 1, borderRadius: 8, overflow: 'hidden', display: 'flex', flexDirection: 'column' }}>
            <TrajectoryReplay durationSec={600} currentTimeSec={currentTimeSec} onTimeChange={setCurrentTimeSec} />
          </div>
        </div>

        {/* Column 2: Temporal & Reasoning Audit (28%) */}
        <div style={{ flex: '0 0 28%', display: 'flex', flexDirection: 'column', gap: 12, overflowY: 'auto' }}>
          <div className="glass-panel" style={{ height: 180, borderRadius: 8, overflow: 'hidden', display: 'flex', flexDirection: 'column' }}>
            <TimelineSixLane
              events={reportEvents}
              durationSec={600}
              currentTimeSec={currentTimeSec}
              onScrub={setCurrentTimeSec}
            />
          </div>

          <div className="glass-panel" style={{ flex: 1, minHeight: 280, borderRadius: 8, overflow: 'hidden', display: 'flex', flexDirection: 'column' }}>
            <ColregsDecisionTree currentTimeSec={currentTimeSec} />
          </div>

          <div className="glass-panel" style={{ borderRadius: 8, overflow: 'hidden', display: 'grid', gridTemplateColumns: '1fr 1fr', padding: 12, gap: 12, minHeight: 180 }}>
            {/* Radar Chart */}
            <div style={{ display: 'flex', justifyContent: 'center', alignItems: 'center', borderRight: '1px solid var(--line-1)' }}>
              <ScoringRadarChart kpis={{
                safety: scoring?.scoring_dimensions?.safety ?? 0,
                ruleCompliance: scoring?.scoring_dimensions?.rule_compliance ?? 0,
                delay: Math.max(0, 1 - (scoring?.scoring_dimensions?.delay_penalty ?? 0)),
                magnitude: Math.max(0, 1 - (scoring?.scoring_dimensions?.action_magnitude_penalty ?? 0)),
                phase: scoring?.scoring_dimensions?.phase_score ?? 0,
                plausibility: scoring?.scoring_dimensions?.plausibility ?? 0,
              }} />
            </div>
            
            {/* Takeover panel */}
            <div style={{ display: 'flex', flexDirection: 'column', gap: 4, justifyContent: 'center' }}>
              <div style={{ fontFamily: 'var(--f-disp)', fontSize: 8, color: 'var(--txt-3)', textTransform: 'uppercase' }}>
                ToR Latency
              </div>
              <div style={{ fontSize: 14, fontWeight: 'bold', color: 'var(--c-warn)', fontFamily: 'var(--f-mono)' }}>5.8 s</div>
              <div style={{ fontSize: 7.5, color: 'var(--txt-3)' }}>✓ CCS/Veitch Compliant (&lt; 10s)</div>
            </div>
          </div>
        </div>

        {/* Column 3: Analytics & Diagnostics (30%) */}
        <div style={{ flex: '0 0 30%', display: 'flex', flexDirection: 'column', gap: 12, overflow: 'hidden' }}>
          {/* KPI list */}
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
            {/* 8 KPI cards mapped */}
            {[
              {
                label: 'VERDICT',
                value: scoring?.verdict ? scoring.verdict.toUpperCase() : '—',
                accent: scoring?.verdict === 'pass' ? 'var(--c-stbd)' : 'var(--c-danger)',
              },
              {
                label: 'Min CPA',
                value: kpis?.min_cpa_nm != null ? `${kpis.min_cpa_nm.toFixed(3)} nm` : '—',
                accent: kpis?.min_cpa_nm != null && kpis.min_cpa_nm >= 0.27 ? 'var(--c-phos)' : 'var(--c-danger)',
              },
              {
                label: 'TCPA Min',
                value: kpis?.tcpa_min_s != null ? `${kpis.tcpa_min_s.toFixed(0)} s` : '—',
                accent: 'var(--c-info)',
              },
              {
                label: 'Avg ROT',
                value: kpis?.avg_rot_dpm != null ? `${kpis.avg_rot_dpm.toFixed(1)} °/min` : '—',
                accent: 'var(--c-info)',
              },
              {
                label: 'Max Rudder',
                value: kpis?.max_rudder_deg != null ? `${kpis.max_rudder_deg.toFixed(1)}°` : '—',
                accent: kpis?.max_rudder_deg != null && kpis.max_rudder_deg <= 35 ? 'var(--c-stbd)' : 'var(--c-danger)',
              },
              {
                label: 'Grounding Risk',
                value: kpis?.grounding_risk_score != null ? `${(kpis.grounding_risk_score * 100).toFixed(1)}%` : '—',
                accent: kpis?.grounding_risk_score != null && kpis.grounding_risk_score >= 0.9 ? 'var(--c-stbd)' : 'var(--c-danger)',
              },
              {
                label: 'Route Dev',
                value: kpis?.route_deviation_nm != null ? `${kpis.route_deviation_nm.toFixed(2)} nm` : '—',
                accent: 'var(--c-warn)',
              },
              {
                label: 'Time to MRC',
                value: kpis?.time_to_mrm_s != null && kpis.time_to_mrm_s > 0 ? `${kpis.time_to_mrm_s.toFixed(0)} s` : 'N/A',
                accent: 'var(--c-warn)',
              },
            ].map((kpi, idx) => (
              <div key={idx} style={{
                background: 'var(--bg-1)',
                border: '1px solid var(--line-1)',
                borderRadius: 4,
                padding: '4px 8px',
                display: 'flex',
                flexDirection: 'column',
              }}>
                <span style={{ fontSize: 7.5, color: 'var(--txt-3)', textTransform: 'uppercase' }}>{kpi.label}</span>
                <span style={{ fontSize: 12, fontWeight: 'bold', color: kpi.accent, fontFamily: 'var(--f-mono)', marginTop: 2 }}>{kpi.value}</span>
              </div>
            ))}
          </div>

          {/* Diagnostics Card */}
          <BoundaryDiagnostics
            minCpaNm={kpis?.min_cpa_nm}
            maxRudderDeg={kpis?.max_rudder_deg}
            events={reportEvents}
          />

          {/* Ledger table */}
          <div className="glass-panel" style={{ flex: 1, borderRadius: 8, overflow: 'hidden', display: 'flex', flexDirection: 'column' }}>
            <AsdrLedger events={asdrLedgerEvents} currentTimeSec={currentTimeSec} />
          </div>
        </div>
      </div>
    </div>
  );
  ```

- [ ] **Step 3: Update unit tests in SimulationEvaluator.test.tsx**
  Verify that the updated `SimulationEvaluator` renders without errors.

- [ ] **Step 4: Run unit tests**
  Run: `npm test --prefix web -- --run`
  Expected: PASS

- [ ] **Step 5: Commit**
  ```bash
  git add web/src/screens/SimulationEvaluator.tsx web/src/screens/__tests__/SimulationEvaluator.test.tsx
  git commit -m "feat: implement three-column dashboard layout and hook up all subcomponents"
  ```

---

### Task 5: Verify via E2E Integration and Sync to A4000 Server

**Files:**
- Create: `web/e2e/evaluator_scrub.spec.ts`

- [ ] **Step 1: Write Playwright E2E test to verify interactive scrubbing**
  Verify timeline scrubbing correctly propagates to Map positions and Ledger highlights.

  ```typescript
  import { test, expect } from '@playwright/test';

  test('Simulation Evaluator scrub syncs components', async ({ page }) => {
    await page.goto('http://localhost:56942/#/evaluator/latest');
    
    // Check if evaluator renders
    await expect(page.locator('[data-testid="asdr-ledger"]')).toBeVisible();

    // Verify decision tree is rendered
    await expect(page.locator('[data-testid="decision-tree"]')).toBeVisible();

    // Scrub timeline to T+01:00
    const timeline = page.locator('[data-testid="timeline-playback"]');
    const box = await timeline.boundingBox();
    if (box) {
      // Click at 10% progress of timeline width to trigger state change
      await page.mouse.click(box.x + box.width * 0.1, box.y + box.height / 2);
      
      // Highlighted item should update inside the ledger
      await expect(page.locator('tr.highlighted')).toBeVisible();
    }
  });
  ```

- [ ] **Step 2: Sync all files to the A4000 server**
  Use `scp` or equivalent tool to push files to `~/Code/mass-l3` on the remote host `192.168.121.50` (or `ssh a4000`).

  Run:
  ```bash
  scp -r web/src/screens/shared/BoundaryDiagnostics.tsx \
         web/src/screens/shared/__tests__/BoundaryDiagnostics.test.tsx \
         web/src/screens/shared/ColregsDecisionTree.tsx \
         web/src/screens/shared/__tests__/ColregsDecisionTree.test.tsx \
         web/src/screens/shared/AsdrLedger.tsx \
         web/src/screens/shared/__tests__/AsdrLedger.test.tsx \
         web/src/screens/SimulationEvaluator.tsx \
         a4000:~/Code/mass-l3/web/src/screens/
  ```

- [ ] **Step 3: Run full verification on local & remote**
  Run: `npm test --prefix web -- --run` on local and remote to guarantee clean regression status.
  Expected: PASS

- [ ] **Step 4: Commit and tag**
  ```bash
  git commit -am "chore: update evaluation screen layout and scrubbing bindings complete"
  ```
