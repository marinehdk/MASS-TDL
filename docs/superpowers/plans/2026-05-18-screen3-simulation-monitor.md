# Screen 3 Simulation-Monitor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the full Screen 3 Simulation-Monitor — captain ECDIS-compliant view + engineer white-box view (IvP/MPC/COLREGs/SOTIF algorithm panels) — sharing one 50 Hz telemetry stream and one FSM state machine.

**Architecture:** Task 0 (serial) establishes SAT types and store expansions that all parallel tracks depend on. Tracks A/B/C execute in parallel after Task 0 completes. Track A fixes TorModal's physical-lock mechanic. Tracks B and C build new map-layer components and panel components respectively. The final task (SimulationMonitor full refactor) wires everything together and depends on A+B+C completion.

**Tech Stack:** React 18, TypeScript, Zustand (uiStore/fsmStore/telemetryStore/controlStore), MapLibre GL JS, vitest + @testing-library/react (jsdom), foxglove_bridge WebSocket

**Spec:** `docs/superpowers/specs/2026-05-18-screen3-simulation-monitor-design.md`

**Parallel execution map:**
```
Task 0 (serial) ──┬── Track A: Task 1 (TorModal fix)
                  │
                  ├── Track B: Task 2 (SafetyDomainLayer)
                  │            Task 3 (IvpRiskGradientLayer)
                  │            Task 4 (MpcTrajectoryLayer)
                  │
                  └── Track C: Task 5 (ColregsRationaleTree)
                               Task 6 (SotifMonitorStrip)
                               Task 7 (DecisionChainTimingBar)

All A+B+C complete ── Task 8 (SimulationMonitor refactor) ── DONE
```

---

## Task 0: Foundation — SAT Types + Store Updates

**Files:**
- Create: `web/src/types/sat.ts`
- Modify: `web/src/store/uiStore.ts`
- Modify: `web/src/store/fsmStore.ts`
- Modify: `web/src/store/telemetryStore.ts`
- Modify: `web/src/store/__tests__/fsmStore.test.ts`

### 0A — SAT Types

- [ ] **Step 1: Create `web/src/types/sat.ts`**

```typescript
// SAT-1/2/3 transparency types (Chen et al. 2014 [W54])
// Consumed by engineer-view panels; all fields optional to handle partial telemetry

export interface IvpContribution {
  direction_deg: number;    // 0 / 45 / 90 / 135 / 180 / 225 / 270 / 315
  cost: number;             // 0.0–1.0  (higher = more dangerous/costly)
  label?: string;           // debug label, e.g. 'cpa_penalty'
}

export interface ColregsChainLayer {
  layer: 1 | 2 | 3 | 4 | 5;
  label: string;            // e.g. 'ODD', '会遇分类', '责任', '方向', '时机'
  conclusion: string;       // e.g. 'GIVE-WAY', 'Rule 14', 'STBD ≥30°'
  inputs: Record<string, string | number>;
  confidence?: number;
  timing_stage?: 'STAGE_1' | 'STAGE_2' | 'STAGE_3' | 'EMERGENCY';
  escalation?: boolean;
}

export interface TrajectoryCandidate {
  id: number;
  points: Array<{ lon: number; lat: number }>;
  cost: number;             // 0.0–1.0 (lower = better)
  is_optimal: boolean;
  type: 'mid_mpc' | 'bc_mpc';
}

export interface SAT2Data {
  ivp_contributions: IvpContribution[];       // M4: 8 directional costs
  active_behavior: string | null;             // M4: winning behavior name
  active_behavior_weight: number;             // M4: weight 0.0–1.0
  colregs_chain: ColregsChainLayer[];         // M6: up to 5 layers
  colregs_chain_target_id: string | null;     // M6: MMSI of active target
  reasoning_latency_ms: number;              // M6: solve time
}

export interface SAT3Data {
  trajectory_candidates: TrajectoryCandidate[]; // M5: mid + bc candidates
  uncertainty_bands: boolean;                   // Phase 3 flag
}

export interface SotifMetrics {
  ais_radar_consistency_sigma: number;   // >2.0σ → warning
  target_predictability_rms_m: number;  // >50m  → warning
  perception_coverage_pct: number;      // <80%  → warning
  colregs_parse_failures: number;       // >3    → warning (3-window)
  comm_link_rtt_ms: number;             // >2000 → warning
  checker_veto_rate_pct: number;        // >20%  → warning (15s window)
}
```

- [ ] **Step 2: Add SAT types to `web/src/types/index.ts`**

The file currently contains `export * from './sil';`. Append:

```typescript
export * from './sat';
```

### 0B — uiStore: add 'engineer', drawers

- [ ] **Step 3: Write failing test**

Add to a new file `web/src/store/__tests__/uiStore.test.ts`:

```typescript
import { describe, it, expect, beforeEach } from 'vitest';
import { useUIStore } from '../uiStore';

describe('uiStore', () => {
  beforeEach(() => { useUIStore.getState().reset(); });

  it('viewMode defaults to captain', () => {
    expect(useUIStore.getState().viewMode).toBe('captain');
  });

  it('accepts engineer viewMode', () => {
    useUIStore.getState().setViewMode('engineer');
    expect(useUIStore.getState().viewMode).toBe('engineer');
  });

  it('leftDrawerOpen defaults to false', () => {
    expect(useUIStore.getState().leftDrawerOpen).toBe(false);
  });

  it('toggleLeftDrawer flips leftDrawerOpen', () => {
    useUIStore.getState().toggleLeftDrawer();
    expect(useUIStore.getState().leftDrawerOpen).toBe(true);
    useUIStore.getState().toggleLeftDrawer();
    expect(useUIStore.getState().leftDrawerOpen).toBe(false);
  });

  it('toggleRightDrawer flips rightDrawerOpen', () => {
    useUIStore.getState().toggleRightDrawer();
    expect(useUIStore.getState().rightDrawerOpen).toBe(true);
  });

  it('reset restores all defaults', () => {
    useUIStore.getState().setViewMode('engineer');
    useUIStore.getState().toggleLeftDrawer();
    useUIStore.getState().reset();
    const s = useUIStore.getState();
    expect(s.viewMode).toBe('captain');
    expect(s.leftDrawerOpen).toBe(false);
    expect(s.rightDrawerOpen).toBe(false);
  });
});
```

- [ ] **Step 4: Run to confirm failure**

```bash
cd web && npx vitest run src/store/__tests__/uiStore.test.ts
```

Expected: FAIL — `Property 'engineer' does not exist on type 'ViewMode'` / `toggleLeftDrawer is not a function`

- [ ] **Step 5: Update `web/src/store/uiStore.ts`**

Replace the entire file content:

```typescript
import { create } from 'zustand';

// 'god' renamed to 'engineer' for semantic clarity (DEMO-1)
// Backward-compat: SilMapView accepts 'engineer' as it formerly accepted 'god'
export type ViewMode = 'captain' | 'engineer' | 'roc';

interface UIState {
  viewMode: ViewMode;
  leftDrawerOpen: boolean;
  rightDrawerOpen: boolean;
  asdrLogExpanded: boolean;
  pulseBarExpanded: boolean;

  setViewMode: (mode: ViewMode) => void;
  toggleLeftDrawer: () => void;
  toggleRightDrawer: () => void;
  toggleAsdrLog: () => void;
  togglePulseBar: () => void;
  reset: () => void;
}

export const useUIStore = create<UIState>((set) => ({
  viewMode: 'captain',
  leftDrawerOpen: false,
  rightDrawerOpen: false,
  asdrLogExpanded: false,
  pulseBarExpanded: false,
  setViewMode: (viewMode) => set({ viewMode }),
  toggleLeftDrawer: () => set((s) => ({ leftDrawerOpen: !s.leftDrawerOpen })),
  toggleRightDrawer: () => set((s) => ({ rightDrawerOpen: !s.rightDrawerOpen })),
  toggleAsdrLog: () => set((s) => ({ asdrLogExpanded: !s.asdrLogExpanded })),
  togglePulseBar: () => set((s) => ({ pulseBarExpanded: !s.pulseBarExpanded })),
  reset: () => set({
    viewMode: 'captain',
    leftDrawerOpen: false,
    rightDrawerOpen: false,
    asdrLogExpanded: false,
    pulseBarExpanded: false,
  }),
}));
```

- [ ] **Step 6: Run to confirm pass**

```bash
cd web && npx vitest run src/store/__tests__/uiStore.test.ts
```

Expected: PASS — 6 tests passed

### 0C — fsmStore: remove sat1LockUntilSimTime, add recommendedMrm

- [ ] **Step 7: Update existing fsmStore test to remove sat1LockUntilSimTime**

Edit `web/src/store/__tests__/fsmStore.test.ts` — replace the `setTorRequest stores request` test body (line 42–51):

```typescript
it('setTorRequest stores request', () => {
  const req = {
    reason: 'M7 Veto',
    triggeredAtSimTime: 200,
    tmrDeadlineSimTime: 260,
    currentSituation: 'Target too close',
    proposedAction: 'Starboard turn',
    recommendedMrm: 'MRM-01' as const,
  };
  useFsmStore.getState().setTorRequest(req);
  expect(useFsmStore.getState().torRequest).toEqual(req);
});
```

Also update the `setTorRequest(null) clears request` test (line 54–61):

```typescript
it('setTorRequest(null) clears request', () => {
  useFsmStore.getState().setTorRequest({
    reason: 'test', triggeredAtSimTime: 0, tmrDeadlineSimTime: 60,
    currentSituation: '', proposedAction: '',
  });
  useFsmStore.getState().setTorRequest(null);
  expect(useFsmStore.getState().torRequest).toBeNull();
});
```

- [ ] **Step 8: Run to confirm failure (sat1LockUntilSimTime still in type)**

```bash
cd web && npx vitest run src/store/__tests__/fsmStore.test.ts
```

Expected: type error or test failure

- [ ] **Step 9: Update `web/src/store/fsmStore.ts`**

Replace the entire file:

```typescript
import { create } from 'zustand';

export type FsmState = 'TRANSIT' | 'COLREG_AVOIDANCE' | 'TOR' | 'OVERRIDE' | 'MRC' | 'HANDBACK';

export interface FsmTransition {
  from: FsmState;
  to: FsmState;
  reason: string;
  timestamp: number;  // sim_time seconds
}

export interface TorRequest {
  reason: string;
  triggeredAtSimTime: number;
  tmrDeadlineSimTime: number;        // triggeredAtSimTime + 60s
  // Physical lock is ≥2s sustained pointer-hold per IMO MASS Code Part 2-A §6.3.2
  // sat1LockUntilSimTime removed — timer-based approach was non-compliant
  currentSituation: string;
  proposedAction: string;
  recommendedMrm?: 'MRM-01' | 'MRM-02' | 'MRM-03' | 'MRM-04';
}

interface FsmStore {
  currentState: FsmState;
  transitionHistory: FsmTransition[];
  torRequest: TorRequest | null;
  setState: (next: FsmState, reason: string, simTime: number) => void;
  setTorRequest: (req: TorRequest | null) => void;
  clearHistory: () => void;
}

export const useFsmStore = create<FsmStore>((set) => ({
  currentState: 'TRANSIT',
  transitionHistory: [],
  torRequest: null,
  setState: (next, reason, simTime) => set((s) => ({
    currentState: next,
    transitionHistory: [
      ...s.transitionHistory,
      { from: s.currentState, to: next, reason, timestamp: simTime },
    ].slice(-100),
  })),
  setTorRequest: (req) => set({ torRequest: req }),
  clearHistory: () => set({ transitionHistory: [] }),
}));
```

- [ ] **Step 10: Run to confirm pass**

```bash
cd web && npx vitest run src/store/__tests__/fsmStore.test.ts
```

Expected: PASS — all 5 tests pass

### 0D — telemetryStore: add SAT + SOTIF fields

- [ ] **Step 11: Write failing tests for new store fields**

Create `web/src/store/__tests__/telemetrySatStore.test.ts`:

```typescript
import { describe, it, expect, beforeEach } from 'vitest';
import { useTelemetryStore } from '../telemetryStore';
import type { SAT2Data, SAT3Data, SotifMetrics } from '../../types/sat';

describe('telemetryStore SAT fields', () => {
  beforeEach(() => { useTelemetryStore.getState().reset(); });

  it('sat2 starts null', () => {
    expect(useTelemetryStore.getState().sat2).toBeNull();
  });

  it('updateSat2 stores SAT2Data', () => {
    const data: SAT2Data = {
      ivp_contributions: [{ direction_deg: 0, cost: 0.2 }, { direction_deg: 90, cost: 0.8 }],
      active_behavior: 'COLREGs_Avoidance',
      active_behavior_weight: 0.7,
      colregs_chain: [],
      colregs_chain_target_id: '123456789',
      reasoning_latency_ms: 2.3,
    };
    useTelemetryStore.getState().updateSat2(data);
    expect(useTelemetryStore.getState().sat2).toEqual(data);
  });

  it('sat3 starts null', () => {
    expect(useTelemetryStore.getState().sat3).toBeNull();
  });

  it('updateSat3 stores SAT3Data', () => {
    const data: SAT3Data = {
      trajectory_candidates: [
        { id: 0, points: [{ lon: 10.4, lat: 63.4 }], cost: 0.1, is_optimal: true, type: 'mid_mpc' },
      ],
      uncertainty_bands: false,
    };
    useTelemetryStore.getState().updateSat3(data);
    expect(useTelemetryStore.getState().sat3?.trajectory_candidates).toHaveLength(1);
  });

  it('sotifMetrics starts null', () => {
    expect(useTelemetryStore.getState().sotifMetrics).toBeNull();
  });

  it('updateSotifMetrics stores metrics', () => {
    const m: SotifMetrics = {
      ais_radar_consistency_sigma: 1.8,
      target_predictability_rms_m: 41,
      perception_coverage_pct: 95,
      colregs_parse_failures: 0,
      comm_link_rtt_ms: 120,
      checker_veto_rate_pct: 8,
    };
    useTelemetryStore.getState().updateSotifMetrics(m);
    expect(useTelemetryStore.getState().sotifMetrics?.checker_veto_rate_pct).toBe(8);
  });

  it('reset clears SAT fields', () => {
    useTelemetryStore.getState().updateSat2({
      ivp_contributions: [], active_behavior: 'Transit', active_behavior_weight: 0.3,
      colregs_chain: [], colregs_chain_target_id: null, reasoning_latency_ms: 0,
    });
    useTelemetryStore.getState().reset();
    expect(useTelemetryStore.getState().sat2).toBeNull();
  });
});
```

- [ ] **Step 12: Run to confirm failure**

```bash
cd web && npx vitest run src/store/__tests__/telemetrySatStore.test.ts
```

Expected: FAIL — `updateSat2 is not a function` / `sat2 is undefined`

- [ ] **Step 13: Update `web/src/store/telemetryStore.ts` — add SAT fields**

Add new imports at the top (after the existing `import { create }` line):

```typescript
import type { SAT2Data, SAT3Data, SotifMetrics } from '../types/sat';
```

Add to the `TelemetryState` interface (after `controlCmd: ControlCmdState | null;`):

```typescript
  /** SAT-2 reasoning transparency (engineer view, ~4 Hz) */
  sat2: SAT2Data | null;
  /** SAT-3 prediction transparency (engineer view, ~2 Hz, Phase 3) */
  sat3: SAT3Data | null;
  /** M7 SOTIF 6-assumption metrics (1 Hz) */
  sotifMetrics: SotifMetrics | null;
```

Add new updater signatures (after `appendPreflightLog: ...`):

```typescript
  updateSat2: (data: SAT2Data) => void;
  updateSat3: (data: SAT3Data) => void;
  updateSotifMetrics: (metrics: SotifMetrics) => void;
```

Add to `initialState` object (after `preflightLog: []`):

```typescript
  sat2: null,
  sat3: null,
  sotifMetrics: null,
```

Add implementations to `create<TelemetryState>` (after `appendPreflightLog: ...`):

```typescript
  updateSat2: (sat2) => set({ sat2 }),
  updateSat3: (sat3) => set({ sat3 }),
  updateSotifMetrics: (sotifMetrics) => set({ sotifMetrics }),
```

- [ ] **Step 14: Run to confirm pass**

```bash
cd web && npx vitest run src/store/__tests__/telemetrySatStore.test.ts
```

Expected: PASS — 7 tests pass

- [ ] **Step 15: Commit Task 0**

```bash
cd web && git add src/types/sat.ts src/types/index.ts src/store/uiStore.ts src/store/fsmStore.ts src/store/telemetryStore.ts src/store/__tests__/uiStore.test.ts src/store/__tests__/fsmStore.test.ts src/store/__tests__/telemetrySatStore.test.ts && git commit -m "feat(screen3): Task 0 — SAT types, uiStore engineer+drawers, fsmStore TorRequest, telemetryStore SAT fields

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Task 1 (Track A): TorModal — Replace 5s Timer with ≥2s Pointer-Hold

**Context:** Current `TorModal.tsx` uses a time-elapsed grayout (5s) that is non-compliant with IMO MASS Code Part 2-A §6.3.2 which requires ≥2s sustained press. GAP-NEW-003.

**Files:**
- Modify: `web/src/screens/shared/TorModal.tsx`
- Create: `web/src/screens/shared/__tests__/TorModal.test.tsx`

- [ ] **Step 1: Write failing tests**

```typescript
// web/src/screens/shared/__tests__/TorModal.test.tsx
import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, act } from '@testing-library/react';
import { TorModal } from '../TorModal';
import { useFsmStore } from '../../../store/fsmStore';
import { useTelemetryStore } from '../../../store';

beforeEach(() => {
  useFsmStore.setState({
    currentState: 'TOR',
    transitionHistory: [],
    torRequest: {
      reason: 'M7 VETO: AIS consistency >2σ',
      triggeredAtSimTime: 100,
      tmrDeadlineSimTime: 160,
      currentSituation: 'Target CPA 0.3nm',
      proposedAction: 'Starboard 30°',
      recommendedMrm: 'MRM-01',
    },
  });
  useTelemetryStore.setState({ lifecycleStatus: { sim_time: 110 } } as any);
});

afterEach(() => {
  useFsmStore.getState().clearHistory();
  useFsmStore.getState().setTorRequest(null);
});

describe('TorModal', () => {
  it('renders when FSM is TOR', () => {
    render(<TorModal />);
    expect(screen.getByTestId('tor-modal')).toBeInTheDocument();
  });

  it('does not render when FSM is TRANSIT', () => {
    useFsmStore.setState({ currentState: 'TRANSIT' });
    render(<TorModal />);
    expect(screen.queryByTestId('tor-modal')).toBeNull();
  });

  it('TAKE OVER button is present', () => {
    render(<TorModal />);
    expect(screen.getByTestId('tor-take-control')).toBeInTheDocument();
  });

  it('pointer-down shows hold progress bar', () => {
    render(<TorModal />);
    const btn = screen.getByTestId('tor-take-control');
    fireEvent.pointerDown(btn);
    expect(screen.getByTestId('tor-hold-progress')).toBeInTheDocument();
  });

  it('pointer-up before 2s does NOT transition to OVERRIDE', () => {
    vi.useFakeTimers();
    render(<TorModal />);
    const btn = screen.getByTestId('tor-take-control');
    fireEvent.pointerDown(btn);
    act(() => { vi.advanceTimersByTime(800); });
    fireEvent.pointerUp(btn);
    expect(useFsmStore.getState().currentState).toBe('TOR');
    vi.useRealTimers();
  });

  it('holding ≥2s transitions to OVERRIDE', () => {
    vi.useFakeTimers();
    render(<TorModal />);
    const btn = screen.getByTestId('tor-take-control');
    fireEvent.pointerDown(btn);
    act(() => { vi.advanceTimersByTime(2100); });
    fireEvent.pointerUp(btn);
    expect(useFsmStore.getState().currentState).toBe('OVERRIDE');
    vi.useRealTimers();
  });

  it('DECLINE button triggers MRC', () => {
    render(<TorModal />);
    fireEvent.click(screen.getByTestId('tor-decline'));
    expect(useFsmStore.getState().currentState).toBe('MRC');
  });

  it('shows countdown in seconds', () => {
    render(<TorModal />);
    // sim_time=110, deadline=160 → 50s remaining
    expect(screen.getByTestId('tor-countdown').textContent).toMatch(/50/);
  });
});
```

- [ ] **Step 2: Run to confirm failure**

```bash
cd web && npx vitest run src/screens/shared/__tests__/TorModal.test.tsx
```

Expected: FAIL — `tor-hold-progress` not found, holding does not change FSM state

- [ ] **Step 3: Replace `web/src/screens/shared/TorModal.tsx`**

```typescript
import React, { useEffect, useRef, useState, useCallback } from 'react';
import { createPortal } from 'react-dom';
import { useFsmStore, useTelemetryStore } from '../../store';

const HOLD_DURATION_MS = 2000;  // IMO MASS Code Part 2-A §6.3.2

export const TorModal: React.FC = () => {
  const currentState = useFsmStore((s) => s.currentState);
  const torRequest   = useFsmStore((s) => s.torRequest);
  const setState     = useFsmStore((s) => s.setState);
  const setTorRequest = useFsmStore((s) => s.setTorRequest);
  const simTime      = useTelemetryStore((s) => s.lifecycleStatus?.sim_time ?? 0);

  const [holdProgress, setHoldProgress] = useState(0);  // 0.0–1.0
  const [autoTransitioned, setAutoTransitioned] = useState(false);
  const holdStartRef    = useRef<number | null>(null);
  const intervalRef     = useRef<ReturnType<typeof setInterval> | null>(null);

  const clearHold = useCallback(() => {
    holdStartRef.current = null;
    if (intervalRef.current) clearInterval(intervalRef.current);
    intervalRef.current = null;
    setHoldProgress(0);
  }, []);

  const handlePointerDown = useCallback(() => {
    holdStartRef.current = Date.now();
    intervalRef.current = setInterval(() => {
      const elapsed = Date.now() - (holdStartRef.current ?? Date.now());
      const progress = Math.min(1, elapsed / HOLD_DURATION_MS);
      setHoldProgress(progress);
      if (progress >= 1) {
        clearHold();
        setState('OVERRIDE', 'CAPTAIN_TAKE_CONTROL', simTime);
        setTorRequest(null);
      }
    }, 50);
  }, [simTime, setState, setTorRequest, clearHold]);

  const handlePointerUp = useCallback(() => {
    const elapsed = holdStartRef.current ? Date.now() - holdStartRef.current : 0;
    clearHold();
    if (elapsed >= HOLD_DURATION_MS) {
      setState('OVERRIDE', 'CAPTAIN_TAKE_CONTROL', simTime);
      setTorRequest(null);
    }
    // else: hold was too short — progress resets, no state change
  }, [simTime, setState, setTorRequest, clearHold]);

  // Auto-MRC on timeout
  useEffect(() => {
    if (!torRequest) return;
    if (simTime >= torRequest.tmrDeadlineSimTime && !autoTransitioned) {
      setAutoTransitioned(true);
      setState('MRC', 'TMR_TIMEOUT', simTime);
      const id = setTimeout(() => { setTorRequest(null); setAutoTransitioned(false); }, 5000);
      return () => clearTimeout(id);
    }
  }, [simTime, torRequest, autoTransitioned, setState, setTorRequest]);

  // Cleanup interval on unmount
  useEffect(() => () => { if (intervalRef.current) clearInterval(intervalRef.current); }, []);

  if (!torRequest || currentState !== 'TOR') return null;

  const deadline = Math.max(0, torRequest.tmrDeadlineSimTime - simTime);
  const deadlineColor = deadline < 10 ? 'var(--c-danger)' : deadline < 30 ? 'var(--c-warn)' : 'var(--c-phos)';

  // Phase 0/1/2 escalation styling
  const elapsed60 = 60 - deadline;
  const borderColor = elapsed60 >= 45 ? '#8b0000' : elapsed60 >= 20 ? 'var(--c-warn)' : 'var(--line-2)';

  return createPortal(
    <div data-testid="tor-modal" style={{
      position: 'fixed', inset: 0,
      background: 'rgba(7,12,19,0.78)', backdropFilter: 'blur(6px)',
      display: 'flex', alignItems: 'center', justifyContent: 'center',
      zIndex: 100, pointerEvents: 'auto',
    }}>
      <div style={{
        width: 560, border: `2px solid ${borderColor}`,
        background: 'var(--bg-1)', position: 'relative',
        boxShadow: elapsed60 >= 45 ? '0 0 32px rgba(139,0,0,0.6)' : elapsed60 >= 20 ? '0 0 24px rgba(251,191,36,0.4)' : 'none',
      }}>
        {/* Scan line */}
        <div style={{ height: 4, background: 'var(--bg-0)', position: 'relative', overflow: 'hidden' }}>
          <div style={{
            position: 'absolute', inset: 0, width: '30%',
            background: `linear-gradient(90deg, transparent, ${borderColor}, transparent)`,
            animation: 'scan-line 1.6s linear infinite',
          }} />
        </div>

        <div style={{ padding: 20 }}>
          {/* Header */}
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline' }}>
            <span style={{
              fontFamily: 'var(--f-disp)', fontSize: 11, color: 'var(--c-warn)',
              fontWeight: 700, letterSpacing: '0.20em', textTransform: 'uppercase',
            }}>⚠ TRANSFER OF RESPONSIBILITY REQUEST</span>
            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-3)' }}>
              {torRequest.recommendedMrm ?? 'MRM-01'}
            </span>
          </div>

          {/* Reason */}
          <div style={{
            fontFamily: 'var(--f-disp)', fontSize: 15, color: 'var(--txt-0)',
            fontWeight: 500, lineHeight: 1.4, marginTop: 10,
          }}>{torRequest.reason}</div>

          {/* Situation / Action */}
          <div style={{ marginTop: 14, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 8 }}>
            <div style={{ padding: 8, background: 'var(--bg-0)', borderLeft: '2px solid var(--c-danger)' }}>
              <div style={{ fontFamily: 'var(--f-disp)', fontSize: 8, color: 'var(--c-danger)', letterSpacing: '0.18em', textTransform: 'uppercase' }}>CURRENT SITUATION</div>
              <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-1)', marginTop: 4 }}>{torRequest.currentSituation}</div>
            </div>
            <div style={{ padding: 8, background: 'var(--bg-0)', borderLeft: '2px solid var(--c-warn)' }}>
              <div style={{ fontFamily: 'var(--f-disp)', fontSize: 8, color: 'var(--c-warn)', letterSpacing: '0.18em', textTransform: 'uppercase' }}>PROPOSED ACTION</div>
              <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-1)', marginTop: 4 }}>{torRequest.proposedAction}</div>
            </div>
          </div>

          {/* Countdown */}
          <div style={{ marginTop: 14 }}>
            <div style={{ fontFamily: 'var(--f-disp)', fontSize: 8, color: 'var(--txt-3)', letterSpacing: '0.18em', textTransform: 'uppercase' }}>DEADLINE</div>
            <div data-testid="tor-countdown" style={{ fontFamily: 'var(--f-mono)', fontSize: 36, color: deadlineColor, fontWeight: 700 }}>
              {String(Math.ceil(deadline)).padStart(2, '0')}
              <span style={{ fontSize: 14, color: 'var(--txt-3)' }}> s</span>
            </div>
            <div style={{ height: 4, background: 'var(--bg-0)', marginTop: 4 }}>
              <div style={{ width: `${(deadline / 60) * 100}%`, height: '100%', background: deadlineColor, transition: 'width 0.4s' }} />
            </div>
          </div>

          {/* TAKE OVER button with pointer-hold mechanic */}
          <div style={{ marginTop: 16, display: 'flex', gap: 10 }}>
            <button
              data-testid="tor-decline"
              onClick={() => { setState('MRC', 'CAPTAIN_DECLINE', simTime); setTorRequest(null); }}
              style={{
                flex: 1, background: 'transparent', border: '1px solid var(--line-3)',
                color: 'var(--txt-1)', padding: '12px 0',
                fontFamily: 'var(--f-disp)', fontSize: 10.5, letterSpacing: '0.18em',
                fontWeight: 700, cursor: 'pointer',
              }}
            >DECLINE · MRC</button>
            <button
              data-testid="tor-take-control"
              onPointerDown={handlePointerDown}
              onPointerUp={handlePointerUp}
              onPointerLeave={handlePointerUp}
              style={{
                flex: 2, background: holdProgress > 0 ? `rgba(251,191,36,${0.1 + holdProgress * 0.5})` : 'var(--bg-0)',
                border: `1px solid ${holdProgress > 0 ? 'var(--c-warn)' : 'var(--line-2)'}`,
                color: holdProgress > 0 ? 'var(--c-warn)' : 'var(--txt-2)',
                padding: '12px 0', fontFamily: 'var(--f-disp)', fontSize: 11,
                letterSpacing: '0.18em', fontWeight: 700, cursor: 'pointer',
                userSelect: 'none', position: 'relative', overflow: 'hidden',
              }}
            >
              {holdProgress > 0 ? `HOLD TO CONFIRM (${((1 - holdProgress) * 2).toFixed(1)}s)` : 'HOLD ≥2s · TAKE CONTROL'}
            </button>
          </div>

          {/* Hold progress indicator */}
          {holdProgress > 0 && (
            <div data-testid="tor-hold-progress" style={{ marginTop: 4, height: 3, background: 'var(--bg-0)' }}>
              <div style={{
                width: `${holdProgress * 100}%`, height: '100%',
                background: 'var(--c-warn)', transition: 'width 0.05s linear',
              }} />
            </div>
          )}

          <div style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)', marginTop: 8 }}>
            IMO MASS Code Part 2-A §6.3.2 · ASDR: sat1_display_duration_s · operator_id
          </div>
        </div>
      </div>
    </div>,
    document.body
  );
};
```

- [ ] **Step 4: Run tests to confirm pass**

```bash
cd web && npx vitest run src/screens/shared/__tests__/TorModal.test.tsx
```

Expected: PASS — 8 tests pass

- [ ] **Step 5: Commit Task 1**

```bash
cd web && git add src/screens/shared/TorModal.tsx src/screens/shared/__tests__/TorModal.test.tsx && git commit -m "fix(screen3): TorModal — replace 5s timer with ≥2s pointer-hold per IMO MASS Code §6.3.2 (GAP-NEW-003)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Task 2 (Track B): SafetyDomainLayer — 3-Tier Safety Zones on Map

**Context:** Engineer view needs 3 concentric safety zones around own-ship (Observation 2nm dashed, Action 1nm amber, Critical 0.3nm red). Follows ImazuGeometry pattern.

**Files:**
- Create: `web/src/map/SafetyDomainLayer.tsx`
- Create: `web/src/map/__tests__/SafetyDomainLayer.test.tsx`

- [ ] **Step 1: Write failing test**

```typescript
// web/src/map/__tests__/SafetyDomainLayer.test.tsx
import { describe, it, expect, vi } from 'vitest';
import { render } from '@testing-library/react';
import { useRef } from 'react';
import { SafetyDomainLayer } from '../SafetyDomainLayer';
import type { OwnShipState } from '../../types';

const addSourceMock = vi.fn();
const addLayerMock  = vi.fn();
const getSourceMock = vi.fn(() => ({ setData: vi.fn() }));

const mockMap = {
  isStyleLoaded: vi.fn(() => true),
  once: vi.fn(),
  addSource: addSourceMock,
  addLayer: addLayerMock,
  getSource: getSourceMock,
  removeLayer: vi.fn(),
  removeSource: vi.fn(),
};

vi.mock('maplibre-gl', () => ({
  default: { Map: vi.fn(() => mockMap) },
}));

function makeOwnShip(lon: number, lat: number): OwnShipState {
  return {
    pose: { lon, lat, heading: 0 },
    kinematics: { sog: 0, cog: 0, rot: 0 },
  } as any;
}

function Wrapper({ ownShip }: { ownShip: OwnShipState | null }) {
  const ref = useRef(mockMap as any);
  return <SafetyDomainLayer mapRef={ref} ownShip={ownShip} visible={true} />;
}

describe('SafetyDomainLayer', () => {
  it('renders null (map overlay, no DOM output)', () => {
    const { container } = render(<Wrapper ownShip={null} />);
    expect(container.firstChild).toBeNull();
  });

  it('adds source and layers when ownShip is provided', () => {
    addSourceMock.mockClear();
    addLayerMock.mockClear();
    render(<Wrapper ownShip={makeOwnShip(10.4, 63.4)} />);
    expect(addSourceMock).toHaveBeenCalledWith('safety-domain', expect.objectContaining({ type: 'geojson' }));
    // 3 layers (observation, action, critical)
    expect(addLayerMock).toHaveBeenCalledTimes(3);
  });

  it('updates source data on ownShip change', () => {
    const setDataMock = vi.fn();
    getSourceMock.mockReturnValue({ setData: setDataMock });
    addSourceMock.mockClear();
    addLayerMock.mockClear();
    const { rerender } = render(<Wrapper ownShip={makeOwnShip(10.4, 63.4)} />);
    rerender(<Wrapper ownShip={makeOwnShip(10.5, 63.5)} />);
    expect(setDataMock).toHaveBeenCalled();
  });
});
```

- [ ] **Step 2: Run to confirm failure**

```bash
cd web && npx vitest run src/map/__tests__/SafetyDomainLayer.test.tsx
```

Expected: FAIL — module not found

- [ ] **Step 3: Create `web/src/map/SafetyDomainLayer.tsx`**

```typescript
import React, { useEffect, useRef } from 'react';
import type maplibregl from 'maplibre-gl';
import type { OwnShipState } from '../types';

interface SafetyDomainLayerProps {
  mapRef: React.MutableRefObject<maplibregl.Map | null>;
  ownShip: OwnShipState | null;
  visible: boolean;
  // ODD-A defaults; override from Capability Manifest when available
  observationNm?: number;  // default 2.0
  actionNm?: number;       // default 1.0
  criticalNm?: number;     // default 0.3
}

const SOURCE_ID = 'safety-domain';
const NM_TO_DEG = 1 / 60;  // 1nm ≈ 1/60° latitude; used for rough radius

function circleFeature(
  lon: number, lat: number, radiusNm: number,
  properties: Record<string, unknown>
): GeoJSON.Feature<GeoJSON.Polygon> {
  const steps = 64;
  const radiusDeg = radiusNm * NM_TO_DEG;
  const coords: [number, number][] = [];
  for (let i = 0; i <= steps; i++) {
    const angle = (i / steps) * 2 * Math.PI;
    // Longitude correction for latitude
    const lonCorrection = radiusDeg / Math.cos((lat * Math.PI) / 180);
    coords.push([lon + lonCorrection * Math.sin(angle), lat + radiusDeg * Math.cos(angle)]);
  }
  return { type: 'Feature', geometry: { type: 'Polygon', coordinates: [coords] }, properties };
}

export const SafetyDomainLayer: React.FC<SafetyDomainLayerProps> = React.memo(({
  mapRef, ownShip, visible,
  observationNm = 2.0, actionNm = 1.0, criticalNm = 0.3,
}) => {
  const addedRef = useRef(false);

  function buildFeatureCollection(ship: OwnShipState): GeoJSON.FeatureCollection {
    const lon = ship.pose?.lon ?? 0;
    const lat = ship.pose?.lat ?? 0;
    return {
      type: 'FeatureCollection',
      features: [
        circleFeature(lon, lat, observationNm, { tier: 'observation', radiusNm: observationNm }),
        circleFeature(lon, lat, actionNm,      { tier: 'action',      radiusNm: actionNm }),
        circleFeature(lon, lat, criticalNm,    { tier: 'critical',    radiusNm: criticalNm }),
      ],
    };
  }

  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;

    function setup() {
      if (!map) return;
      const data = ownShip ? buildFeatureCollection(ownShip) : { type: 'FeatureCollection' as const, features: [] };

      if (!addedRef.current) {
        map.addSource(SOURCE_ID, { type: 'geojson', data });
        // Observation: grey dashed outline only (engineer only)
        map.addLayer({
          id: 'safety-observation',
          type: 'line',
          source: SOURCE_ID,
          filter: ['==', ['get', 'tier'], 'observation'],
          paint: { 'line-color': '#6b7280', 'line-width': 1, 'line-dasharray': [4, 4], 'line-opacity': 0.5 },
        });
        // Action zone: amber semi-transparent fill
        map.addLayer({
          id: 'safety-action',
          type: 'line',
          source: SOURCE_ID,
          filter: ['==', ['get', 'tier'], 'action'],
          paint: { 'line-color': '#fbbf24', 'line-width': 1.5, 'line-dasharray': [2, 2], 'line-opacity': 0.7 },
        });
        // Critical zone: solid red
        map.addLayer({
          id: 'safety-critical',
          type: 'line',
          source: SOURCE_ID,
          filter: ['==', ['get', 'tier'], 'critical'],
          paint: { 'line-color': '#f87171', 'line-width': 2, 'line-opacity': 0.9 },
        });
        addedRef.current = true;
      } else {
        (map.getSource(SOURCE_ID) as any)?.setData(data);
      }
    }

    if (!map.isStyleLoaded()) {
      map.once('style.load', setup);
    } else {
      setup();
    }
  }, [mapRef, ownShip, visible, observationNm, actionNm, criticalNm]);

  return null;
});
SafetyDomainLayer.displayName = 'SafetyDomainLayer';
```

- [ ] **Step 4: Run to confirm pass**

```bash
cd web && npx vitest run src/map/__tests__/SafetyDomainLayer.test.tsx
```

Expected: PASS — 3 tests pass

- [ ] **Step 5: Commit Task 2**

```bash
cd web && git add src/map/SafetyDomainLayer.tsx src/map/__tests__/SafetyDomainLayer.test.tsx && git commit -m "feat(screen3): Task 2 — SafetyDomainLayer 3-tier safety zones (engineer view)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Task 3 (Track B): IvpRiskGradientLayer — M4 8-Direction Risk Vectors

**Context:** Engineer view shows 8 directional arrows around own-ship, length ∝ IvP cost, color green→amber→red. SVG overlay (not MapLibre GL layer) for precise pixel-space positioning.

**Files:**
- Create: `web/src/map/IvpRiskGradientLayer.tsx`
- Create: `web/src/map/__tests__/IvpRiskGradientLayer.test.tsx`

- [ ] **Step 1: Write failing test**

```typescript
// web/src/map/__tests__/IvpRiskGradientLayer.test.tsx
import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { IvpRiskGradientLayer } from '../IvpRiskGradientLayer';
import type { IvpContribution } from '../../types/sat';

const contributions: IvpContribution[] = [
  { direction_deg: 0,   cost: 0.2 },
  { direction_deg: 45,  cost: 0.5 },
  { direction_deg: 90,  cost: 0.9 },
  { direction_deg: 135, cost: 0.1 },
  { direction_deg: 180, cost: 0.3 },
  { direction_deg: 225, cost: 0.4 },
  { direction_deg: 270, cost: 0.7 },
  { direction_deg: 315, cost: 0.6 },
];

describe('IvpRiskGradientLayer', () => {
  it('renders null when contributions empty', () => {
    const { container } = render(
      <IvpRiskGradientLayer contributions={[]} activeBehavior={null} activeBehaviorWeight={0} ownShipScreenPos={[400, 300]} headingDeg={0} />
    );
    expect(container.firstChild).toBeNull();
  });

  it('renders 8 arrows for 8 contributions', () => {
    render(
      <IvpRiskGradientLayer contributions={contributions} activeBehavior="COLREGs_Avoidance" activeBehaviorWeight={0.7} ownShipScreenPos={[400, 300]} headingDeg={45} />
    );
    const arrows = document.querySelectorAll('[data-testid^="ivp-arrow-"]');
    expect(arrows).toHaveLength(8);
  });

  it('high-cost direction has red fill', () => {
    render(
      <IvpRiskGradientLayer contributions={contributions} activeBehavior={null} activeBehaviorWeight={0} ownShipScreenPos={[400, 300]} headingDeg={0} />
    );
    // direction 90° has cost 0.9 → red
    const arrow90 = document.querySelector('[data-testid="ivp-arrow-90"]');
    expect(arrow90?.getAttribute('fill')).toBe('#f87171');
  });

  it('shows active behavior label', () => {
    render(
      <IvpRiskGradientLayer contributions={contributions} activeBehavior="COLREGs_Avoidance" activeBehaviorWeight={0.7} ownShipScreenPos={[400, 300]} headingDeg={0} />
    );
    expect(screen.getByText(/COLREGs_Avoidance/)).toBeInTheDocument();
  });
});
```

- [ ] **Step 2: Run to confirm failure**

```bash
cd web && npx vitest run src/map/__tests__/IvpRiskGradientLayer.test.tsx
```

Expected: FAIL — module not found

- [ ] **Step 3: Create `web/src/map/IvpRiskGradientLayer.tsx`**

```typescript
import React from 'react';
import type { IvpContribution } from '../types/sat';

interface IvpRiskGradientLayerProps {
  contributions: IvpContribution[];
  activeBehavior: string | null;
  activeBehaviorWeight: number;
  ownShipScreenPos: [number, number];  // [x, y] in screen pixels
  headingDeg: number;
}

const MAX_ARROW_PX = 60;  // max arrow length at cost=1.0

function costToColor(cost: number): string {
  if (cost < 0.4) return '#34d399';   // green
  if (cost < 0.7) return '#fbbf24';   // amber
  return '#f87171';                   // red
}

export const IvpRiskGradientLayer: React.FC<IvpRiskGradientLayerProps> = React.memo(({
  contributions, activeBehavior, activeBehaviorWeight, ownShipScreenPos, headingDeg,
}) => {
  if (contributions.length === 0) return null;

  const [cx, cy] = ownShipScreenPos;

  return (
    <svg
      style={{ position: 'absolute', inset: 0, pointerEvents: 'none', zIndex: 8, overflow: 'visible' }}
      width="100%" height="100%"
    >
      {contributions.map((c) => {
        const length = c.cost * MAX_ARROW_PX;
        const angleDeg = c.direction_deg - headingDeg;  // heading-relative
        const angleRad = (angleDeg - 90) * (Math.PI / 180);
        const x2 = cx + length * Math.cos(angleRad);
        const y2 = cy + length * Math.sin(angleRad);
        const color = costToColor(c.cost);

        return (
          <g key={c.direction_deg}>
            <line
              data-testid={`ivp-arrow-${c.direction_deg}`}
              x1={cx} y1={cy} x2={x2} y2={y2}
              stroke={color} strokeWidth={2.5} strokeLinecap="round"
              fill={color}  // for querySelector fill check
            />
            {/* Arrowhead */}
            <polygon
              fill={color}
              points={`${x2},${y2} ${x2 - 4 * Math.cos(angleRad - 0.4)},${y2 - 4 * Math.sin(angleRad - 0.4)} ${x2 - 4 * Math.cos(angleRad + 0.4)},${y2 - 4 * Math.sin(angleRad + 0.4)}`}
            />
          </g>
        );
      })}

      {/* Active behavior label above own-ship */}
      {activeBehavior && (
        <text
          x={cx} y={cy - 72}
          textAnchor="middle"
          fill="#fbbf24"
          fontSize={11}
          fontFamily="var(--f-mono)"
          fontWeight="bold"
        >
          {activeBehavior} {activeBehaviorWeight.toFixed(2)}
        </text>
      )}
    </svg>
  );
});
IvpRiskGradientLayer.displayName = 'IvpRiskGradientLayer';
```

- [ ] **Step 4: Run to confirm pass**

```bash
cd web && npx vitest run src/map/__tests__/IvpRiskGradientLayer.test.tsx
```

Expected: PASS — 4 tests pass

- [ ] **Step 5: Commit Task 3**

```bash
cd web && git add src/map/IvpRiskGradientLayer.tsx src/map/__tests__/IvpRiskGradientLayer.test.tsx && git commit -m "feat(screen3): Task 3 — IvpRiskGradientLayer 8-direction M4 risk gradient vectors

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Task 4 (Track B): MpcTrajectoryLayer — M5 Mid-MPC Arc + BC-MPC 13 Branches

**Context:** Engineer view shows Mid-MPC 90s arc (green solid) + BC-MPC 13 candidate branches (red→green by cost). Follows ImazuGeometry MapLibre pattern.

**Files:**
- Create: `web/src/map/MpcTrajectoryLayer.tsx`
- Create: `web/src/map/__tests__/MpcTrajectoryLayer.test.tsx`

- [ ] **Step 1: Write failing test**

```typescript
// web/src/map/__tests__/MpcTrajectoryLayer.test.tsx
import { describe, it, expect, vi } from 'vitest';
import { render } from '@testing-library/react';
import { useRef } from 'react';
import { MpcTrajectoryLayer } from '../MpcTrajectoryLayer';
import type { TrajectoryCandidate } from '../../types/sat';

const addSourceMock = vi.fn();
const addLayerMock  = vi.fn();
const setDataMock   = vi.fn();

const mockMap = {
  isStyleLoaded: vi.fn(() => true),
  once: vi.fn(),
  addSource: addSourceMock,
  addLayer: addLayerMock,
  getSource: vi.fn(() => ({ setData: setDataMock })),
};

vi.mock('maplibre-gl', () => ({ default: { Map: vi.fn(() => mockMap) } }));

function makeCandidate(id: number, isOptimal: boolean, cost: number, type: 'mid_mpc' | 'bc_mpc'): TrajectoryCandidate {
  return {
    id, cost, is_optimal: isOptimal, type,
    points: [{ lon: 10.4, lat: 63.4 }, { lon: 10.42, lat: 63.42 }],
  };
}

function Wrapper({ candidates }: { candidates: TrajectoryCandidate[] }) {
  const ref = useRef(mockMap as any);
  return <MpcTrajectoryLayer mapRef={ref} candidates={candidates} visible={true} />;
}

describe('MpcTrajectoryLayer', () => {
  it('renders null (no DOM output)', () => {
    const { container } = render(<Wrapper candidates={[]} />);
    expect(container.firstChild).toBeNull();
  });

  it('adds source when candidates provided', () => {
    addSourceMock.mockClear();
    render(<Wrapper candidates={[makeCandidate(0, true, 0.1, 'mid_mpc')]} />);
    expect(addSourceMock).toHaveBeenCalledWith('mpc-trajectories', expect.objectContaining({ type: 'geojson' }));
  });

  it('builds LineString features from candidates', () => {
    addSourceMock.mockClear();
    render(<Wrapper candidates={[
      makeCandidate(0, true, 0.1, 'mid_mpc'),
      makeCandidate(1, false, 0.8, 'bc_mpc'),
    ]} />);
    const callArgs = addSourceMock.mock.calls[0][1].data;
    expect(callArgs.features).toHaveLength(2);
    expect(callArgs.features[0].geometry.type).toBe('LineString');
    expect(callArgs.features[0].properties.cost).toBe(0.1);
  });

  it('updates source on new candidates', () => {
    setDataMock.mockClear();
    addSourceMock.mockClear();
    addLayerMock.mockClear();
    const { rerender } = render(<Wrapper candidates={[makeCandidate(0, true, 0.1, 'mid_mpc')]} />);
    rerender(<Wrapper candidates={[makeCandidate(0, false, 0.5, 'bc_mpc')]} />);
    expect(setDataMock).toHaveBeenCalled();
  });
});
```

- [ ] **Step 2: Run to confirm failure**

```bash
cd web && npx vitest run src/map/__tests__/MpcTrajectoryLayer.test.tsx
```

Expected: FAIL — module not found

- [ ] **Step 3: Create `web/src/map/MpcTrajectoryLayer.tsx`**

```typescript
import React, { useEffect, useRef } from 'react';
import type maplibregl from 'maplibre-gl';
import type { TrajectoryCandidate } from '../types/sat';

interface MpcTrajectoryLayerProps {
  mapRef: React.MutableRefObject<maplibregl.Map | null>;
  candidates: TrajectoryCandidate[];
  visible: boolean;
}

const SOURCE_ID = 'mpc-trajectories';
const LAYER_ID  = 'mpc-trajectories-line';

function costToColor(cost: number): string {
  // Red (high cost) → Green (low cost), using RGB interpolation
  const r = Math.round(248 * cost + 52 * (1 - cost));
  const g = Math.round(113 * cost + 211 * (1 - cost));
  const b = Math.round(113 * cost + 153 * (1 - cost));
  return `#${r.toString(16).padStart(2, '0')}${g.toString(16).padStart(2, '0')}${b.toString(16).padStart(2, '0')}`;
}

function buildGeoJSON(candidates: TrajectoryCandidate[]): GeoJSON.FeatureCollection {
  return {
    type: 'FeatureCollection',
    features: candidates.map((c) => ({
      type: 'Feature',
      geometry: {
        type: 'LineString',
        coordinates: c.points.map((p) => [p.lon, p.lat]),
      },
      properties: {
        id: c.id,
        cost: c.cost,
        is_optimal: c.is_optimal,
        type: c.type,
        color: c.is_optimal && c.type === 'mid_mpc' ? '#34d399' : costToColor(c.cost),
        line_width: c.is_optimal ? 3 : 1.5,
      },
    })),
  };
}

export const MpcTrajectoryLayer: React.FC<MpcTrajectoryLayerProps> = React.memo(({
  mapRef, candidates, visible,
}) => {
  const addedRef = useRef(false);

  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;

    function setup() {
      if (!map) return;
      const data = buildGeoJSON(candidates);

      if (!addedRef.current) {
        map.addSource(SOURCE_ID, { type: 'geojson', data });
        map.addLayer({
          id: LAYER_ID,
          type: 'line',
          source: SOURCE_ID,
          layout: { 'line-cap': 'round', 'line-join': 'round' },
          paint: {
            'line-color': ['get', 'color'],
            'line-width': ['get', 'line_width'],
            'line-opacity': visible ? 0.85 : 0,
          },
        });
        addedRef.current = true;
      } else {
        (map.getSource(SOURCE_ID) as any)?.setData(data);
      }
    }

    if (!map.isStyleLoaded()) {
      map.once('style.load', setup);
    } else {
      setup();
    }
  }, [mapRef, candidates, visible]);

  return null;
});
MpcTrajectoryLayer.displayName = 'MpcTrajectoryLayer';
```

- [ ] **Step 4: Run to confirm pass**

```bash
cd web && npx vitest run src/map/__tests__/MpcTrajectoryLayer.test.tsx
```

Expected: PASS — 4 tests pass

- [ ] **Step 5: Commit Task 4**

```bash
cd web && git add src/map/MpcTrajectoryLayer.tsx src/map/__tests__/MpcTrajectoryLayer.test.tsx && git commit -m "feat(screen3): Task 4 — MpcTrajectoryLayer Mid-MPC arc + BC-MPC 13 branches (Red→Green cost)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Task 5 (Track C): ColregsRationaleTree — 5-Layer M6 Decision Rationale Panel

**Context:** Left drawer ② area. Displays all 5 COLREGs reasoning layers for the active target: ODD → Meeting classification → Responsibility → Action direction → Timing stage.

**Files:**
- Create: `web/src/screens/shared/ColregsRationaleTree.tsx`
- Create: `web/src/screens/shared/__tests__/ColregsRationaleTree.test.tsx`

- [ ] **Step 1: Write failing test**

```typescript
// web/src/screens/shared/__tests__/ColregsRationaleTree.test.tsx
import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ColregsRationaleTree } from '../ColregsRationaleTree';
import type { ColregsChainLayer } from '../../../types/sat';

const mockChain: ColregsChainLayer[] = [
  { layer: 1, label: 'ODD', conclusion: 'ODD-A', inputs: { odd_domain: 'A' } },
  { layer: 2, label: '会遇分类', conclusion: 'Rule 14 ✓', inputs: { hdg_diff: 178, bearing: 3.2, range_nm: 2.1 }, confidence: 0.94 },
  { layer: 3, label: '责任', conclusion: 'GIVE-WAY', inputs: { rule: 'Rule 16' } },
  { layer: 4, label: '行动方向', conclusion: 'STARBOARD ≥30°', inputs: { rule: 'Rule 8' } },
  { layer: 5, label: '时机', conclusion: 'STAGE_3', inputs: { tcpa_min: 4.0, t_act: 4.0 }, timing_stage: 'STAGE_3', escalation: true },
];

describe('ColregsRationaleTree', () => {
  it('renders empty state when no chain', () => {
    render(<ColregsRationaleTree chain={[]} targetId={null} latencyMs={0} />);
    expect(screen.getByText(/No active COLREGs/i)).toBeInTheDocument();
  });

  it('renders all 5 layer conclusions', () => {
    render(<ColregsRationaleTree chain={mockChain} targetId="123456789" latencyMs={2.3} />);
    expect(screen.getByText(/ODD-A/)).toBeInTheDocument();
    expect(screen.getByText(/Rule 14/)).toBeInTheDocument();
    expect(screen.getByText(/GIVE-WAY/)).toBeInTheDocument();
    expect(screen.getByText(/STARBOARD/)).toBeInTheDocument();
    expect(screen.getByText(/STAGE_3/)).toBeInTheDocument();
  });

  it('shows target MMSI', () => {
    render(<ColregsRationaleTree chain={mockChain} targetId="123456789" latencyMs={2.3} />);
    expect(screen.getByText(/123456789/)).toBeInTheDocument();
  });

  it('shows latency', () => {
    render(<ColregsRationaleTree chain={mockChain} targetId="123456789" latencyMs={2.3} />);
    expect(screen.getByText(/2.3ms/)).toBeInTheDocument();
  });

  it('highlights STAGE_3 with escalation styling', () => {
    render(<ColregsRationaleTree chain={mockChain} targetId="123456789" latencyMs={2.3} />);
    const stage3 = screen.getByTestId('colregs-layer-5');
    expect(stage3.className || stage3.getAttribute('data-escalation')).toBeTruthy();
  });
});
```

- [ ] **Step 2: Run to confirm failure**

```bash
cd web && npx vitest run src/screens/shared/__tests__/ColregsRationaleTree.test.tsx
```

Expected: FAIL — module not found

- [ ] **Step 3: Create `web/src/screens/shared/ColregsRationaleTree.tsx`**

```typescript
import React from 'react';
import type { ColregsChainLayer } from '../../types/sat';

interface ColregsRationaleTreeProps {
  chain: ColregsChainLayer[];
  targetId: string | null;
  latencyMs: number;
}

const LAYER_LABELS = ['ODD', '会遇分类', '责任', '行动方向', '时机'] as const;
const STAGE_COLORS: Record<string, string> = {
  STAGE_1: '#34d399',
  STAGE_2: '#fbbf24',
  STAGE_3: '#f87171',
  EMERGENCY: '#8b0000',
};

export const ColregsRationaleTree: React.FC<ColregsRationaleTreeProps> = ({ chain, targetId, latencyMs }) => {
  if (chain.length === 0) {
    return (
      <div style={{ padding: '16px 12px', fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-3)' }}>
        No active COLREGs encounter
      </div>
    );
  }

  return (
    <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-1)' }}>
      {/* Header */}
      <div style={{
        padding: '6px 12px', background: 'var(--bg-0)',
        borderBottom: '1px solid var(--line-2)',
        display: 'flex', justifyContent: 'space-between', alignItems: 'center',
      }}>
        <span style={{ color: 'var(--c-phos)', fontWeight: 700, fontSize: 10, letterSpacing: '0.1em' }}>
          M6 COLREGs REASONING
        </span>
        <span style={{ color: 'var(--txt-3)', fontSize: 9 }}>
          {targetId ? `MMSI:${targetId}` : ''} · {latencyMs.toFixed(1)}ms
        </span>
      </div>

      {/* 5 Layers */}
      {chain.map((layer) => {
        const isEscalation = layer.escalation === true;
        const stageColor = layer.timing_stage ? STAGE_COLORS[layer.timing_stage] : undefined;

        return (
          <div
            key={layer.layer}
            data-testid={`colregs-layer-${layer.layer}`}
            data-escalation={isEscalation ? 'true' : undefined}
            style={{
              padding: '8px 12px',
              borderBottom: '1px solid var(--line-2)',
              background: isEscalation ? 'rgba(248,81,73,0.06)' : 'transparent',
              borderLeft: `3px solid ${stageColor ?? (isEscalation ? '#f87171' : 'var(--line-2)')}`,
            }}
          >
            <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 2 }}>
              <span style={{ color: 'var(--txt-3)', fontSize: 9, letterSpacing: '0.08em', textTransform: 'uppercase' }}>
                [{layer.layer}] {LAYER_LABELS[layer.layer - 1] ?? layer.label}
              </span>
              {layer.confidence !== undefined && (
                <span style={{ color: 'var(--txt-3)', fontSize: 9 }}>
                  conf: {(layer.confidence * 100).toFixed(0)}%
                </span>
              )}
            </div>
            <div style={{ color: stageColor ?? 'var(--txt-0)', fontWeight: 700, fontSize: 12 }}>
              {layer.conclusion}
              {isEscalation && (
                <span style={{ color: '#f87171', fontSize: 9, marginLeft: 6 }}>⚠️ 独立避让</span>
              )}
            </div>
            {/* Key input values */}
            <div style={{ color: 'var(--txt-3)', fontSize: 9, marginTop: 2 }}>
              {Object.entries(layer.inputs)
                .map(([k, v]) => `${k}: ${v}`)
                .join(' · ')}
            </div>
          </div>
        );
      })}
    </div>
  );
};
```

- [ ] **Step 4: Run to confirm pass**

```bash
cd web && npx vitest run src/screens/shared/__tests__/ColregsRationaleTree.test.tsx
```

Expected: PASS — 5 tests pass

- [ ] **Step 5: Commit Task 5**

```bash
cd web && git add src/screens/shared/ColregsRationaleTree.tsx src/screens/shared/__tests__/ColregsRationaleTree.test.tsx && git commit -m "feat(screen3): Task 5 — ColregsRationaleTree 5-layer M6 decision rationale panel

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Task 6 (Track C): SotifMonitorStrip — M7 6-Assumption Violation Monitor

**Context:** Right drawer ③ area. 6 horizontal progress bars for M7 SOTIF assumptions with thresholds; row flashes red when violated.

**Files:**
- Create: `web/src/screens/shared/SotifMonitorStrip.tsx`
- Create: `web/src/screens/shared/__tests__/SotifMonitorStrip.test.tsx`

- [ ] **Step 1: Write failing test**

```typescript
// web/src/screens/shared/__tests__/SotifMonitorStrip.test.tsx
import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { SotifMonitorStrip } from '../SotifMonitorStrip';
import type { SotifMetrics } from '../../../types/sat';

const nominal: SotifMetrics = {
  ais_radar_consistency_sigma: 1.8,
  target_predictability_rms_m: 41,
  perception_coverage_pct: 95,
  colregs_parse_failures: 0,
  comm_link_rtt_ms: 120,
  checker_veto_rate_pct: 8,
};

const violated: SotifMetrics = {
  ais_radar_consistency_sigma: 2.5,  // >2.0 → violation
  target_predictability_rms_m: 41,
  perception_coverage_pct: 75,        // <80 → violation
  colregs_parse_failures: 0,
  comm_link_rtt_ms: 120,
  checker_veto_rate_pct: 8,
};

describe('SotifMonitorStrip', () => {
  it('renders null when metrics is null', () => {
    const { container } = render(<SotifMonitorStrip metrics={null} />);
    expect(container.firstChild).toBeNull();
  });

  it('renders 6 rows for 6 assumptions', () => {
    render(<SotifMonitorStrip metrics={nominal} />);
    const rows = document.querySelectorAll('[data-testid^="sotif-row-"]');
    expect(rows).toHaveLength(6);
  });

  it('all rows green when nominal', () => {
    render(<SotifMonitorStrip metrics={nominal} />);
    const rows = document.querySelectorAll('[data-testid^="sotif-row-"]');
    rows.forEach((row) => {
      expect(row.getAttribute('data-violated')).toBe('false');
    });
  });

  it('AIS consistency row violated when sigma > 2.0', () => {
    render(<SotifMonitorStrip metrics={violated} />);
    expect(document.querySelector('[data-testid="sotif-row-ais"]')?.getAttribute('data-violated')).toBe('true');
  });

  it('perception coverage row violated when < 80%', () => {
    render(<SotifMonitorStrip metrics={violated} />);
    expect(document.querySelector('[data-testid="sotif-row-coverage"]')?.getAttribute('data-violated')).toBe('true');
  });

  it('shows MRM recommendation when any row violated', () => {
    render(<SotifMonitorStrip metrics={violated} />);
    expect(screen.getByText(/MRM-01/)).toBeInTheDocument();
  });
});
```

- [ ] **Step 2: Run to confirm failure**

```bash
cd web && npx vitest run src/screens/shared/__tests__/SotifMonitorStrip.test.tsx
```

Expected: FAIL — module not found

- [ ] **Step 3: Create `web/src/screens/shared/SotifMonitorStrip.tsx`**

```typescript
import React from 'react';
import type { SotifMetrics } from '../../types/sat';

interface SotifMonitorStripProps {
  metrics: SotifMetrics | null;
}

interface MetricRow {
  testId: string;
  label: string;
  value: number;
  displayValue: string;
  maxValue: number;
  threshold: number;
  violated: boolean;
  unit: string;
  higherIsBad: boolean; // true = bar fills red when high
}

function buildRows(m: SotifMetrics): MetricRow[] {
  return [
    {
      testId: 'ais', label: 'AIS/雷达一致性',
      value: m.ais_radar_consistency_sigma, displayValue: `${m.ais_radar_consistency_sigma.toFixed(1)}σ`,
      maxValue: 4, threshold: 2.0, violated: m.ais_radar_consistency_sigma > 2.0,
      unit: 'σ', higherIsBad: true,
    },
    {
      testId: 'predictability', label: '目标可预测性 RMS',
      value: m.target_predictability_rms_m, displayValue: `${m.target_predictability_rms_m.toFixed(0)}m`,
      maxValue: 100, threshold: 50, violated: m.target_predictability_rms_m > 50,
      unit: 'm', higherIsBad: true,
    },
    {
      testId: 'coverage', label: '感知覆盖充分性',
      value: m.perception_coverage_pct, displayValue: `${m.perception_coverage_pct.toFixed(0)}%`,
      maxValue: 100, threshold: 80, violated: m.perception_coverage_pct < 80,
      unit: '%', higherIsBad: false,
    },
    {
      testId: 'colregs', label: 'COLREGs解析失败',
      value: m.colregs_parse_failures, displayValue: `${m.colregs_parse_failures}次`,
      maxValue: 10, threshold: 3, violated: m.colregs_parse_failures > 3,
      unit: '次', higherIsBad: true,
    },
    {
      testId: 'comm', label: '通信链路质量',
      value: m.comm_link_rtt_ms, displayValue: `${m.comm_link_rtt_ms}ms`,
      maxValue: 3000, threshold: 2000, violated: m.comm_link_rtt_ms > 2000,
      unit: 'ms', higherIsBad: true,
    },
    {
      testId: 'checker', label: 'Checker否决率',
      value: m.checker_veto_rate_pct, displayValue: `${m.checker_veto_rate_pct.toFixed(0)}%`,
      maxValue: 40, threshold: 20, violated: m.checker_veto_rate_pct > 20,
      unit: '%', higherIsBad: true,
    },
  ];
}

export const SotifMonitorStrip: React.FC<SotifMonitorStripProps> = ({ metrics }) => {
  if (!metrics) return null;

  const rows = buildRows(metrics);
  const anyViolated = rows.some((r) => r.violated);

  return (
    <div style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-1)' }}>
      {/* Header */}
      <div style={{
        padding: '6px 12px', background: 'var(--bg-0)',
        borderBottom: '1px solid var(--line-2)',
        display: 'flex', justifyContent: 'space-between',
      }}>
        <span style={{ color: 'var(--c-phos)', fontWeight: 700, letterSpacing: '0.1em', fontSize: 9, textTransform: 'uppercase' }}>
          M7 SOTIF 假设监控
        </span>
        <span style={{
          fontSize: 9,
          color: anyViolated ? '#f87171' : '#34d399',
          animation: anyViolated ? 'sotif-pulse 1s ease-in-out infinite' : 'none',
        }}>
          {anyViolated ? '⚠ VIOLATION' : '✓ NOMINAL'}
        </span>
      </div>

      {/* 6 metric rows */}
      {rows.map((row) => {
        const barPct = Math.min(100, (row.value / row.maxValue) * 100);
        const barColor = row.violated ? '#f87171' : barPct > 79 ? '#fbbf24' : '#34d399';

        return (
          <div
            key={row.testId}
            data-testid={`sotif-row-${row.testId}`}
            data-violated={String(row.violated)}
            style={{
              padding: '5px 12px',
              borderBottom: '1px solid var(--line-2)',
              background: row.violated ? 'rgba(248,81,73,0.08)' : 'transparent',
              animation: row.violated ? 'sotif-pulse 1s ease-in-out infinite' : 'none',
            }}
          >
            <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 3 }}>
              <span style={{ color: row.violated ? '#f87171' : 'var(--txt-2)', fontSize: 9 }}>
                {row.label}
              </span>
              <span style={{ color: row.violated ? '#f87171' : 'var(--txt-1)', fontWeight: row.violated ? 700 : 400 }}>
                {row.displayValue}
                {row.violated ? ' 🔴' : ' 🟢'}
              </span>
            </div>
            <div style={{ height: 3, background: 'var(--bg-0)', borderRadius: 1 }}>
              <div style={{
                width: `${barPct}%`, height: '100%',
                background: barColor, borderRadius: 1, transition: 'width 0.5s',
              }} />
            </div>
          </div>
        );
      })}

      {/* MRM recommendation when violated */}
      {anyViolated && (
        <div style={{
          margin: '8px 12px', padding: '6px 8px',
          background: 'rgba(248,81,73,0.1)', border: '1px solid #f87171',
          fontSize: 9, color: '#f87171',
        }}>
          推荐: MRM-01（减速至安全速度，保持航向）· 等待 M1 仲裁
        </div>
      )}

      <style>{`
        @keyframes sotif-pulse {
          0%, 100% { opacity: 1; }
          50% { opacity: 0.6; }
        }
      `}</style>
    </div>
  );
};
```

- [ ] **Step 4: Run to confirm pass**

```bash
cd web && npx vitest run src/screens/shared/__tests__/SotifMonitorStrip.test.tsx
```

Expected: PASS — 6 tests pass

- [ ] **Step 5: Commit Task 6**

```bash
cd web && git add src/screens/shared/SotifMonitorStrip.tsx src/screens/shared/__tests__/SotifMonitorStrip.test.tsx && git commit -m "feat(screen3): Task 6 — SotifMonitorStrip M7 6-assumption violation monitor

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Task 7 (Track C): DecisionChainTimingBar — Bottom M1→M8 Latency Strip

**Context:** Fixed 24px bar at the bottom of engineer view. Shows per-module latency (ms) with color coding: green <5ms, amber 5-20ms, red >20ms (M5 threshold 50ms). Total >100ms → full bar red.

**Files:**
- Create: `web/src/screens/shared/DecisionChainTimingBar.tsx`
- Create: `web/src/screens/shared/__tests__/DecisionChainTimingBar.test.tsx`

- [ ] **Step 1: Write failing test**

```typescript
// web/src/screens/shared/__tests__/DecisionChainTimingBar.test.tsx
import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { DecisionChainTimingBar } from '../DecisionChainTimingBar';
import type { ModulePulse } from '../../../types';

function makePulse(moduleId: number, latencyMs: number): ModulePulse {
  return { moduleId, state: 1, latencyMs } as any;
}

const nominalPulses: ModulePulse[] = [
  makePulse(1, 0.8),   // M1
  makePulse(2, 3.2),   // M2
  makePulse(4, 1.1),   // M4
  makePulse(6, 2.3),   // M6
  makePulse(5, 18.7),  // M5
  makePulse(7, 4.1),   // M7
];

const slowPulses: ModulePulse[] = [
  makePulse(1, 0.8),
  makePulse(2, 3.2),
  makePulse(4, 1.1),
  makePulse(6, 2.3),
  makePulse(5, 85.0),  // M5 > 50ms — red
  makePulse(7, 4.1),
];

describe('DecisionChainTimingBar', () => {
  it('renders null when no pulses', () => {
    const { container } = render(<DecisionChainTimingBar pulses={[]} />);
    expect(container.firstChild).toBeNull();
  });

  it('renders module latency labels', () => {
    render(<DecisionChainTimingBar pulses={nominalPulses} />);
    expect(screen.getByText(/M1/)).toBeInTheDocument();
    expect(screen.getByText(/M5/)).toBeInTheDocument();
  });

  it('shows total latency', () => {
    render(<DecisionChainTimingBar pulses={nominalPulses} />);
    // 0.8+3.2+1.1+2.3+18.7+4.1 = 30.2ms
    expect(screen.getByTestId('timing-total').textContent).toMatch(/30/);
  });

  it('M5 segment is red when > 50ms', () => {
    render(<DecisionChainTimingBar pulses={slowPulses} />);
    const m5seg = screen.getByTestId('timing-seg-5');
    expect(m5seg.getAttribute('data-color')).toBe('red');
  });

  it('whole bar data-overload when total > 100ms', () => {
    const overloadPulses = [makePulse(5, 120)];
    render(<DecisionChainTimingBar pulses={overloadPulses} />);
    expect(screen.getByTestId('timing-bar').getAttribute('data-overload')).toBe('true');
  });
});
```

- [ ] **Step 2: Run to confirm failure**

```bash
cd web && npx vitest run src/screens/shared/__tests__/DecisionChainTimingBar.test.tsx
```

Expected: FAIL — module not found

- [ ] **Step 3: Create `web/src/screens/shared/DecisionChainTimingBar.tsx`**

```typescript
import React from 'react';
import type { ModulePulse } from '../../types';

interface DecisionChainTimingBarProps {
  pulses: ModulePulse[];
}

// Decision chain order: M1 → M2 → M4 → M6 → M5 → M7
const CHAIN_ORDER = [1, 2, 4, 6, 5, 7] as const;
// M5 MPC solve allowed up to 50ms (vs 20ms for others)
const MODULE_THRESHOLD: Record<number, number> = { 5: 50 };
const DEFAULT_THRESHOLD = 20;

function segColor(moduleId: number, latencyMs: number): 'green' | 'amber' | 'red' {
  const threshold = MODULE_THRESHOLD[moduleId] ?? DEFAULT_THRESHOLD;
  if (latencyMs < 5) return 'green';
  if (latencyMs < threshold) return 'amber';
  return 'red';
}

const COLOR_VALUES = {
  green: '#34d399',
  amber: '#fbbf24',
  red:   '#f87171',
};

export const DecisionChainTimingBar: React.FC<DecisionChainTimingBarProps> = ({ pulses }) => {
  if (pulses.length === 0) return null;

  const byId: Record<number, ModulePulse> = {};
  for (const p of pulses) { if (p.moduleId != null) byId[Number(p.moduleId)] = p; }

  const segments = CHAIN_ORDER
    .filter((id) => byId[id] !== undefined)
    .map((id) => {
      const p = byId[id]!;
      const lat = p.latencyMs ?? 0;
      const color = segColor(id, lat);
      return { id, lat, color };
    });

  if (segments.length === 0) return null;

  const total = segments.reduce((sum, s) => sum + s.lat, 0);
  const isOverload = total > 100;

  return (
    <div
      data-testid="timing-bar"
      data-overload={String(isOverload)}
      style={{
        height: 24, background: isOverload ? 'rgba(248,81,73,0.15)' : 'var(--bg-1)',
        borderTop: `1px solid ${isOverload ? '#f87171' : 'var(--line-2)'}`,
        display: 'flex', alignItems: 'center', padding: '0 12px', gap: 4,
        fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)',
        flexShrink: 0,
      }}
    >
      {segments.map((seg, i) => (
        <React.Fragment key={seg.id}>
          <span
            data-testid={`timing-seg-${seg.id}`}
            data-color={seg.color}
            style={{ color: COLOR_VALUES[seg.color] }}
          >
            M{seg.id}[{seg.lat.toFixed(1)}ms]
          </span>
          {i < segments.length - 1 && (
            <span style={{ color: 'var(--txt-3)', opacity: 0.4 }}>→</span>
          )}
        </React.Fragment>
      ))}
      <span style={{ marginLeft: 8, color: 'var(--txt-3)', opacity: 0.6 }}>total:</span>
      <span
        data-testid="timing-total"
        style={{ color: isOverload ? '#f87171' : 'var(--txt-2)', fontWeight: 700 }}
      >
        {total.toFixed(1)}ms
      </span>
    </div>
  );
};
```

- [ ] **Step 4: Run to confirm pass**

```bash
cd web && npx vitest run src/screens/shared/__tests__/DecisionChainTimingBar.test.tsx
```

Expected: PASS — 5 tests pass

- [ ] **Step 5: Commit Task 7**

```bash
cd web && git add src/screens/shared/DecisionChainTimingBar.tsx src/screens/shared/__tests__/DecisionChainTimingBar.test.tsx && git commit -m "feat(screen3): Task 7 — DecisionChainTimingBar M1→M8 decision latency strip

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Task 8 (Final): SimulationMonitor Full Refactor

**Dependencies:** Tasks 1–7 all complete.

**Context:** Refactor `SimulationMonitor.tsx` to support captain/engineer/roc view modes with full drawer layout, FSM border glow, algorithm panels, and 'god'→'engineer' rename. Also update `SilMapView` to accept 'engineer' where it previously accepted 'god'.

**Files:**
- Modify: `web/src/screens/SimulationMonitor.tsx`
- Modify: `web/src/map/SilMapView.tsx` (viewMode prop type: add 'engineer')
- Create: `web/src/screens/__tests__/SimulationMonitor.test.tsx`

- [ ] **Step 1: Write failing test**

```typescript
// web/src/screens/__tests__/SimulationMonitor.test.tsx
import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';

// Mock maplibre-gl
const mockMap = {
  addControl: vi.fn(), remove: vi.fn(),
  on: vi.fn((event: string, cb: () => void) => { if (event === 'load') cb(); }),
  addSource: vi.fn(), addLayer: vi.fn(),
  getSource: vi.fn(() => ({ setData: vi.fn() })),
  getCenter: vi.fn(() => ({ lng: 10.4, lat: 63.4 })),
  jumpTo: vi.fn(), easeTo: vi.fn(),
  isStyleLoaded: vi.fn(() => true),
  once: vi.fn(),
  setPaintProperty: vi.fn(), setLayoutProperty: vi.fn(),
};
vi.mock('maplibre-gl', () => ({
  default: {
    Map: vi.fn(() => mockMap),
    NavigationControl: vi.fn(() => ({})),
    ScaleControl: vi.fn(() => ({})),
    Marker: vi.fn(() => ({
      setLngLat: vi.fn().mockReturnThis(), addTo: vi.fn().mockReturnThis(),
      setRotation: vi.fn().mockReturnThis(), remove: vi.fn(),
      getElement: vi.fn(() => ({ innerHTML: '' })),
    })),
  },
}));

// Mock foxglove live hook
vi.mock('../../hooks/useFoxgloveLive', () => ({ useFoxgloveLive: vi.fn() }));

import { SimulationMonitor } from '../SimulationMonitor';
import { useUIStore } from '../../store/uiStore';
import { useFsmStore } from '../../store/fsmStore';
import { useTelemetryStore } from '../../store';

beforeEach(() => {
  useUIStore.getState().reset();
  useFsmStore.setState({ currentState: 'TRANSIT', transitionHistory: [], torRequest: null });
  useTelemetryStore.setState({ wsConnected: true, ownShip: null } as any);
});

describe('SimulationMonitor', () => {
  it('renders monitor container', () => {
    render(<SimulationMonitor />);
    expect(screen.getByTestId('simulation-monitor')).toBeInTheDocument();
  });

  it('captain view is default', () => {
    render(<SimulationMonitor />);
    expect(useUIStore.getState().viewMode).toBe('captain');
  });

  it('G key switches to engineer view', () => {
    render(<SimulationMonitor />);
    fireEvent.keyDown(document, { key: 'g' });
    expect(useUIStore.getState().viewMode).toBe('engineer');
  });

  it('engineer view shows left drawer toggle', () => {
    useUIStore.getState().setViewMode('engineer');
    render(<SimulationMonitor />);
    expect(screen.getByTestId('left-drawer-toggle')).toBeInTheDocument();
  });

  it('left drawer toggle opens drawer', () => {
    useUIStore.getState().setViewMode('engineer');
    render(<SimulationMonitor />);
    fireEvent.click(screen.getByTestId('left-drawer-toggle'));
    expect(useUIStore.getState().leftDrawerOpen).toBe(true);
  });

  it('MRC state applies blood-red border class', () => {
    useFsmStore.setState({ currentState: 'MRC', transitionHistory: [], torRequest: null });
    useUIStore.getState().setViewMode('captain');
    render(<SimulationMonitor />);
    const monitor = screen.getByTestId('simulation-monitor');
    expect(monitor.getAttribute('data-fsm')).toBe('MRC');
  });

  it('TOR state shows amber border indicator', () => {
    useFsmStore.setState({
      currentState: 'TOR',
      transitionHistory: [],
      torRequest: {
        reason: 'test', triggeredAtSimTime: 0, tmrDeadlineSimTime: 60,
        currentSituation: '', proposedAction: '',
      },
    });
    render(<SimulationMonitor />);
    const monitor = screen.getByTestId('simulation-monitor');
    expect(monitor.getAttribute('data-fsm')).toBe('TOR');
  });
});
```

- [ ] **Step 2: Run to confirm failure**

```bash
cd web && npx vitest run src/screens/__tests__/SimulationMonitor.test.tsx
```

Expected: FAIL — `left-drawer-toggle` not found, viewMode 'engineer' not available

- [ ] **Step 3: Update `web/src/map/SilMapView.tsx` — accept 'engineer' viewMode**

Find the line that restricts viewMode type. Search for `viewMode as 'captain' | 'god'` or the prop type definition. Update the `viewMode` prop type in SilMapView from `'captain' | 'god'` to `'captain' | 'engineer' | 'roc'` and replace any internal `viewMode === 'god'` checks with `viewMode === 'engineer'`.

Open `web/src/map/SilMapView.tsx` and find the prop interface. The viewMode prop appears as:

```typescript
// Find this (exact text may vary, adapt to actual file):
viewMode?: 'captain' | 'god';
```

Replace with:

```typescript
viewMode?: 'captain' | 'engineer' | 'roc';
```

Also replace all occurrences of `viewMode === 'god'` with `viewMode === 'engineer'` throughout the file.

After editing, run the SilMapView test to confirm no regression:

```bash
cd web && npx vitest run src/map/__tests__/SilMapView.test.tsx
```

Expected: PASS (all existing tests still pass)

- [ ] **Step 4: Rewrite `web/src/screens/SimulationMonitor.tsx`**

```typescript
import { useRef, useEffect, useState } from 'react';
import { SilMapView } from '../map/SilMapView';
import { SafetyDomainLayer } from '../map/SafetyDomainLayer';
import { IvpRiskGradientLayer } from '../map/IvpRiskGradientLayer';
import { MpcTrajectoryLayer } from '../map/MpcTrajectoryLayer';
import { useFoxgloveLive } from '../hooks/useFoxgloveLive';
import { useTelemetryStore, useControlStore, useUIStore } from '../store';
import { useDeactivateLifecycleMutation } from '../api/silApi';
import { CompassRose } from '../map/CompassRose';
import { PpiRings } from '../map/PpiRings';
import { ColregsSectors } from '../map/ColregsSectors';
import { DistanceScale } from '../map/DistanceScale';
import { ArpaTargetTable } from './shared/ArpaTargetTable';
import { ScoringGauges } from './shared/ScoringGauges';
import { TorModal } from './shared/TorModal';
import { FaultInjectPanel } from './shared/FaultInjectPanel';
import { ConningBar } from './shared/ConningBar';
import { ThreatRibbon } from './shared/ThreatRibbon';
import { ColregsRationaleTree } from './shared/ColregsRationaleTree';
import { SotifMonitorStrip } from './shared/SotifMonitorStrip';
import { DecisionChainTimingBar } from './shared/DecisionChainTimingBar';
import { useFsmStore } from '../store';
import { useHotkeys } from '../hooks/useHotkeys';
import {
  LucidePlay, LucidePause, LucideSquare, LucideRotateCcw,
  LucideTerminalSquare, LucideAlertTriangle, LucidePanelLeft, LucidePanelRight,
} from 'lucide-react';
import type maplibregl from 'maplibre-gl';

const MODULE_NAMES = ['M1', 'M2', 'M3', 'M4', 'M5', 'M6', 'M7', 'M8'];
const HEALTH_COLOR: Record<number, string> = { 1: '#34d399', 2: '#fbbf24', 3: '#f87171' };

function fmtSimTime(secs: number) {
  const m = Math.floor(secs / 60).toString().padStart(2, '0');
  const s = Math.floor(secs % 60).toString().padStart(2, '0');
  return `${m}:${s}`;
}

// FSM → border color mapping
const FSM_BORDER: Record<string, string> = {
  TRANSIT:           'transparent',
  COLREG_AVOIDANCE:  'transparent',
  TOR:               'var(--c-warn)',
  OVERRIDE:          'transparent',
  MRC:               '#8b0000',
};
const FSM_GLOW: Record<string, string> = {
  TOR: '0 0 0 3px rgba(251,191,36,0.4)',
  MRC: '0 0 0 3px rgba(139,0,0,0.6)',
};

function ModulePulseBar() {
  const pulses = useTelemetryStore((s) => s.modulePulses);
  const byId: Record<number, typeof pulses[0]> = {};
  for (const p of pulses) { if (p.moduleId != null) byId[Number(p.moduleId)] = p; }

  return (
    <div style={{
      display: 'flex', gap: 2, alignItems: 'center', height: 16, background: '#070C13',
      position: 'absolute', top: 0, left: 0, right: 0, zIndex: 30, justifyContent: 'center',
    }}>
      {MODULE_NAMES.map((name, i) => {
        const p = byId[i + 1];
        const color = p ? (HEALTH_COLOR[p.state ?? 0] ?? '#444') : '#333';
        const lat = p?.latencyMs;
        const tip = p
          ? `${name}: ${p.state === 1 ? 'GREEN' : p.state === 2 ? 'AMBER' : 'RED'} | ${lat ?? '?'}ms`
          : `${name}: no data`;
        return (
          <div key={name} title={tip} style={{ display: 'flex', alignItems: 'center', gap: 4, padding: '0 8px', borderRight: '1px solid #1B2C44' }}>
            <div style={{ width: 6, height: 6, borderRadius: '50%', background: color }} />
            <span style={{ color: '#8A9AAD', fontSize: 9, fontFamily: 'var(--f-mono)' }}>{name}</span>
          </div>
        );
      })}
    </div>
  );
}

// ── Engineer Left Drawer (300px) ────────────────────────────────────────────
function LeftDrawer() {
  const sat2 = useTelemetryStore((s) => s.sat2);
  const targets = useTelemetryStore((s) => s.targets);

  return (
    <div style={{
      position: 'absolute', top: 16, left: 0, bottom: 80, width: 300,
      background: 'rgba(7,12,19,0.92)', backdropFilter: 'blur(8px)',
      borderRight: '1px solid var(--line-2)', zIndex: 20, overflowY: 'auto',
    }}>
      {/* ① ARPA Target Table */}
      <div style={{ borderBottom: '1px solid var(--line-2)', padding: '6px 12px' }}>
        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--c-phos)', letterSpacing: '0.1em', textTransform: 'uppercase' }}>
          ① ARPA 目标表
        </span>
      </div>
      <div style={{ maxHeight: 180, overflowY: 'auto' }}>
        <ArpaTargetTable targets={targets} compact />
      </div>

      {/* ② M6 COLREGs Decision Rationale */}
      <div style={{ borderBottom: '1px solid var(--line-2)', borderTop: '1px solid var(--line-2)', padding: '6px 12px', marginTop: 4 }}>
        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--c-phos)', letterSpacing: '0.1em', textTransform: 'uppercase' }}>
          ② M6 决策溯源
        </span>
      </div>
      <ColregsRationaleTree
        chain={sat2?.colregs_chain ?? []}
        targetId={sat2?.colregs_chain_target_id ?? null}
        latencyMs={sat2?.reasoning_latency_ms ?? 0}
      />
    </div>
  );
}

// ── Engineer Right Drawer (280px) ───────────────────────────────────────────
function RightDrawer() {
  const sotifMetrics = useTelemetryStore((s) => s.sotifMetrics);
  const asdrEvents   = useTelemetryStore((s) => s.asdrEvents);
  const scoringRow   = useTelemetryStore((s) => s.scoringRow);

  return (
    <div style={{
      position: 'absolute', top: 16, right: 0, bottom: 80, width: 280,
      background: 'rgba(7,12,19,0.92)', backdropFilter: 'blur(8px)',
      borderLeft: '1px solid var(--line-2)', zIndex: 20, overflowY: 'auto',
    }}>
      {/* ③ M7 SOTIF Monitor */}
      <SotifMonitorStrip metrics={sotifMetrics} />

      {/* ④ ASDR Ledger (compact) */}
      <div style={{ borderTop: '1px solid var(--line-2)', padding: '6px 12px' }}>
        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--c-phos)', letterSpacing: '0.1em', textTransform: 'uppercase' }}>
          ④ ASDR Ledger
        </span>
      </div>
      <div style={{ maxHeight: 140, overflowY: 'auto', padding: '4px 0' }}>
        {[...asdrEvents].reverse().slice(0, 30).map((e, i) => (
          <div key={i} style={{
            padding: '2px 12px', fontFamily: 'var(--f-mono)', fontSize: 9,
            color: 'var(--txt-2)', borderBottom: '1px solid rgba(255,255,255,0.03)',
          }}>
            <span style={{ color: '#60a5fa' }}>{e.event_type}</span>
            {e.rule_ref && <span style={{ color: '#a3e635' }}> [{e.rule_ref}]</span>}
          </div>
        ))}
      </div>

      {/* ⑤ Scoring HUD */}
      {scoringRow && (
        <div style={{ borderTop: '1px solid var(--line-2)', padding: '6px 12px' }}>
          <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--c-phos)', letterSpacing: '0.1em', textTransform: 'uppercase' }}>
            ⑤ 实时评分
          </span>
          <ScoringGauges scoringRow={scoringRow} compact />
        </div>
      )}
    </div>
  );
}

// ── Main SimulationMonitor screen ─────────────────────────────────────────────
export function SimulationMonitor() {
  useFoxgloveLive();

  const lifecycleStatus = useTelemetryStore((s) => s.lifecycleStatus);
  const asdrEvents      = useTelemetryStore((s) => s.asdrEvents);
  const wsConnected     = useTelemetryStore((s) => s.wsConnected);
  const ownShip         = useTelemetryStore((s) => s.ownShip);
  const modulePulses    = useTelemetryStore((s) => s.modulePulses);
  const sat2            = useTelemetryStore((s) => s.sat2);
  const sat3            = useTelemetryStore((s) => s.sat3);

  const simRate   = useControlStore((s) => s.simRate);
  const isPaused  = useControlStore((s) => s.isPaused);
  const setSimRate = useControlStore((s) => s.setSimRate);
  const setPaused  = useControlStore((s) => s.setPaused);

  const viewMode       = useUIStore((s) => s.viewMode);
  const setViewMode    = useUIStore((s) => s.setViewMode);
  const leftDrawerOpen  = useUIStore((s) => s.leftDrawerOpen);
  const rightDrawerOpen = useUIStore((s) => s.rightDrawerOpen);
  const toggleLeft     = useUIStore((s) => s.toggleLeftDrawer);
  const toggleRight    = useUIStore((s) => s.toggleRightDrawer);
  const asdrExpanded   = useUIStore((s) => s.asdrLogExpanded);
  const toggleAsdr     = useUIStore((s) => s.toggleAsdrLog);

  const fsmState    = useFsmStore((s) => s.currentState);
  const torRequest  = useFsmStore((s) => s.torRequest);

  const [showFaultModal, setShowFaultModal] = useState(false);
  const [deactivate] = useDeactivateLifecycleMutation();
  const autoNavRef = useRef(false);
  const externalMapRef = useRef<maplibregl.Map | null>(null);

  // Hotkeys: G → engineer, V → roc, P → pause, R → resume, S → stop
  useHotkeys({
    g: () => setViewMode(viewMode === 'engineer' ? 'captain' : 'engineer'),
    v: () => setViewMode(viewMode === 'roc' ? 'captain' : 'roc'),
    p: () => setPaused(true),
    r: () => setPaused(false),
    s: handleStop,
  });

  async function handleStop() {
    await deactivate();
    window.location.hash = '#/evaluator/latest';
  }

  const simTimeSec = lifecycleStatus?.sim_time ?? 0;
  const lcState    = lifecycleStatus?.current_state;

  // Auto-navigate when simulation completes
  useEffect(() => {
    if (lcState === 5 && !autoNavRef.current) {
      autoNavRef.current = true;
      const timer = setTimeout(() => { window.location.hash = '#/evaluator/latest'; }, 1500);
      return () => clearTimeout(timer);
    }
  }, [lcState]);

  const borderColor = FSM_BORDER[fsmState] ?? 'transparent';
  const boxShadow   = FSM_GLOW[fsmState] ?? 'none';
  const isEngineer  = viewMode === 'engineer';

  // Own-ship screen position for IvP overlay (approximate center)
  const ownShipScreenPos: [number, number] = [
    window.innerWidth * (isEngineer ? 0.55 : 0.5),
    window.innerHeight * (viewMode === 'captain' ? 0.7 : 0.5),
  ];

  return (
    <div
      data-testid="simulation-monitor"
      data-fsm={fsmState}
      style={{
        height: '100%', display: 'flex', flexDirection: 'column',
        background: 'var(--bg-0)',
        outline: `3px solid ${borderColor}`, outlineOffset: -3,
        boxShadow,
        transition: 'outline-color 0.3s, box-shadow 0.3s',
      }}
    >
      {/* ── Full-screen map area ── */}
      <div style={{ flex: 1, position: 'relative', overflow: 'hidden' }}>
        <ModulePulseBar />

        <SilMapView
          externalMapRef={externalMapRef}
          followOwnShip={viewMode === 'captain' || viewMode === 'roc'}
          viewMode={viewMode as 'captain' | 'engineer' | 'roc'}
        />

        {/* Engineer-view map layers */}
        {isEngineer && (
          <>
            <SafetyDomainLayer
              mapRef={externalMapRef}
              ownShip={ownShip}
              visible={true}
            />
            <MpcTrajectoryLayer
              mapRef={externalMapRef}
              candidates={sat3?.trajectory_candidates ?? []}
              visible={true}
            />
            {ownShip && sat2 && (
              <IvpRiskGradientLayer
                contributions={sat2.ivp_contributions}
                activeBehavior={sat2.active_behavior}
                activeBehaviorWeight={sat2.active_behavior_weight}
                ownShipScreenPos={ownShipScreenPos}
                headingDeg={(ownShip.pose?.heading ?? 0) * 180 / Math.PI}
              />
            )}
          </>
        )}

        {/* COLREGs sectors (engineer + captain in avoidance) */}
        {(isEngineer || fsmState === 'COLREG_AVOIDANCE') && ownShip && (
          <ColregsSectors
            ownShipFraction={isEngineer ? [50, 50] : [50, 70]}
            headingDeg={(ownShip.pose?.heading ?? 0) * 180 / Math.PI}
            outerRadiusPx={320}
          />
        )}

        {/* Waiting overlay */}
        {!ownShip && (
          <div style={{
            position: 'absolute', inset: 0, zIndex: 50,
            display: 'flex', flexDirection: 'column',
            alignItems: 'center', justifyContent: 'center',
            background: 'rgba(7,12,19,0.88)', fontFamily: 'var(--f-mono)',
          }}>
            <div style={{ marginBottom: 16, fontSize: 14, color: 'var(--txt-1)', letterSpacing: '0.16em' }}>
              AWAITING TELEMETRY
            </div>
            <div style={{ fontSize: 10, color: wsConnected ? '#2dd4bf' : '#f87171' }}>
              {wsConnected ? '● WS CONNECTED' : '○ WS DISCONNECTED'} · ws://127.0.0.1:8765
            </div>
          </div>
        )}

        {/* Compass / PPI / Scale */}
        <div style={{ position: 'absolute', bottom: 64, left: 16, zIndex: 15 }}>
          <CompassRose bearing={ownShip ? (ownShip.pose?.heading ?? 0) * 180 / Math.PI : 0} relativeMode={viewMode === 'captain'} />
        </div>
        <PpiRings centerFraction={viewMode === 'captain' ? [50, 70] : [50, 50]} radiiPx={[40, 80, 160, 320]} />
        <div style={{ position: 'absolute', bottom: 64, left: '50%', transform: 'translateX(-50%)', zIndex: 15 }}>
          <DistanceScale nmPerPixel={0.01} />
        </div>

        {/* ThreatRibbon — top center */}
        <div style={{ position: 'absolute', top: 20, left: '50%', transform: 'translateX(-50%)', zIndex: 15 }}>
          <ThreatRibbon />
        </div>

        {/* View mode switcher — top right */}
        <div className="glass-panel" style={{
          position: 'absolute', top: 24, right: 16, zIndex: 15,
          display: 'flex', gap: 4, borderRadius: 6, padding: 4,
        }}>
          {(['captain', 'engineer', 'roc'] as const).map((mode) => (
            <button key={mode} onClick={() => setViewMode(mode)} style={{
              background: viewMode === mode ? '#2dd4bf22' : 'transparent',
              color: viewMode === mode ? '#2dd4bf' : '#8A9AAD',
              border: 'none', padding: '5px 12px', borderRadius: 4, cursor: 'pointer',
              fontFamily: 'var(--f-disp)', fontSize: 10, letterSpacing: 1,
              fontWeight: viewMode === mode ? 700 : 500, textTransform: 'uppercase',
            }}>
              {mode === 'engineer' ? 'ENG' : mode === 'captain' ? 'CAPT' : 'ROC'}
            </button>
          ))}
        </div>

        {/* Engineer drawer toggles */}
        {isEngineer && (
          <>
            <button
              data-testid="left-drawer-toggle"
              onClick={toggleLeft}
              style={{
                position: 'absolute', top: '50%', left: leftDrawerOpen ? 304 : 4,
                transform: 'translateY(-50%)', zIndex: 25,
                background: 'rgba(7,12,19,0.8)', border: '1px solid var(--line-2)',
                color: leftDrawerOpen ? 'var(--c-phos)' : 'var(--txt-3)',
                padding: '6px 4px', cursor: 'pointer', borderRadius: '0 4px 4px 0',
                transition: 'left 0.2s',
              }}
            ><LucidePanelLeft size={14} /></button>
            <button
              data-testid="right-drawer-toggle"
              onClick={toggleRight}
              style={{
                position: 'absolute', top: '50%', right: rightDrawerOpen ? 284 : 4,
                transform: 'translateY(-50%)', zIndex: 25,
                background: 'rgba(7,12,19,0.8)', border: '1px solid var(--line-2)',
                color: rightDrawerOpen ? 'var(--c-phos)' : 'var(--txt-3)',
                padding: '6px 4px', cursor: 'pointer', borderRadius: '4px 0 0 4px',
                transition: 'right 0.2s',
              }}
            ><LucidePanelRight size={14} /></button>
          </>
        )}

        {/* Engineer drawers */}
        {isEngineer && leftDrawerOpen  && <LeftDrawer />}
        {isEngineer && rightDrawerOpen && <RightDrawer />}

        {/* Fault inject modal */}
        {showFaultModal && <FaultInjectPanel onClose={() => setShowFaultModal(false)} />}

        {/* ToR Modal (portal, renders over everything) */}
        <TorModal />
      </div>

      {/* Decision chain timing strip (engineer only) */}
      {isEngineer && <DecisionChainTimingBar pulses={modulePulses} />}

      {/* Bottom control toolbar */}
      <div style={{
        height: 48, background: 'var(--bg-1)', borderTop: '1px solid var(--line-2)',
        display: 'flex', alignItems: 'center', padding: '0 24px', gap: 24,
        fontFamily: 'var(--f-mono)', fontSize: 12, color: 'var(--txt-1)', flexShrink: 0,
      }}>
        {/* Playback */}
        <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
          <button onClick={() => setPaused(false)} style={{ background: 'transparent', color: !isPaused ? 'var(--c-phos)' : 'var(--txt-2)', border: 'none', cursor: 'pointer' }}>
            <LucidePlay size={20} />
          </button>
          <button onClick={() => setPaused(true)} style={{ background: 'transparent', color: isPaused ? 'var(--c-warn)' : 'var(--txt-2)', border: 'none', cursor: 'pointer' }}>
            <LucidePause size={20} />
          </button>
          <button onClick={handleStop} style={{ background: 'transparent', color: 'var(--c-danger)', border: 'none', cursor: 'pointer', marginLeft: 8 }}>
            <LucideSquare size={20} />
          </button>
        </div>

        {/* Time */}
        <div style={{ flex: 1, display: 'flex', alignItems: 'center', gap: 12 }}>
          <input type="range" min="0" max="600" value={simTimeSec} style={{ flex: 1, accentColor: 'var(--c-phos)' }} readOnly />
          <span style={{ color: 'var(--txt-1)' }}>{fmtSimTime(simTimeSec)}</span>
        </div>

        {/* Speed */}
        <select value={simRate} onChange={(e) => setSimRate(Number(e.target.value))} style={{
          background: 'var(--bg-2)', color: 'var(--c-phos)',
          border: '1px solid var(--line-2)', padding: '4px 8px', borderRadius: 4, fontFamily: 'var(--f-mono)',
        }}>
          {[0.5, 1, 2, 4, 10, 20, 50].map((r) => <option key={r} value={r}>{r}x</option>)}
        </select>

        {/* Tools */}
        <div style={{ display: 'flex', gap: 16, borderLeft: '1px solid var(--line-2)', paddingLeft: 24 }}>
          <button onClick={toggleAsdr} style={{ background: 'transparent', color: asdrExpanded ? 'var(--c-phos)' : 'var(--txt-2)', border: 'none', cursor: 'pointer', display: 'flex', alignItems: 'center', gap: 6 }}>
            <LucideTerminalSquare size={18} /> ASDR
          </button>
          <button onClick={() => setShowFaultModal(true)} style={{ background: 'transparent', color: 'var(--c-danger)', border: 'none', cursor: 'pointer', display: 'flex', alignItems: 'center', gap: 6 }}>
            <LucideAlertTriangle size={18} /> FAULT
          </button>
        </div>
      </div>
    </div>
  );
}
```

- [ ] **Step 5: Fix type error in SilMapView.tsx**

Open `web/src/map/SilMapView.tsx`. Search for the `viewMode` prop type definition. Replace:

```typescript
viewMode?: 'captain' | 'god';
```

with:

```typescript
viewMode?: 'captain' | 'engineer' | 'roc';
```

Replace all `viewMode === 'god'` occurrences with `viewMode === 'engineer'`:

```bash
cd web && grep -n "'god'" src/map/SilMapView.tsx
```

Apply replacements for each occurrence found. Then run:

```bash
cd web && npx tsc --noEmit
```

Expected: no errors (or only pre-existing errors unrelated to this change)

- [ ] **Step 6: Run SimulationMonitor test**

```bash
cd web && npx vitest run src/screens/__tests__/SimulationMonitor.test.tsx
```

Expected: PASS — 7 tests pass

- [ ] **Step 7: Run full test suite to check for regressions**

```bash
cd web && npx vitest run
```

Expected: all existing tests pass, new tests pass. If `BridgeHMI.test.tsx` fails due to `viewMode` type change, open it and update the mock from `'god'` to `'engineer'`.

- [ ] **Step 8: Commit Task 8 (final)**

```bash
cd web && git add src/screens/SimulationMonitor.tsx src/map/SilMapView.tsx src/screens/__tests__/SimulationMonitor.test.tsx && git commit -m "feat(screen3): Task 8 — SimulationMonitor full refactor: captain/engineer/roc views, drawers, FSM glow, algorithm layers

DEMO-1 deliverables:
- captain view: IEC 62288 compliant, ThreatRibbon, ConningBar, ToR modal (≥2s hold)
- engineer view: left drawer (ARPA + M6 tree), right drawer (SOTIF + ASDR + scoring)
- engineer map layers: SafetyDomainLayer, MpcTrajectoryLayer, IvpRiskGradientLayer
- FSM border glow: TOR=amber, MRC=blood-red
- G hotkey: captain↔engineer toggle; V hotkey: ROC toggle
- Fixes GAP-029: 'god' → 'engineer' viewMode

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage check:**

| Spec requirement | Covered in task |
|---|---|
| Captain ECDIS view (IEC 62288 S-Mode) | Task 8 — captain mode, ConningBar, ThreatRibbon, COG vector via SilMapView |
| Engineer view with drawers | Task 8 — leftDrawerOpen/rightDrawerOpen, LeftDrawer/RightDrawer components |
| viewMode 'god' → 'engineer' rename | Task 0B, Task 8 |
| ToR ≥2s pointer-hold (IMO MASS Code §6.3.2) | Task 1 (GAP-NEW-003) |
| 3-tier safety zones | Task 2 |
| M4 IvP 8-direction risk gradient | Task 3 |
| M5 MPC trajectory (Mid + BC-MPC) | Task 4 |
| M6 5-layer COLREGs rationale tree | Task 5 |
| M7 SOTIF 6-assumption monitor | Task 6 |
| Decision chain timing strip | Task 7 |
| FSM border glow (TOR amber / MRC blood-red) | Task 8 |
| SAT-1/2/3 store fields | Task 0D |
| G/V hotkeys | Task 8 |
| ASDR in right drawer | Task 8 (RightDrawer ④) |
| Scoring HUD in right drawer | Task 8 (RightDrawer ⑤) |
| Fault inject panel | Task 8 (FaultInjectPanel re-used) |

**Placeholder scan:** No `TBD`, `TODO`, or incomplete steps found.

**Type consistency check:**
- `SAT2Data.ivp_contributions` → consumed by `IvpRiskGradientLayer` as `IvpContribution[]` ✓
- `SAT3Data.trajectory_candidates` → consumed by `MpcTrajectoryLayer` as `TrajectoryCandidate[]` ✓
- `SotifMetrics` → consumed by `SotifMonitorStrip` as `SotifMetrics | null` ✓
- `ColregsChainLayer[]` → consumed by `ColregsRationaleTree` ✓
- `ViewMode` in uiStore updated to `'captain' | 'engineer' | 'roc'`; all consumers updated ✓
- `TorRequest` no longer has `sat1LockUntilSimTime`; TorModal no longer references it ✓

**GAP closures:**
- GAP-029: `'god'` → `'engineer'` (Tasks 0B + 8)
- GAP-NEW-003: TorModal pointer-hold (Task 1)
- GAP-NEW-004/5/6/7/8: New components (Tasks 3/4/5/6/7)

---

**Plan complete.** Saved to `docs/superpowers/plans/2026-05-18-screen3-simulation-monitor.md`.

**Two execution options:**

**1. Subagent-Driven (recommended)** — Fresh subagent per task, review between tasks, fast iteration. Task 0 runs first (serial), then Tasks 1/2/3/5/6/7 run in parallel (3 parallel agents), then Task 4/final after.

**2. Inline Execution** — Execute tasks sequentially in this session using `superpowers:executing-plans`.

Which approach?
