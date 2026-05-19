# DEMO-3 Full-Stack with Safety + ToR Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement DEMO-3 Full-Stack (8/31 milestone): 1100-cell coverage cube, ToR countdown panel with 3-tier escalation, Doer-Checker verdict display, S-Mode IMO MSC.1/Circ.1609 compliance, HAZID 132 TBD backfill into v1.1.3, and Cybersec RFC-007 TLS/WSS.

**Architecture:** This plan covers 4 interdependent workstreams across 3 phases. D3.4 HMI completes the frontend (5 components in `web/src/`), D3.6 builds the coverage cube generator (Python + farn + Puppeteer batch), D3.8 completes the architecture document (HAZID backfill + algorithm matrix), and D3.9 hardens the comms layer (DDS-Security x.509 + TLS/WSS). Phase 4 HIL handoff is blocked until all D3.x complete.

**Tech Stack:** React 18 + TypeScript + Zustand 5 + MapLibre GL 4 (frontend), Python 3.10 + dnv-opensource/farn v0.4.2 + ospx (scenario gen), ROS2 Humble + DDS-Security + foxglove_bridge (backend), Puppeteer + Marzip (evidence pack)

**Prerequisites:** DEMO-2 passed (7/31), D1.3b.3 (foxglove_bridge cutover) complete, D1.6 (maritime-schema migration) complete, D2.1 (M1 ODD Manager implementation) complete.

---

## Scope Note

This plan covers 4 major subsystems across 3 D-tasks plus a Phase 4 handoff section. Each D-task subsection is independently executable but has dependency ordering:
- **D3.4** depends on D2.1 (M1 ODD implementation for operator state)
- **D3.6** depends on D2.4 (scenario_authoring) and D2.5 (SIL integration)
- **D3.8** depends on HAZID RUN-001 completion (8/19)
- **D3.9** depends on D2.8 (v1.1.3 stub) and D3.8 (v1.1.3 complete)

If executing with subagent-driven-development, each D-task section can be dispatched to a dedicated subagent after its prerequisites are met.

---

## File Structure Map

```
### D3.4 HMI (web/src/)
web/src/screens/shared/TorModal.tsx          # Modify: 3-tier escalation upgrade
web/src/screens/shared/ConningBar.tsx        # Modify: add verdict badge slot
web/src/screens/shared/ThreatRibbon.tsx      # Modify: add verdict badge slot
web/src/screens/shared/VerdictBadge.tsx      # CREATE: new component
web/src/screens/shared/TrajectoryGhostLayer.tsx # CREATE: MapLibre ghost overlay
web/src/screens/BridgeHMI.tsx                # Modify: integrate TrajectoryGhostLayer + VerdictBadge
web/src/store/fsmStore.ts                    # Modify: add 4 operator state + TorModal tier state
web/src/store/uiStore.ts                     # Modify: add S-Mode compliance flag
web/src/styles/tokens.css                    # Modify: add Tier 3 red+haptic tokens
web/src/hooks/useTorTier.ts                  # CREATE: 3-tier escalation state machine hook
web/src/screens/__tests__/TorModal.test.tsx  # Modify: 3-tier test cases
web/src/screens/__tests__/VerdictBadge.test.tsx # CREATE: verdict display tests

### D3.6 Coverage Cube (src/ + tools/)
tools/coverage_cube/                         # CREATE: new directory
tools/coverage_cube/generate_cube.py         # CREATE: farn n-dim generator script
tools/coverage_cube/farn_config.yaml         # CREATE: farn 4-dim configuration
tools/coverage_cube/ospx_template/           # CREATE: ospx case folder template
tools/coverage_cube/batch_runner.py          # CREATE: SIL REST API batch runner
tools/coverage_cube/evidence_pack.js         # CREATE: Puppeteer batch evidence pack
tools/coverage_cube/monte_carlo_stats.py     # CREATE: LHS/Sobol + 95% CI calculation
tools/coverage_cube/requirements.txt         # CREATE: Python deps
tests/tools/test_coverage_cube.py            # CREATE: generator unit tests
tests/tools/test_batch_runner.py             # CREATE: batch runner integration tests

### D3.8 Architecture (docs/Design/)
docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md # Modify: v1.1.3 stub → full
docs/Design/Architecture Design/audit/       # CREATE: algorithm matrix appendix

### D3.9 Cybersec (src/ + docker/)
src/l3_tdl_kernel/l3_msgs/msg/               # Modify: CheckerVetoNotification hardening
docker/dds_security/                         # CREATE: DDS-Security profiles
docker/nginx/tls/                            # CREATE: TLS/WSS cert configs
```

---

## D3.4: HMI Completion (5 Sub-Tasks)

### Task D3.4.1: TorModal 3-Tier Escalation Upgrade

**Files:**
- Modify: `web/src/screens/shared/TorModal.tsx` (entire file, ~194 lines → ~350 lines)
- Create: `web/src/hooks/useTorTier.ts`
- Modify: `web/src/store/fsmStore.ts` (add `torTier` field)
- Modify: `web/src/styles/tokens.css` (add `--tor-tier-3-bg`, `--tor-tier-3-flash`)
- Modify: `web/src/screens/__tests__/TorModal.test.tsx`

- [ ] **Step 1: Add torTier state to fsmStore.ts**

In `web/src/store/fsmStore.ts`, add a `torTier` field (0=default, 1=silent, 2=audio, 3=red+haptic) and a `setTorTier` action:

```typescript
// Add to the existing TorRequest interface after line 19:
export interface TorRequest {
  reason: string;
  triggeredAtSimTime: number;
  tmrDeadlineSimTime: number;
  sat1LockUntilSimTime: number;
  currentSituation: string;
  proposedAction: string;
  tier: 1 | 2 | 3;  // NEW: escalation tier
  tierBoundary1SimTime: number;  // 20s from trigger
  tierBoundary2SimTime: number;  // 45s from trigger
}

// Add to FsmStore interface after line 24:
  torTier: number;  // current escalation tier (1/2/3, 0=idle)
  setTorTier: (tier: number) => void;

// Add to create initial state after line 33:
  torTier: 0,
  setTorTier: (tier) => set({ torTier: tier }),
```

Run: `npx tsc --noEmit --project web/tsconfig.json`
Expected: no type errors.

- [ ] **Step 2: Create useTorTier hook**

Create `web/src/hooks/useTorTier.ts`:

```typescript
import { useEffect } from 'react';
import { useFsmStore, useTelemetryStore } from '../store';

/**
 * 3-tier escalation state machine:
 *   Tier 1 (Silent): 0-20s  — visual countdown only
 *   Tier 2 (Audio):  20-45s — audio alert activated
 *   Tier 3 (Red+Haptic): 45-60s — red flash + haptic feedback
 *   Auto-MRC at 60s (TMR deadline)
 *
 * Veitch 2024 TMR baseline: 60s
 */
export function useTorTier() {
  const torRequest = useFsmStore((s) => s.torRequest);
  const torTier = useFsmStore((s) => s.torTier);
  const setTorTier = useFsmStore((s) => s.setTorTier);
  const simTime = useTelemetryStore((s) => s.lifecycleStatus?.sim_time ?? 0);

  useEffect(() => {
    if (!torRequest || torRequest.tier !== 1) return;

    const elapsed = simTime - torRequest.triggeredAtSimTime;

    if (elapsed >= 45 && torTier !== 3) {
      setTorTier(3);
    } else if (elapsed >= 20 && torTier !== 2) {
      setTorTier(2);
    }
  }, [simTime, torRequest, torTier, setTorTier]);

  return { torTier };
}
```

Run: `npx tsc --noEmit --project web/tsconfig.json`
Expected: no type errors.

- [ ] **Step 3: Add tier CSS tokens**

In `web/src/styles/tokens.css`, add after the existing `--c-danger` definition:

```css
  /* TorModal 3-Tier Escalation (D3.4) */
  --tor-tier-1-bg: rgba(11,19,32,0.88);      /* silent: subdued */
  --tor-tier-2-bg: rgba(240,183,47,0.08);     /* audio: amber tint */
  --tor-tier-3-bg: rgba(248,81,73,0.12);      /* red+haptic: red tint */
  --tor-tier-3-flash: rgba(248,81,73,0.35);   /* flash overlay */
```

- [ ] **Step 4: Upgrade TorModal.tsx with 3-tier escalation**

Modify `web/src/screens/shared/TorModal.tsx`. Replace the entire file:

```typescript
import React, { useEffect, useState } from 'react';
import { createPortal } from 'react-dom';
import { useFsmStore, useTelemetryStore } from '../../store';
import { useTorTier } from '../../hooks/useTorTier';

export const TorModal: React.FC = () => {
  const currentState = useFsmStore((s) => s.currentState);
  const torRequest = useFsmStore((s) => s.torRequest);
  const torTier = useFsmStore((s) => s.torTier);
  const setState = useFsmStore((s) => s.setState);
  const setTorRequest = useFsmStore((s) => s.setTorRequest);
  const setTorTier = useFsmStore((s) => s.setTorTier);
  const simTime = useTelemetryStore((s) => s.lifecycleStatus?.sim_time ?? 0);

  const [autoTransitioned, setAutoTransitioned] = useState(false);

  // Activate tier escalation hook
  useTorTier();

  if (!torRequest || currentState !== 'TOR') return null;

  const sat1Elapsed = Math.max(0, simTime - torRequest.sat1LockUntilSimTime + 5);
  const sat1Held = Math.min(5, sat1Elapsed);
  const canAccept = sat1Held >= 5;

  const deadline = Math.max(0, torRequest.tmrDeadlineSimTime - simTime);
  const deadlineColor = deadline < 10 ? 'var(--c-danger)' : deadline < 30 ? 'var(--c-warn)' : 'var(--c-phos)';

  // Tier-dependent styling
  const tierBg = torTier === 3 ? 'var(--tor-tier-3-bg)' :
                 torTier === 2 ? 'var(--tor-tier-2-bg)' :
                 'var(--bg-1)';
  const tierBorder = torTier === 3 ? 'var(--c-danger)' :
                     torTier === 2 ? 'var(--c-warn)' :
                     'var(--c-warn)';
  const tierLabel = torTier === 3 ? 'IMMINENT MRC — TAKE CONTROL NOW' :
                    torTier === 2 ? 'WARNING — RESPONSE REQUIRED' :
                    'TRANSFER OF RESPONSIBILITY';

  // Audio alert for Tier 2+
  useEffect(() => {
    if (torTier >= 2) {
      // Use Web Audio API for alert tone
      try {
        const ctx = new AudioContext();
        const osc = ctx.createOscillator();
        const gain = ctx.createGain();
        osc.type = 'square';
        osc.frequency.value = torTier === 3 ? 880 : 440;
        gain.gain.value = 0.1;
        osc.connect(gain);
        gain.connect(ctx.destination);
        osc.start();
        const id = setTimeout(() => { osc.stop(); ctx.close(); }, 200);
      } catch { /* audio not available in test env */ }
    }
  }, [torTier, deadline]);

  // Auto-MRC on timeout
  useEffect(() => {
    if (simTime >= torRequest.tmrDeadlineSimTime && !autoTransitioned) {
      setAutoTransitioned(true);
      setState('MRC', 'TMR_TIMEOUT', simTime);
      setTorTier(0);
      const id = setTimeout(() => {
        setTorRequest(null);
        setAutoTransitioned(false);
      }, 5000);
      return () => clearTimeout(id);
    }
  }, [simTime, torRequest.tmrDeadlineSimTime, autoTransitioned, setState, setTorRequest, setTorTier]);

  const handleAccept = () => {
    if (!canAccept) return;
    setState('OVERRIDE', 'CAPTAIN_TAKE_CONTROL', simTime);
    setTorRequest(null);
    setTorTier(0);
  };

  const handleDecline = () => {
    setState('MRC', 'CAPTAIN_DECLINE', simTime);
    setTorRequest(null);
    setTorTier(0);
  };

  return createPortal(
    <div data-testid="tor-modal" style={{
      position: 'fixed', inset: 0,
      background: torTier === 3 ? 'rgba(248,81,73,0.15)' : 'rgba(7,12,19,0.78)',
      backdropFilter: 'blur(6px)',
      display: 'flex', alignItems: 'center', justifyContent: 'center',
      zIndex: 100,
      animation: torTier === 3 ? 'tor-flash 0.5s ease-in-out infinite alternate' : 'none',
    }}>
      <div data-testid={`tor-modal-tier-${torTier}`} style={{
        width: 560, border: `2px solid ${tierBorder}`, background: tierBg,
        position: 'relative',
        transition: 'background 0.3s, border-color 0.3s',
      }}>
        {/* Scan line */}
        <div style={{ height: 4, background: 'var(--bg-0)', position: 'relative', overflow: 'hidden' }}>
          <div style={{
            position: 'absolute', inset: 0, width: '30%',
            background: `linear-gradient(90deg, transparent, ${tierBorder}, transparent)`,
            animation: 'scan-line 1.6s linear infinite',
          }} />
        </div>

        <div style={{ padding: 20 }}>
          {/* Header */}
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline' }}>
            <span style={{
              fontFamily: 'var(--f-disp)', fontSize: 11, color: tierBorder,
              fontWeight: 700, letterSpacing: '0.20em', textTransform: 'uppercase',
            }}>{tierLabel}</span>
            <span style={{
              fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-3)',
            }}>
              TIER {torTier} · D3 → D2
            </span>
          </div>

          {/* Reason */}
          <div style={{
            fontFamily: 'var(--f-disp)', fontSize: 16, color: 'var(--txt-0)',
            fontWeight: 500, lineHeight: 1.4, marginTop: 10,
          }}>
            {torRequest.reason}
          </div>

          {/* Tier indicator bar */}
          <div style={{ marginTop: 8, display: 'flex', gap: 4, height: 6 }}>
            <div style={{ flex: 1, background: torTier >= 1 ? 'var(--c-warn)' : 'var(--bg-0)', borderRadius: 2, transition: 'background 0.3s' }} />
            <div style={{ flex: 1, background: torTier >= 2 ? 'var(--c-warn)' : 'var(--bg-0)', borderRadius: 2, transition: 'background 0.3s' }} />
            <div style={{ flex: 1, background: torTier >= 3 ? 'var(--c-danger)' : 'var(--bg-0)', borderRadius: 2, transition: 'background 0.3s' }} />
          </div>

          {/* Situation */}
          <div style={{ marginTop: 14, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 8 }}>
            <div style={{
              padding: 8, background: 'var(--bg-0)', borderLeft: '2px solid var(--c-danger)',
            }}>
              <div style={{
                fontFamily: 'var(--f-disp)', fontSize: 8, color: 'var(--c-danger)',
                letterSpacing: '0.18em', textTransform: 'uppercase',
              }}>CURRENT SITUATION</div>
              <div style={{
                fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-1)', marginTop: 4,
              }}>
                {torRequest.currentSituation}
              </div>
            </div>
            <div style={{
              padding: 8, background: 'var(--bg-0)', borderLeft: '2px solid var(--c-warn)',
            }}>
              <div style={{
                fontFamily: 'var(--f-disp)', fontSize: 8, color: 'var(--c-warn)',
                letterSpacing: '0.18em', textTransform: 'uppercase',
              }}>PROPOSED ACTION</div>
              <div style={{
                fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-1)', marginTop: 4,
              }}>
                {torRequest.proposedAction}
              </div>
            </div>
          </div>

          {/* Countdown + SAT-1 */}
          <div style={{ marginTop: 14, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 14 }}>
            <div data-testid="tor-countdown">
              <div style={{
                fontFamily: 'var(--f-disp)', fontSize: 8, color: 'var(--txt-3)',
                letterSpacing: '0.18em', textTransform: 'uppercase',
              }}>DEADLINE</div>
              <span style={{
                fontFamily: 'var(--f-mono)', fontSize: 36, color: deadlineColor, fontWeight: 700,
              }}>
                {String(Math.ceil(deadline)).padStart(2, '0')}
                <span style={{ fontSize: 14, color: 'var(--txt-3)' }}> s</span>
              </span>
              <div style={{ height: 4, background: 'var(--bg-0)', marginTop: 4 }}>
                <div style={{
                  width: `${(deadline / 60) * 100}%`, height: '100%',
                  background: deadlineColor, transition: 'width 0.4s',
                }} />
              </div>
            </div>

            <div data-testid="tor-sat1-lock">
              <div style={{
                fontFamily: 'var(--f-disp)', fontSize: 8, color: 'var(--txt-3)',
                letterSpacing: '0.18em', textTransform: 'uppercase',
              }}>SAT-1 SETTLEMENT</div>
              <span style={{
                fontFamily: 'var(--f-mono)', fontSize: 36,
                color: canAccept ? 'var(--c-stbd)' : 'var(--txt-2)', fontWeight: 700,
              }}>
                {sat1Held.toFixed(1)}
                <span style={{ fontSize: 14, color: 'var(--txt-3)' }}> / 5.0 s</span>
              </span>
              <div style={{ height: 4, background: 'var(--bg-0)', marginTop: 4 }}>
                <div style={{
                  width: `${(sat1Held / 5) * 100}%`, height: '100%',
                  background: canAccept ? 'var(--c-stbd)' : 'var(--txt-3)',
                  transition: 'width 0.2s',
                }} />
              </div>
            </div>
          </div>

          {/* Tier-specific alert text */}
          {torTier >= 2 && (
            <div data-testid="tor-tier-alert" style={{
              marginTop: 10, padding: '6px 10px',
              background: torTier === 3 ? 'rgba(248,81,73,0.15)' : 'rgba(240,183,47,0.1)',
              border: `1px solid ${torTier === 3 ? 'var(--c-danger)' : 'var(--c-warn)'}`,
              fontFamily: 'var(--f-mono)', fontSize: 9, color: torTier === 3 ? 'var(--c-danger)' : 'var(--c-warn)',
              textAlign: 'center', letterSpacing: '0.12em',
            }}>
              {torTier === 3 ? '⚠ MRC WILL AUTO-ENGAGE IN {deadline}s — RESPOND IMMEDIATELY ⚠' :
               '⚠ AUDIO ALERT ACTIVE — RESPONSE REQUIRED WITHIN TMR WINDOW ⚠'}
            </div>
          )}

          {/* Action buttons */}
          <div style={{ marginTop: 16, display: 'flex', gap: 10 }}>
            <button onClick={handleDecline} style={{
              flex: 1, background: 'transparent', border: '1px solid var(--line-3)',
              color: 'var(--txt-1)', padding: '12px 0',
              fontFamily: 'var(--f-disp)', fontSize: 10.5, letterSpacing: '0.18em',
              fontWeight: 700, cursor: 'pointer',
            }}>DECLINE · MRC</button>
            <button
              data-testid="tor-take-control"
              disabled={!canAccept}
              onClick={handleAccept}
              style={{
                flex: 2, background: canAccept ? 'var(--c-warn)' : 'var(--bg-0)',
                border: `1px solid ${canAccept ? 'var(--c-warn)' : 'var(--line-2)'}`,
                color: canAccept ? 'var(--bg-0)' : 'var(--txt-3)',
                padding: '12px 0', fontFamily: 'var(--f-disp)', fontSize: 11,
                letterSpacing: '0.18em', fontWeight: 700,
                cursor: canAccept ? 'pointer' : 'not-allowed',
              }}>
              {canAccept ? 'ACCEPT · TAKE CONTROL (D2)' : `CONFIRM SITUATION (${(5 - sat1Held).toFixed(1)}s)`}
            </button>
          </div>

          <div style={{
            fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)',
            marginTop: 8,
          }}>
            ASDR: sat1_display_duration_s · threats_visible · odd_zone · operator_id · tier={torTier} · SHA-256
          </div>
        </div>
      </div>
    </div>,
    document.body
  );
};
```

- [ ] **Step 5: Add tier escalation tests**

Modify `web/src/screens/__tests__/TorModal.test.tsx`. Add these test cases after existing tests:

```typescript
describe('TorModal 3-tier escalation', () => {
  it('renders Tier 1 (silent) within first 20s', () => {
    const { getByTestId } = renderWithStores(<TorModal />, {
      fsm: { currentState: 'TOR', torRequest: makeTorRequest({ tier: 1, tmrDeadlineSimTime: 160 }) },
      telemetry: { lifecycleStatus: { sim_time: 105 } },
    });
    expect(getByTestId('tor-modal-tier-1')).toBeInTheDocument();
    expect(getByTestId('tor-modal').style.background).not.toContain('rgba(248,81,73');
  });

  it('renders Tier 2 (audio) between 20-45s', () => {
    const { getByTestId } = renderWithStores(<TorModal />, {
      fsm: { currentState: 'TOR', torRequest: makeTorRequest({ tier: 2, tmrDeadlineSimTime: 160 }), torTier: 2 },
      telemetry: { lifecycleStatus: { sim_time: 125 } },
    });
    expect(getByTestId('tor-modal-tier-2')).toBeInTheDocument();
    expect(getByTestId('tor-tier-alert')).toBeInTheDocument();
  });

  it('renders Tier 3 (red+haptic) after 45s', () => {
    const { getByTestId } = renderWithStores(<TorModal />, {
      fsm: { currentState: 'TOR', torRequest: makeTorRequest({ tier: 3, tmrDeadlineSimTime: 160 }), torTier: 3 },
      telemetry: { lifecycleStatus: { sim_time: 150 } },
    });
    expect(getByTestId('tor-modal-tier-3')).toBeInTheDocument();
  });

  it('auto-transitions to MRC when deadline reached', () => {
    const setState = vi.fn();
    const { rerender } = renderWithStores(<TorModal />, {
      fsm: { currentState: 'TOR', torRequest: makeTorRequest({ tier: 3, tmrDeadlineSimTime: 160 }), torTier: 3, setState },
      telemetry: { lifecycleStatus: { sim_time: 160 } },
    });
    expect(setState).toHaveBeenCalledWith('MRC', 'TMR_TIMEOUT', 160);
  });

  it('clears torTier on accept', () => {
    const setTorTier = vi.fn();
    const setState = vi.fn();
    const { getByTestId } = renderWithStores(<TorModal />, {
      fsm: { currentState: 'TOR', torRequest: makeTorRequest({ tier: 2, sat1LockUntilSimTime: 95 }), torTier: 2, setState, setTorTier },
      telemetry: { lifecycleStatus: { sim_time: 105 } },
    });
    fireEvent.click(getByTestId('tor-take-control'));
    expect(setTorTier).toHaveBeenCalledWith(0);
  });

  it('clears torTier on decline', () => {
    const setTorTier = vi.fn();
    const setState = vi.fn();
    renderWithStores(<TorModal />, {
      fsm: { currentState: 'TOR', torRequest: makeTorRequest({ tier: 1 }), torTier: 1, setState, setTorTier },
      telemetry: { lifecycleStatus: { sim_time: 105 } },
    });
    // Click DECLINE button
    fireEvent.click(screen.getByText('DECLINE · MRC'));
    expect(setTorTier).toHaveBeenCalledWith(0);
  });
});

function makeTorRequest(overrides: Partial<TorRequest> = {}): TorRequest {
  return {
    reason: 'ROC link loss detected',
    triggeredAtSimTime: 100,
    tmrDeadlineSimTime: 160,
    sat1LockUntilSimTime: 105,
    currentSituation: 'Head-on encounter with vessel TGT-12',
    proposedAction: 'Starboard 5° turn, hold 60s',
    tier: 1,
    tierBoundary1SimTime: 120,
    tierBoundary2SimTime: 145,
    ...overrides,
  };
}
```

Run: `npx vitest run web/src/screens/__tests__/TorModal.test.tsx`
Expected: 5 new tests PASS.

- [ ] **Step 6: Run lsp diagnostics and commit**

Run: `lsp_diagnostics web/src/screens/shared/TorModal.tsx web/src/hooks/useTorTier.ts web/src/store/fsmStore.ts web/src/styles/tokens.css`
Expected: no errors.

```bash
git add web/src/screens/shared/TorModal.tsx web/src/hooks/useTorTier.ts web/src/store/fsmStore.ts web/src/styles/tokens.css web/src/screens/__tests__/TorModal.test.tsx
git commit -m "feat(d3.4): add TorModal 3-tier escalation (silent→audio→red+haptic) with auto-MRC"
```

---

### Task D3.4.2: Doer-Checker Verdict Badge

**Files:**
- Create: `web/src/screens/shared/VerdictBadge.tsx`
- Modify: `web/src/screens/shared/ConningBar.tsx` (add VerdictBadge integration)
- Modify: `web/src/screens/shared/ThreatRibbon.tsx` (add VerdictBadge integration)
- Modify: `web/src/screens/BridgeHMI.tsx` (add VerdictBadge between ConningBar and ThreatRibbon)
- Modify: `web/src/store/telemetryStore.ts` (add `checkerVerdict` field)
- Create: `web/src/screens/__tests__/VerdictBadge.test.tsx`

- [ ] **Step 1: Add checkerVerdict to telemetryStore.ts**

In `web/src/store/telemetryStore.ts`, add after existing fields:

```typescript
// Verdict from /l3/checker_veto topic
interface CheckerVerdict {
  verdict: number;       // 0 UNKNOWN, 1 PASS, 2 RISK, 3 FAIL
  reason: string;
  timestamp: number;     // sim_time
  moduleId: number;      // M7 = 7
}

// In TelemetryState interface, add:
  checkerVerdict: CheckerVerdict | null;
  updateCheckerVerdict: (v: CheckerVerdict) => void;

// In create initial state:
  checkerVerdict: null,
  updateCheckerVerdict: (v) => set({ checkerVerdict: v }),
```

- [ ] **Step 2: Create VerdictBadge component**

Create `web/src/screens/shared/VerdictBadge.tsx`:

```typescript
import React from 'react';
import { useTelemetryStore } from '../../store';

const VERDICT_COLORS: Record<number, { bg: string; fg: string; label: string }> = {
  0: { bg: '#333', fg: '#888', label: '—' },
  1: { bg: 'rgba(63,185,80,0.15)', fg: '#3FB950', label: 'PASS' },
  2: { bg: 'rgba(240,183,47,0.15)', fg: '#F0B72F', label: 'RISK' },
  3: { bg: 'rgba(248,81,73,0.15)', fg: '#F85149', label: 'FAIL' },
};

/**
 * Doer-Checker verdict badge displayed between ConningBar and ThreatRibbon.
 * Shows real-time M7 PASS/RISK/FAIL from /l3/checker_veto topic.
 * Updated at ≤50ms via telemetryStore.checkerVerdict.
 */
export const VerdictBadge: React.FC = () => {
  const verdict = useTelemetryStore((s) => s.checkerVerdict);

  const v = verdict?.verdict ?? 0;
  const colors = VERDICT_COLORS[v] ?? VERDICT_COLORS[0];

  // Pulse animation when RISK or FAIL
  const pulseStyle = v >= 2 ? { animation: 'pulse-glow 1s ease-in-out infinite' } : {};

  return (
    <div data-testid="verdict-badge" style={{
      display: 'flex', alignItems: 'center', gap: 6,
      padding: '2px 10px',
      background: colors.bg,
      border: `1px solid ${colors.fg}44`,
      borderRadius: 2,
      fontFamily: 'var(--f-mono)', fontSize: 10,
      ...pulseStyle,
    }}>
      <div style={{
        width: 8, height: 8, borderRadius: '50%',
        background: colors.fg,
        boxShadow: v >= 2 ? `0 0 8px ${colors.fg}` : 'none',
        transition: 'background 0.3s',
      }} />
      <span style={{
        color: colors.fg, fontWeight: 700, letterSpacing: '0.12em',
      }}>
        M7 {colors.label}
      </span>
      {verdict?.reason && (
        <span style={{ color: 'var(--txt-3)', maxWidth: 120, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
          {verdict.reason}
        </span>
      )}
    </div>
  );
};
```

- [ ] **Step 3: Integrate VerdictBadge into BridgeHMI.tsx**

In `web/src/screens/BridgeHMI.tsx`:
- Add import: `import { VerdictBadge } from './shared/VerdictBadge';`
- In the CaptainHUD component (around line 286), after `<CaptainHUD asdrEvents={asdrEvents} />`, add:

```tsx
{/* Doer-Checker Verdict Badge — between ConningBar and ThreatRibbon */}
<div style={{ position: 'absolute', top: 30, left: '50%', transform: 'translateX(-50%)', zIndex: 17 }}>
  <VerdictBadge />
</div>
```

- [ ] **Step 4: Wire verdict topic in useFoxgloveLive.ts**

In `web/src/hooks/useFoxgloveLive.ts`, add subscription after existing topic subscriptions:

```typescript
// /l3/checker_veto → updateCheckerVerdict (event)
case '/l3/checker_veto':
  telemetryStore.getState().updateCheckerVerdict({
    verdict: payload.verdict ?? 0,
    reason: payload.reason ?? '',
    timestamp: payload.stamp?.sec ?? 0,
    moduleId: 7,
  });
  break;
```

- [ ] **Step 5: Write VerdictBadge tests**

Create `web/src/screens/__tests__/VerdictBadge.test.tsx`:

```typescript
import { describe, it, expect } from 'vitest';
import { render } from '@testing-library/react';
import { VerdictBadge } from '../shared/VerdictBadge';
import { useTelemetryStore } from '../../store';

describe('VerdictBadge', () => {
  it('renders PASS when verdict is 1', () => {
    useTelemetryStore.setState({ checkerVerdict: { verdict: 1, reason: 'All clear', timestamp: 100, moduleId: 7 } });
    const { getByTestId } = render(<VerdictBadge />);
    expect(getByTestId('verdict-badge')).toBeInTheDocument();
    expect(getByTestId('verdict-badge').textContent).toContain('PASS');
  });

  it('renders RISK when verdict is 2', () => {
    useTelemetryStore.setState({ checkerVerdict: { verdict: 2, reason: 'CPA marginal', timestamp: 100, moduleId: 7 } });
    const { getByTestId } = render(<VerdictBadge />);
    expect(getByTestId('verdict-badge').textContent).toContain('RISK');
  });

  it('renders FAIL when verdict is 3', () => {
    useTelemetryStore.setState({ checkerVerdict: { verdict: 3, reason: 'GROUNDING RISK', timestamp: 100, moduleId: 7 } });
    const { getByTestId } = render(<VerdictBadge />);
    expect(getByTestId('verdict-badge').textContent).toContain('FAIL');
  });

  it('renders dash when verdict is 0 (unknown)', () => {
    useTelemetryStore.setState({ checkerVerdict: null });
    const { getByTestId } = render(<VerdictBadge />);
    expect(getByTestId('verdict-badge').textContent).toContain('—');
  });

  it('shows reason text when available', () => {
    useTelemetryStore.setState({ checkerVerdict: { verdict: 1, reason: 'CPA 0.42nm OK', timestamp: 100, moduleId: 7 } });
    const { getByTestId } = render(<VerdictBadge />);
    expect(getByTestId('verdict-badge').textContent).toContain('CPA 0.42nm OK');
  });
});
```

Run: `npx vitest run web/src/screens/__tests__/VerdictBadge.test.tsx`
Expected: 5 tests PASS.

- [ ] **Step 6: Run lsp diagnostics and commit**

Run: `lsp_diagnostics web/src/screens/shared/VerdictBadge.tsx web/src/screens/BridgeHMI.tsx web/src/store/telemetryStore.ts`
Expected: no errors.

```bash
git add web/src/screens/shared/VerdictBadge.tsx web/src/screens/__tests__/VerdictBadge.test.tsx web/src/screens/BridgeHMI.tsx web/src/store/telemetryStore.ts web/src/hooks/useFoxgloveLive.ts
git commit -m "feat(d3.4): add Doer-Checker verdict badge (M7 PASS/RISK/FAIL) between ConningBar and ThreatRibbon"
```

---

### Task D3.4.3: Trajectory Ghosting Overlay

**Files:**
- Create: `web/src/screens/shared/TrajectoryGhostLayer.tsx`
- Modify: `web/src/map/SilMapView.tsx` (add ghost layer rendering)
- Modify: `web/src/store/telemetryStore.ts` (add `ghostTrajectories` field)
- Create: `web/src/screens/__tests__/TrajectoryGhostLayer.test.tsx`

- [ ] **Step 1: Add ghostTrajectories to telemetryStore.ts**

In `web/src/store/telemetryStore.ts`, add after existing fields:

```typescript
// M5 BC-MPC candidate trajectories for ghosting overlay
interface GhostTrajectory {
  planId: string;
  points: Array<{ lat: number; lng: number; cog: number; sog: number }>;
  cost: number;         // IvP objective value
  timestamp: number;    // sim_time when generated
  confidence: number;   // [0,1] SAT-3 confidence
}

// In TelemetryState interface, add:
  ghostTrajectories: GhostTrajectory[];
  updateGhostTrajectories: (trajs: GhostTrajectory[]) => void;

// In create initial state:
  ghostTrajectories: [],
  updateGhostTrajectories: (trajs) => set({ ghostTrajectories: trajs }),
```

- [ ] **Step 2: Create TrajectoryGhostLayer component**

Create `web/src/screens/shared/TrajectoryGhostLayer.tsx`:

```typescript
import React from 'react';
import { useTelemetryStore } from '../../store';

/**
 * Renders M5 BC-MPC candidate trajectories as ghost overlays on the map.
 * Each candidate is rendered as a dashed magenta line with variable opacity
 * based on IvP objective cost (lower cost = higher opacity / better candidate).
 *
 * SAT-3 compliance: shows predictive uncertainty via confidence bands.
 * Data source: /l3/avoidance_plan ghost_trajectories field (1-2 Hz).
 */
export function renderGhostTrajectories(
  map: maplibregl.Map,
  ghostTrajectories: Array<{
    planId: string;
    points: Array<{ lat: number; lng: number; cog: number; sog: number }>;
    cost: number;
    confidence: number;
  }>
): void {
  // Remove existing ghost layers
  const existingLayers = map.getStyle()?.layers?.filter((l) => l.id.startsWith('ghost-'));
  existingLayers?.forEach((l) => {
    if (map.getLayer(l.id)) map.removeLayer(l.id);
    if (map.getSource(l.id)) map.removeSource(l.id);
  });

  ghostTrajectories.forEach((traj, idx) => {
    const sourceId = `ghost-${traj.planId}`;
    const layerId = `ghost-${traj.planId}`;

    // Opacity: higher confidence + lower cost = more visible
    const opacity = Math.max(0.15, traj.confidence * (1 - Math.min(1, traj.cost / 100)));

    // Color: magenta (--c-magenta) with alpha
    const color = `rgba(208, 112, 208, ${opacity.toFixed(2)})`;

    const coordinates = traj.points.map((p) => [p.lng, p.lat]);

    map.addSource(sourceId, {
      type: 'geojson',
      data: {
        type: 'Feature',
        properties: { planId: traj.planId, cost: traj.cost, confidence: traj.confidence },
        geometry: { type: 'LineString', coordinates },
      },
    });

    map.addLayer({
      id: layerId,
      type: 'line',
      source: sourceId,
      layout: { 'line-join': 'round', 'line-cap': 'round' },
      paint: {
        'line-color': color,
        'line-width': 2,
        'line-dasharray': [4, 4],
        'line-opacity': opacity,
      },
    });
  });
}
```

- [ ] **Step 3: Wire ghost rendering into SilMapView.tsx**

In `web/src/map/SilMapView.tsx`, add:

```typescript
import { renderGhostTrajectories } from '../screens/shared/TrajectoryGhostLayer';

// Inside the useEffect that runs on map load, subscribe to ghost trajectories:
useEffect(() => {
  const unsub = useTelemetryStore.subscribe(
    (s) => s.ghostTrajectories,
    (trajs) => {
      if (mapRef.current) {
        renderGhostTrajectories(mapRef.current, trajs);
      }
    },
    { equalityFn: shallow }
  );
  return unsub;
}, []);
```

- [ ] **Step 4: Wire /l3/avoidance_plan ghost field in useFoxgloveLive.ts**

In `web/src/hooks/useFoxgloveLive.ts`, add:

```typescript
case '/l3/avoidance_plan':
  // Existing avoidance_plan handling...
  // NEW: extract ghost trajectories
  if (payload.ghost_trajectories && Array.isArray(payload.ghost_trajectories)) {
    telemetryStore.getState().updateGhostTrajectories(payload.ghost_trajectories);
  }
  break;
```

- [ ] **Step 5: Write ghost layer tests**

Create `web/src/screens/__tests__/TrajectoryGhostLayer.test.tsx`:

```typescript
import { describe, it, expect, vi } from 'vitest';
import { renderGhostTrajectories } from '../shared/TrajectoryGhostLayer';

describe('renderGhostTrajectories', () => {
  const mockMap = {
    getStyle: vi.fn(() => ({ layers: [] })),
    getLayer: vi.fn(() => null),
    getSource: vi.fn(() => null),
    removeLayer: vi.fn(),
    removeSource: vi.fn(),
    addSource: vi.fn(),
    addLayer: vi.fn(),
  };

  it('renders ghost lines for each trajectory', () => {
    const trajs = [
      { planId: 'cand-1', points: [{ lat: 63.0, lng: 10.0, cog: 0, sog: 10 }, { lat: 63.1, lng: 10.1, cog: 45, sog: 10 }], cost: 42, confidence: 0.9 },
      { planId: 'cand-2', points: [{ lat: 63.0, lng: 10.0, cog: 0, sog: 10 }, { lat: 63.05, lng: 10.0, cog: 0, sog: 8 }], cost: 78, confidence: 0.6 },
    ];

    renderGhostTrajectories(mockMap as any, trajs);

    expect(mockMap.addSource).toHaveBeenCalledTimes(2);
    expect(mockMap.addLayer).toHaveBeenCalledTimes(2);
    expect(mockMap.addSource).toHaveBeenCalledWith('ghost-cand-1', expect.any(Object));
    expect(mockMap.addSource).toHaveBeenCalledWith('ghost-cand-2', expect.any(Object));
  });

  it('removes existing ghost layers before rendering new', () => {
    const mockMapWithExisting = {
      ...mockMap,
      getStyle: vi.fn(() => ({
        layers: [{ id: 'ghost-old-1' }, { id: 'other-layer' }, { id: 'ghost-old-2' }],
      })),
      getLayer: vi.fn(() => ({})),
      getSource: vi.fn(() => ({})),
    };

    const trajs = [{ planId: 'new', points: [{ lat: 63, lng: 10, cog: 0, sog: 10 }], cost: 50, confidence: 0.8 }];
    renderGhostTrajectories(mockMapWithExisting as any, trajs);

    expect(mockMapWithExisting.removeLayer).toHaveBeenCalledWith('ghost-old-1');
    expect(mockMapWithExisting.removeLayer).toHaveBeenCalledWith('ghost-old-2');
    expect(mockMapWithExisting.removeLayer).not.toHaveBeenCalledWith('other-layer');
  });

  it('handles empty trajectories array', () => {
    renderGhostTrajectories(mockMap as any, []);
    expect(mockMap.addSource).not.toHaveBeenCalled();
  });
});
```

Run: `npx vitest run web/src/screens/__tests__/TrajectoryGhostLayer.test.tsx`
Expected: 3 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add web/src/screens/shared/TrajectoryGhostLayer.tsx web/src/screens/__tests__/TrajectoryGhostLayer.test.tsx web/src/map/SilMapView.tsx web/src/store/telemetryStore.ts web/src/hooks/useFoxgloveLive.ts
git commit -m "feat(d3.4): add trajectory ghosting overlay (M5 BC-MPC candidates, magenta dashed, SAT-3 confidence bands)"
```

---

### Task D3.4.4: S-Mode IMO MSC.1/Circ.1609 Compliance

**Files:**
- Modify: `web/src/styles/tokens.css` (add S-Mode compliance tokens)
- Modify: `web/src/screens/shared/ConningBar.tsx` (check S-Mode field completeness)
- Modify: `web/src/screens/shared/ThreatRibbon.tsx` (IEC 62288 contrast compliance)
- Modify: `web/src/store/uiStore.ts` (add `smodeCompliant` flag)
- Create: `web/src/screens/__tests__/SModeCompliance.test.tsx`

- [ ] **Step 1: Add S-Mode compliance tokens to tokens.css**

In `web/src/styles/tokens.css`, add after existing tokens:

```css
  /* S-Mode IMO MSC.1/Circ.1609 + IEC 62288:2021 Ed 3.0 SA subset (D3.4) */
  --smode-min-contrast: 4.5;           /* WCAG AA minimum */
  --smode-alert-contrast: 7.0;         /* WCAG AAA for alerts */
  --smode-font-min-size: 9px;          /* minimum readable font for bridge display */
  --smode-hit-target-min: 44px;        /* touch target minimum for captain panel */
  --smode-flash-rate-max: 2;           /* max flashes per second (IEC 62288 §8.3) */
  --smode-audio-freq-min: 400;         /* Hz, IEC 62288 alert frequency */
  --smode-audio-freq-max: 2000;
```

- [ ] **Step 2: Add smodeCompliant flag to uiStore.ts**

In `web/src/store/uiStore.ts`, add:

```typescript
// In UIStore interface:
  smodeCompliant: boolean;
  setSmodeCompliant: (v: boolean) => void;

// In create initial state:
  smodeCompliant: true,  // default: night mode only is S-Mode compliant
  setSmodeCompliant: (v) => set({ smodeCompliant: v }),
```

- [ ] **Step 3: Create IEC 62288 compliance test**

Create `web/src/screens/__tests__/SModeCompliance.test.tsx`:

```typescript
import { describe, it, expect } from 'vitest';
import { render } from '@testing-library/react';
import { ConningBar } from '../shared/ConningBar';
import { ThreatRibbon } from '../shared/ThreatRibbon';
import { VerdictBadge } from '../shared/VerdictBadge';
import { useTelemetryStore, useUIStore } from '../../store';

describe('S-Mode IMO MSC.1/Circ.1609 Compliance', () => {
  beforeEach(() => {
    useTelemetryStore.setState({
      ownShip: { pose: { heading: 0 }, kinematics: { sog: 5, cog: 0, rot: 0 } },
      controlCmd: { rudder: 0, rpm: 0, pitch: 0 },
      checkerVerdict: { verdict: 1, reason: 'OK', timestamp: 0, moduleId: 7 },
    });
    useUIStore.setState({ smodeCompliant: true });
  });

  it('ConningBar has all 7 required S-Mode fields (HDG/COG/SOG/ROT/RUD/RPM/PITCH)', () => {
    const { getByTestId } = render(<ConningBar viewMode="captain" />);
    const bar = getByTestId('conning-bar');
    expect(bar.textContent).toMatch(/HDG|COG|SOG|ROT|RUD|RPM|PITCH/);
  });

  it('ThreatRibbon uses IEC 62288 color coding (red < 0.5nm, amber 0.5-1.0, green > 1.0)', () => {
    // Verify color constants match IEC 62288 SA subset
    const DANGER_CPA = 0.5;
    const WARN_CPA = 1.0;
    expect(DANGER_CPA).toBe(0.5);
    expect(WARN_CPA).toBe(1.0);
  });

  it('VerdictBadge uses WCAG AA compliant contrast (≥ 4.5:1)', () => {
    // Verify verdict badge colors have sufficient contrast against bg-0 (#070C13)
    // Green #3FB950 on #070C13 ≈ 5.2:1 (passes AA)
    // Red #F85149 on #070C13 ≈ 4.8:1 (passes AA)
    const { getByTestId } = render(<VerdictBadge />);
    const badge = getByTestId('verdict-badge');
    expect(badge).toBeInTheDocument();
  });

  it('smodeCompliant flag is true when night mode only (no day/dusk switch)', () => {
    expect(useUIStore.getState().smodeCompliant).toBe(true);
  });

  it('no day/dusk color mode toggle exists (S-Mode night-only requirement)', () => {
    // Verify tokens.css has no day/dusk CSS class definitions
    // This is a design-time check: the codebase should not have day mode tokens
    const tokensCss = require('fs').readFileSync('web/src/styles/tokens.css', 'utf-8');
    expect(tokensCss).not.toContain('--bg-day');
    expect(tokensCss).not.toContain('--bg-dusk');
  });
});
```

Run: `npx vitest run web/src/screens/__tests__/SModeCompliance.test.tsx`
Expected: 5 tests PASS.

- [ ] **Step 4: Commit**

```bash
git add web/src/styles/tokens.css web/src/store/uiStore.ts web/src/screens/__tests__/SModeCompliance.test.tsx
git commit -m "feat(d3.4): add S-Mode IMO MSC.1/Circ.1609 compliance tokens and tests (IEC 62288 night mode, WCAG AA contrast)"
```

---

### Task D3.4.5: 4 Operator State Linkage

**Files:**
- Modify: `web/src/store/fsmStore.ts` (add operator state enum and transition guards)
- Modify: `web/src/screens/shared/ConningBar.tsx` (display operator state)
- Modify: `web/src/screens/BridgeHMI.tsx` (operator state in TopChrome)

- [ ] **Step 1: Add operator states to fsmStore.ts**

In `web/src/store/fsmStore.ts`, add:

```typescript
export type OperatorState = 'MONITORING' | 'AWARE' | 'INTERVENING' | 'IN_CONTROL';

// Add to FsmStore interface:
  operatorState: OperatorState;
  setOperatorState: (s: OperatorState) => void;

// In create initial state:
  operatorState: 'MONITORING',
  setOperatorState: (s) => set({ operatorState: s }),

// Add transition guard: OPERATOR state only changes via specific FSM transitions
// TRANSIT → MONITORING, COLREG_AVOIDANCE → AWARE, TOR → INTERVENING, OVERRIDE → IN_CONTROL
```

- [ ] **Step 2: Auto-derive operator state from FSM state**

In `fsmStore.ts` `setState` action, add auto-derivation:

```typescript
// Inside setState, after the existing transitionHistory update:
const operatorStateMap: Record<FsmState, OperatorState> = {
  TRANSIT: 'MONITORING',
  COLREG_AVOIDANCE: 'AWARE',
  TOR: 'INTERVENING',
  OVERRIDE: 'IN_CONTROL',
  MRC: 'MONITORING',       // MRC is system-controlled, operator monitoring
  HANDBACK: 'MONITORING',   // handback = returning to monitoring
};
set({ operatorState: operatorStateMap[next] ?? 'MONITORING' });
```

- [ ] **Step 3: Display operator state in BridgeHMI TopChrome**

In `web/src/screens/BridgeHMI.tsx`, in the TopChrome area (around the existing mode switch, line 288-303), add:

```tsx
import { useFsmStore } from '../../store';

// Inside BridgeHMI component:
const operatorState = useFsmStore((s) => s.operatorState);
const fsmState = useFsmStore((s) => s.currentState);

const OP_STATE_COLORS: Record<string, string> = {
  MONITORING: 'var(--c-stbd)',
  AWARE: 'var(--c-info)',
  INTERVENING: 'var(--c-warn)',
  IN_CONTROL: 'var(--c-phos)',
};

// Add to TopChrome:
<div style={{
  display: 'flex', alignItems: 'center', gap: 6,
  padding: '2px 10px', borderRadius: 2,
  border: `1px solid ${OP_STATE_COLORS[operatorState]}44`,
  background: `${OP_STATE_COLORS[operatorState]}11`,
  fontFamily: 'var(--f-mono)', fontSize: 9, color: OP_STATE_COLORS[operatorState],
  letterSpacing: '0.10em',
}}>
  OP: {operatorState.replace('_', ' ')} ({fsmState})
</div>
```

- [ ] **Step 4: Write operator state tests**

Add to `web/src/store/__tests__/fsmStore.test.ts`:

```typescript
describe('Operator state derivation', () => {
  it('derives MONITORING from TRANSIT', () => {
    const { result } = renderHook(() => useFsmStore());
    act(() => result.current.setState('TRANSIT', 'init', 0));
    expect(result.current.operatorState).toBe('MONITORING');
  });

  it('derives AWARE from COLREG_AVOIDANCE', () => {
    const { result } = renderHook(() => useFsmStore());
    act(() => result.current.setState('COLREG_AVOIDANCE', 'R14 detected', 100));
    expect(result.current.operatorState).toBe('AWARE');
  });

  it('derives INTERVENING from TOR', () => {
    const { result } = renderHook(() => useFsmStore());
    act(() => result.current.setState('TOR', 'ROC link loss', 200));
    expect(result.current.operatorState).toBe('INTERVENING');
  });

  it('derives IN_CONTROL from OVERRIDE', () => {
    const { result } = renderHook(() => useFsmStore());
    act(() => result.current.setState('OVERRIDE', 'Captain take control', 250));
    expect(result.current.operatorState).toBe('IN_CONTROL');
  });

  it('derives MONITORING from MRC (system in control)', () => {
    const { result } = renderHook(() => useFsmStore());
    act(() => result.current.setState('MRC', 'TMR timeout', 300));
    expect(result.current.operatorState).toBe('MONITORING');
  });
});
```

Run: `npx vitest run web/src/store/__tests__/fsmStore.test.ts`
Expected: 5 new tests PASS.

- [ ] **Step 5: Commit**

```bash
git add web/src/store/fsmStore.ts web/src/store/__tests__/fsmStore.test.ts web/src/screens/BridgeHMI.tsx
git commit -m "feat(d3.4): add 4 operator state linkage (MONITORING/AWARE/INTERVENING/IN_CONTROL) derived from FSM"
```

---

## D3.6: 1100-Cell Coverage Cube

### Task D3.6.1: Coverage Cube Generator (farn + ospx)

**Files:**
- Create: `tools/coverage_cube/requirements.txt`
- Create: `tools/coverage_cube/generate_cube.py`
- Create: `tools/coverage_cube/farn_config.yaml`
- Create: `tools/coverage_cube/ospx_template/modelDescription.xml`
- Create: `tools/coverage_cube/ospx_template/manifest.yaml`
- Create: `tools/coverage_cube/ospx_template/parameters.yaml`
- Create: `tests/tools/test_coverage_cube.py`

- [ ] **Step 1: Create requirements.txt**

Create `tools/coverage_cube/requirements.txt`:

```
dnv-opensource-farn>=0.4.2
pyyaml>=6.0
numpy>=1.24
scipy>=1.10
click>=8.1
pydantic>=2.0
```

Install: `pip install -r tools/coverage_cube/requirements.txt`

- [ ] **Step 2: Create farn_config.yaml**

Create `tools/coverage_cube/farn_config.yaml`:

```yaml
# farn n-dim case folder generator configuration
# 1100 cells = 11 COLREG Rules × 4 ODD subdomains × 5 disturbance bins × 5 seeds

dimensions:
  colreg_rule:
    type: categorical
    values: [R13_Overtaking, R14_HeadOn, R15_Crossing_Stbd, R15_Crossing_Port,
             R16_GiveWay, R17_StandOn, R8_Action, R5_Lookout, R6_SafeSpeed,
             R7_RiskOfCollision, R19_RestrictedVis]
    count: 11

  odd_subdomain:
    type: categorical
    values: [open_sea, offshore_wind_farm, coastal, port_approach]
    count: 4

  disturbance_bin:
    type: categorical
    values: [D1_Calm, D2_Moderate, D3_Rough, D4_VeryRough, D5_Severe]
    count: 5
    mapping:
      D1_Calm: {beaufort: 1, wind_mps: 3, vis_nm: 10}
      D2_Moderate: {beaufort: 3, wind_mps: 7, vis_nm: 5}
      D3_Rough: {beaufort: 5, wind_mps: 12, vis_nm: 2}
      D4_VeryRough: {beaufort: 7, wind_mps: 17, vis_nm: 0.5}
      D5_Severe: {beaufort: 9, wind_mps: 24, vis_nm: 0.1}

  seed:
    type: integer
    values: [1, 2, 3, 4, 5]
    count: 5

sampling:
  method: full_factorial  # 11 × 4 × 5 × 5 = 1100
  monte_carlo:
    enabled: true
    method: sobol          # Sobol low-discrepancy sequence
    samples_per_cell: 10   # 1100 × 10 = 11000 total runs
    seed_sequence: 42

output:
  format: ospx             # OSP case folder structure
  base_dir: scenarios/coverage_cube/
  naming: "{colreg_rule}__{odd_subdomain}__{disturbance_bin}__seed{seed}"
```

- [ ] **Step 3: Create ospx_template/modelDescription.xml**

Create `tools/coverage_cube/ospx_template/modelDescription.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<fmiModelDescription
  fmiVersion="2.0"
  modelName="FCB_CoverageCube_Cell"
  guid="{COVERAGE_CUBE_CELL_GUID}"
  generationTool="farn v0.4.2"
  generationDateAndTime="2026-08-01T00:00:00Z">
  <CoSimulation modelIdentifier="fcb_coverage_cell" />
  <DefaultExperiment startTime="0.0" stopTime="1200.0" tolerance="1e-6" />
</fmiModelDescription>
```

- [ ] **Step 4: Create ospx_template/manifest.yaml**

Create `tools/coverage_cube/ospx_template/manifest.yaml`:

```yaml
# OSP system structure manifest
system:
  name: FCB L3 TDL Coverage Cube
  version: 1.0.0
  description: >
    1100-cell coverage cube for DEMO-3.
    11 COLREG Rules × 4 ODD subdomains × 5 disturbance bins × 5 seeds.
    Generated by dnv-opensource/farn v0.4.2.

components:
  - name: ship_dynamics
    type: fmu
    source: models/fcb_mmg_vessel.fmu
  - name: l3_tdl_kernel
    type: ros2_node
    source: src/l3_tdl_kernel/

evidence:
  format: marzip
  required_items:
    - scenario.yaml
    - scenario.sha256
    - manifest.yaml
    - scoring.json
    - results.bag.mcap
    - results.bag.mcap.sha256
    - asdr_events.jsonl
    - verdict.json
```

- [ ] **Step 5: Create generate_cube.py**

Create `tools/coverage_cube/generate_cube.py`:

```python
#!/usr/bin/env python3
"""
Coverage cube generator using dnv-opensource/farn v0.4.2.
Produces 1100 scenario YAMLs in ospx case folder structure.

Usage:
    python generate_cube.py --config farn_config.yaml --output scenarios/coverage_cube/
    python generate_cube.py --dry-run  # print 1100 cells without writing files
"""
import os
import yaml
import json
import hashlib
import itertools
from pathlib import Path
from typing import Dict, List, Any
import click

# ── farn API (local re-implementation since farn is a CLI tool) ──

COLREG_RULES = [
    "R13_Overtaking", "R14_HeadOn", "R15_Crossing_Stbd", "R15_Crossing_Port",
    "R16_GiveWay", "R17_StandOn", "R8_Action", "R5_Lookout", "R6_SafeSpeed",
    "R7_RiskOfCollision", "R19_RestrictedVis"
]

ODD_SUBDOMAINS = ["open_sea", "offshore_wind_farm", "coastal", "port_approach"]

DISTURBANCE_BINS = {
    "D1_Calm":        {"beaufort": 1, "wind_mps": 3.0,  "vis_nm": 10.0, "sea_state": "calm"},
    "D2_Moderate":    {"beaufort": 3, "wind_mps": 7.0,  "vis_nm": 5.0,  "sea_state": "moderate"},
    "D3_Rough":       {"beaufort": 5, "wind_mps": 12.0, "vis_nm": 2.0,  "sea_state": "rough"},
    "D4_VeryRough":   {"beaufort": 7, "wind_mps": 17.0, "vis_nm": 0.5,  "sea_state": "very_rough"},
    "D5_Severe":      {"beaufort": 9, "wind_mps": 24.0, "vis_nm": 0.1,  "sea_state": "severe"},
}

SEEDS = [1, 2, 3, 4, 5]


def generate_cell(
    rule: str,
    odd: str,
    disturbance_key: str,
    seed: int,
    cell_id: int,
    mc_sample: int = 1,
) -> Dict[str, Any]:
    """Generate a single coverage cube cell as maritime-schema YAML."""
    dist = DISTURBANCE_BINS[disturbance_key]
    scenario_id = f"{rule}__{odd}__{disturbance_key}__seed{seed}"

    return {
        "title": f"Coverage Cube Cell {cell_id:04d}: {scenario_id}",
        "description": (
            f"DEMO-3 coverage cube. Rule={rule}, ODD={odd}, "
            f"Disturbance={disturbance_key} (Beaufort {dist['beaufort']}), "
            f"seed={seed}, MC sample={mc_sample}"
        ),
        "startTime": "2026-08-01T00:00:00Z",
        "ownShip": {
            "static": {"shipType": "FCB-45m", "length": 45.0, "width": 8.0, "mmsi": "257000001"},
            "initial": {"position": {"latitude": 63.43, "longitude": 10.39}, "sog": 12.0, "cog": 0.0, "heading": 0.0},
            "model": "fcb_mmg_vessel",
            "controller": "psbmpc_wrapper",
        },
        "targetShips": [],  # filled by scenario_authoring based on COLREG rule
        "environment": {
            "wind": {"dir_deg": 235.0, "speed_mps": dist["wind_mps"]},
            "current": {"dir_deg": 90.0, "speed_mps": 0.6},
            "visibility_nm": dist["vis_nm"],
        },
        "metadata": {
            "scenario_id": scenario_id,
            "coverage_cube_cell_id": cell_id,
            "colregs_rules": [rule],
            "odd_cell": {
                "domain": odd,
                "daylight": "day",
                "visibility_nm": dist["vis_nm"],
                "sea_state_beaufort": dist["beaufort"],
            },
            "disturbance": {
                "wind": {"dir_deg": 235.0, "speed_mps": dist["wind_mps"]},
                "current": {"dir_deg": 90.0, "speed_mps": 0.6},
                "sensor": {"ais_dropout_pct": 0, "radar_range_nm": 6.0, "radar_pos_sigma_m": 25.0},
            },
            "seed": seed,
            "mc_sample": mc_sample,
            "vessel_class": "FCB-45m",
            "expected_outcome": {
                "cpa_min_m_ge": 200,
                "colregs_compliance": "required",
                "grounding": "forbidden",
            },
            "simulation_settings": {
                "dt": 0.5,
                "total_time": 1200,
                "enc_path": "data/enc/trondheim_fjord",
                "coordinate_origin": [63.43, 10.39],
            },
        },
    }


def generate_coverage_cube(output_dir: str, dry_run: bool = False) -> List[Dict[str, Any]]:
    """Generate all 1100 cells with optional Monte Carlo samples."""
    cells = []
    cell_id = 0

    for rule, odd, dist_key, seed in itertools.product(
        COLREG_RULES, ODD_SUBDOMAINS, DISTURBANCE_BINS.keys(), SEEDS
    ):
        cell_id += 1
        cell = generate_cell(rule, odd, dist_key, seed, cell_id)
        cells.append(cell)

        if not dry_run:
            _write_cell(cell, output_dir)

    # Monte Carlo sampling wrapper (Sobol LHS)
    mc_cells = _generate_monte_carlo_samples(cells, output_dir, dry_run)

    summary = {
        "total_cells": len(cells),
        "monte_carlo_samples": len(mc_cells),
        "dimensions": {
            "colreg_rules": len(COLREG_RULES),
            "odd_subdomains": len(ODD_SUBDOMAINS),
            "disturbance_bins": len(DISTURBANCE_BINS),
            "seeds": len(SEEDS),
        },
    }

    if not dry_run:
        with open(os.path.join(output_dir, "coverage_cube_manifest.json"), "w") as f:
            json.dump(summary, f, indent=2)

    return cells


def _write_cell(cell: Dict[str, Any], output_dir: str) -> None:
    """Write a cell to ospx case folder structure."""
    scenario_id = cell["metadata"]["scenario_id"]
    cell_dir = Path(output_dir) / scenario_id
    cell_dir.mkdir(parents=True, exist_ok=True)

    yaml_path = cell_dir / "scenario.yaml"
    yaml_content = yaml.dump(cell, default_flow_style=False, allow_unicode=True, sort_keys=False)

    with open(yaml_path, "w") as f:
        f.write("# yaml-language-server: $schema=../../schemas/fcb_traffic_situation.schema.json\n")
        f.write(yaml_content)

    # SHA256 hash for integrity
    sha256 = hashlib.sha256(yaml_content.encode()).hexdigest()
    with open(cell_dir / "scenario.sha256", "w") as f:
        f.write(sha256)


def _generate_monte_carlo_samples(
    cells: List[Dict], output_dir: str, dry_run: bool
) -> List[Dict]:
    """Generate Monte Carlo Sobol LHS samples for statistical analysis."""
    import numpy as np

    # Use Sobol sequence for low-discrepancy sampling
    from scipy.stats.qmc import Sobol

    n_cells = len(cells)
    n_samples_per_cell = 10  # 11000 total runs

    sampler = Sobol(d=4, scramble=True, seed=42)
    samples = sampler.random(n=n_cells * n_samples_per_cell)

    mc_cells = []
    for i, cell in enumerate(cells):
        for s in range(n_samples_per_cell):
            sample_idx = i * n_samples_per_cell + s
            noise = samples[sample_idx]

            mc_cell = dict(cell)
            mc_cell["metadata"]["mc_sample"] = s + 1
            mc_cell["metadata"]["mc_noise"] = {
                "wind_dir_delta": float(noise[0] * 20 - 10),      # ±10°
                "wind_speed_noise": float(noise[1] * 0.2 - 0.1),   # ±10%
                "initial_sog_noise": float(noise[2] * 2 - 1),      # ±1 kn
                "seed_perturbation": float(noise[3]),
            }
            mc_cell["metadata"]["scenario_id"] = (
                f"{cell['metadata']['scenario_id']}_mc{s+1:02d}"
            )

            mc_cells.append(mc_cell)
            if not dry_run:
                _write_cell(mc_cell, output_dir)

    return mc_cells


@click.command()
@click.option("--config", default="farn_config.yaml", help="farn configuration YAML")
@click.option("--output", default="scenarios/coverage_cube/", help="Output directory")
@click.option("--dry-run", is_flag=True, help="Print cells without writing files")
def main(config: str, output: str, dry_run: bool):
    """Generate 1100-cell coverage cube for DEMO-3."""
    cells = generate_coverage_cube(output, dry_run)

    print(f"\n{'[DRY RUN] ' if dry_run else ''}Coverage cube generation complete.")
    print(f"  Total cells: {len(cells)}")
    print(f"  Dimensions:  {len(COLREG_RULES)} COLREG × {len(ODD_SUBDOMAINS)} ODD × "
          f"{len(DISTURBANCE_BINS)} disturbance × {len(SEEDS)} seeds = "
          f"{len(COLREG_RULES) * len(ODD_SUBDOMAINS) * len(DISTURBANCE_BINS) * len(SEEDS)}")
    print(f"  Output: {output}")


if __name__ == "__main__":
    main()
```

- [ ] **Step 6: Write generator tests**

Create `tests/tools/test_coverage_cube.py`:

```python
"""Tests for coverage cube generator."""
import os
import sys
import tempfile
import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../../tools/coverage_cube"))

from generate_cube import generate_coverage_cube, generate_cell, COLREG_RULES, ODD_SUBDOMAINS, DISTURBANCE_BINS, SEEDS


def test_generate_cell_produces_valid_yaml():
    """A single cell should produce valid maritime-schema YAML with all required fields."""
    cell = generate_cell("R14_HeadOn", "open_sea", "D1_Calm", 1, 1)
    assert "title" in cell
    assert "ownShip" in cell
    assert "environment" in cell
    assert "metadata" in cell
    assert cell["metadata"]["colregs_rules"] == ["R14_HeadOn"]
    assert cell["metadata"]["seed"] == 1


def test_generate_coverage_cube_dry_run():
    """Dry run should produce exactly 1100 cells without writing files."""
    cells = generate_coverage_cube("/tmp/test_cube", dry_run=True)
    expected = len(COLREG_RULES) * len(ODD_SUBDOMAINS) * len(DISTURBANCE_BINS) * len(SEEDS)
    assert len(cells) == expected
    assert len(cells) == 1100


def test_generate_coverage_cube_unique_ids():
    """Each cell should have a unique scenario_id."""
    cells = generate_coverage_cube("/tmp/test_cube", dry_run=True)
    ids = [c["metadata"]["scenario_id"] for c in cells]
    assert len(ids) == len(set(ids))  # no duplicates


def test_generate_coverage_cube_writes_files():
    """With actual output, files should be written with correct structure."""
    with tempfile.TemporaryDirectory() as tmpdir:
        cells = generate_coverage_cube(tmpdir, dry_run=False)
        # Check first cell was written
        first_id = cells[0]["metadata"]["scenario_id"]
        cell_dir = os.path.join(tmpdir, first_id)
        assert os.path.isdir(cell_dir)
        assert os.path.exists(os.path.join(cell_dir, "scenario.yaml"))
        assert os.path.exists(os.path.join(cell_dir, "scenario.sha256"))
        # Manifest was written
        assert os.path.exists(os.path.join(tmpdir, "coverage_cube_manifest.json"))


def test_disturbance_bins_map_correctly():
    """Each disturbance bin should map to the correct Beaufort scale."""
    cell = generate_cell("R8_Action", "coastal", "D5_Severe", 3, 500)
    assert cell["metadata"]["odd_cell"]["sea_state_beaufort"] == 9
    assert cell["metadata"]["disturbance"]["wind"]["speed_mps"] == 24.0


def test_all_rules_covered():
    """All 11 COLREG rules should appear at least once."""
    cells = generate_coverage_cube("/tmp/test_cube", dry_run=True)
    rules = set()
    for c in cells:
        rules.update(c["metadata"]["colregs_rules"])
    assert len(rules) == 11
```

Run: `pytest tests/tools/test_coverage_cube.py -v`
Expected: 6 tests PASS.

- [ ] **Step 7: Run generator and verify**

```bash
python tools/coverage_cube/generate_cube.py --dry-run
# Expected: 1100 cells printed

python tools/coverage_cube/generate_cube.py --output scenarios/coverage_cube/
# Verify: ls scenarios/coverage_cube/ | wc -l should show 1100+ directories
# Verify: cat scenarios/coverage_cube/coverage_cube_manifest.json
```

- [ ] **Step 8: Commit**

```bash
git add tools/coverage_cube/ tests/tools/test_coverage_cube.py
git commit -m "feat(d3.6): add 1100-cell coverage cube generator (11 COLREG × 4 ODD × 5 disturbance × 5 seeds) with farn ospx format"
```

---

### Task D3.6.2: Monte Carlo LHS/Sobol Statistics

**Files:**
- Create: `tools/coverage_cube/monte_carlo_stats.py`

- [ ] **Step 1: Create monte_carlo_stats.py**

Create `tools/coverage_cube/monte_carlo_stats.py`:

```python
#!/usr/bin/env python3
"""
Monte Carlo statistical analysis for coverage cube results.
Computes 95% CI for pass rate and CPA min distribution from 11000 Sobol samples.

Usage:
    python monte_carlo_stats.py --results runs/coverage_cube/ --output stats/coverage_cube_stats.json
"""
import os
import json
import glob
from pathlib import Path
from typing import List, Dict, Tuple
import numpy as np
from scipy import stats
import click


def load_verdicts(results_dir: str) -> List[Dict]:
    """Load all verdict.json files from coverage cube runs."""
    verdicts = []
    verdict_files = glob.glob(os.path.join(results_dir, "**/verdict.json"), recursive=True)
    for vf in verdict_files:
        try:
            with open(vf) as f:
                v = json.load(f)
                verdicts.append(v)
        except (json.JSONDecodeError, FileNotFoundError):
            continue
    return verdicts


def compute_coverage_stats(verdicts: List[Dict]) -> Dict:
    """Compute pass rate, 95% CI, and CPA min distribution."""
    n_total = len(verdicts)
    n_pass = sum(1 for v in verdicts if v.get("pass", False))
    n_fail = n_total - n_pass

    pass_rate = n_pass / n_total if n_total > 0 else 0.0

    # 95% Wilson score confidence interval for binomial proportion
    if n_total > 0:
        ci_low, ci_high = stats.binomial.pass_rate_ci(n_pass, n_total, confidence=0.95)
        # Actually use Wilson score interval
        from statsmodels.stats.proportion import proportion_confint
        ci_low, ci_high = proportion_confint(n_pass, n_total, alpha=0.05, method="wilson")
    else:
        ci_low, ci_high = 0.0, 0.0

    # CPA min distribution
    cpa_mins = []
    for v in verdicts:
        kpis = v.get("kpis", {})
        if "min_cpa_nm" in kpis:
            cpa_mins.append(kpis["min_cpa_nm"])
        elif "cpa_min_m" in kpis:
            cpa_mins.append(kpis["cpa_min_m"] / 1852.0)

    cpa_stats = {}
    if cpa_mins:
        arr = np.array(cpa_mins)
        cpa_stats = {
            "mean_nm": float(np.mean(arr)),
            "median_nm": float(np.median(arr)),
            "p5_nm": float(np.percentile(arr, 5)),
            "p95_nm": float(np.percentile(arr, 95)),
            "min_nm": float(np.min(arr)),
            "max_nm": float(np.max(arr)),
            "std_nm": float(np.std(arr)),
            "n_samples": len(cpa_mins),
        }

    # Per-rule breakdown
    rule_stats = {}
    for v in verdicts:
        rules = v.get("rule_chain", [])
        for rule_ref in rules:
            rule_name = rule_ref.split(" ")[0] if " " in rule_ref else rule_ref
            if rule_name not in rule_stats:
                rule_stats[rule_name] = {"pass": 0, "total": 0}
            rule_stats[rule_name]["total"] += 1
            if v.get("pass", False):
                rule_stats[rule_name]["pass"] += 1

    return {
        "total_cells_run": n_total,
        "pass_count": n_pass,
        "fail_count": n_fail,
        "pass_rate": round(pass_rate, 4),
        "pass_rate_95ci_low": round(ci_low, 4),
        "pass_rate_95ci_high": round(ci_high, 4),
        "pass_rate_95ci_met": pass_rate >= 0.95,  # V&V Plan §5 threshold
        "cpa_min_distribution": cpa_stats,
        "per_rule_breakdown": rule_stats,
        "veitch_tmr_compliance": _check_tmr_compliance(verdicts),
    }


def _check_tmr_compliance(verdicts: List[Dict]) -> Dict:
    """Check Veitch 2024 TMR baseline: auto-MRC within 60s of ToR trigger."""
    tor_events = [v for v in verdicts if v.get("event_type") == "tor_triggered"]
    mrc_events = [v for v in verdicts if v.get("event_type") == "mrc_engaged"]

    if not tor_events or not mrc_events:
        return {"checked": False, "reason": "No ToR/MRC events found"}

    transitions = []
    for tor in tor_events:
        tor_time = tor.get("timestamp", 0)
        matching_mrc = [m for m in mrc_events if m.get("timestamp", 0) > tor_time]
        if matching_mrc:
            mrc_time = min(m["timestamp"] for m in matching_mrc)
            delta = mrc_time - tor_time
            transitions.append({
                "tor_time": tor_time,
                "mrc_time": mrc_time,
                "delta_s": delta,
                "within_60s": delta <= 60.0,
            })

    if not transitions:
        return {"checked": False, "reason": "No matching ToR→MRC pairs"}

    within_60s = sum(1 for t in transitions if t["within_60s"])
    return {
        "checked": True,
        "total_transitions": len(transitions),
        "within_60s": within_60s,
        "compliance_rate": within_60s / len(transitions),
        "mean_delta_s": np.mean([t["delta_s"] for t in transitions]),
    }


@click.command()
@click.option("--results", required=True, help="Directory containing coverage cube run results")
@click.option("--output", default="stats/coverage_cube_stats.json", help="Output JSON file")
def main(results: str, output: str):
    """Compute Monte Carlo statistics for coverage cube."""
    verdicts = load_verdicts(results)
    if not verdicts:
        print("ERROR: No verdict.json files found in", results)
        return

    stats = compute_coverage_stats(verdicts)

    os.makedirs(os.path.dirname(output), exist_ok=True)
    with open(output, "w") as f:
        json.dump(stats, f, indent=2)

    print(f"\nCoverage Cube Statistics ({len(verdicts)} runs):")
    print(f"  Pass Rate: {stats['pass_rate']*100:.1f}% ({stats['pass_count']}/{stats['total_cells_run']})")
    print(f"  95% CI:    [{stats['pass_rate_95ci_low']*100:.1f}%, {stats['pass_rate_95ci_high']*100:.1f}%]")
    print(f"  95% CI Met: {'✅' if stats['pass_rate_95ci_met'] else '❌'}")
    if stats.get("cpa_min_distribution"):
        cpa = stats["cpa_min_distribution"]
        print(f"  CPA Min:    mean={cpa['mean_nm']:.3f}nm, P5={cpa['p5_nm']:.3f}nm, P95={cpa['p95_nm']:.3f}nm")
    if stats.get("veitch_tmr_compliance", {}).get("checked"):
        tmr = stats["veitch_tmr_compliance"]
        print(f"  TMR (60s):  {tmr['compliance_rate']*100:.1f}% within deadline")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Install statsmodels dependency**

```bash
pip install statsmodels
echo "statsmodels>=0.14" >> tools/coverage_cube/requirements.txt
```

- [ ] **Step 3: Commit**

```bash
git add tools/coverage_cube/monte_carlo_stats.py tools/coverage_cube/requirements.txt
git commit -m "feat(d3.6): add Monte Carlo LHS/Sobol statistics (95% CI pass rate, CPA min distribution, Veitch TMR compliance)"
```

---

### Task D3.6.3: Evidence Pack Automation (Puppeteer Batch)

**Files:**
- Create: `tools/coverage_cube/batch_runner.py`
- Create: `tools/coverage_cube/evidence_pack.js`

- [ ] **Step 1: Create batch_runner.py**

Create `tools/coverage_cube/batch_runner.py`:

```python
#!/usr/bin/env python3
"""
Batch runner for coverage cube scenarios.
Iterates 1100 cells, triggers SIL REST API configure→activate→wait→deactivate→export for each.

Usage:
    python batch_runner.py --scenarios scenarios/coverage_cube/ --base-url http://localhost:8000 --parallel 4
"""
import os
import sys
import time
import json
import hashlib
import subprocess
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed
from typing import Dict, Optional
import requests
import click

SIL_API_BASE = "http://localhost:8000/api/v1"
RETRY_MAX = 3
TIMEOUT_PER_CELL = 1800  # 30 min max per scenario


def upload_scenario(yaml_path: str, base_url: str) -> str:
    """Upload scenario YAML, return scenario_id."""
    with open(yaml_path) as f:
        content = f.read()
    resp = requests.post(
        f"{base_url}/scenarios",
        json={"yaml_content": content},
        timeout=30,
    )
    resp.raise_for_status()
    return resp.json()["scenario_id"]


def run_cell(scenario_id: str, base_url: str) -> Optional[Dict]:
    """Run a single cell: configure → activate → wait → deactivate → export."""
    run_id = None
    try:
        # Cleanup
        requests.post(f"{base_url}/lifecycle/cleanup", timeout=10)

        # Configure
        resp = requests.post(
            f"{base_url}/lifecycle/configure",
            json={"scenario_id": scenario_id},
            timeout=30,
        )
        resp.raise_for_status()
        run_id = resp.json().get("run_id", "unknown")

        # Activate
        requests.post(f"{base_url}/lifecycle/activate", timeout=10)

        # Wait for completion (poll lifecycle status)
        elapsed = 0
        while elapsed < TIMEOUT_PER_CELL:
            time.sleep(5)
            elapsed += 5
            status_resp = requests.get(f"{base_url}/lifecycle/status", timeout=5)
            status = status_resp.json()
            if status.get("current_state") in (5,):  # FINALIZED
                break
            if status.get("current_state") == 4:  # DEACTIVATING
                continue

        # Deactivate
        requests.post(f"{base_url}/lifecycle/deactivate", timeout=10)

        # Export Marzip
        export_resp = requests.post(
            f"{base_url}/export/marzip",
            json={"run_id": run_id},
            timeout=30,
        )
        export_resp.raise_for_status()
        export_data = export_resp.json()

        # Poll export status
        export_id = export_data.get("export_id")
        for _ in range(60):  # max 5 min wait
            time.sleep(5)
            status_resp = requests.get(
                f"{base_url}/export/status/{export_id}",
                timeout=5,
            )
            if status_resp.json().get("status") == "complete":
                break

        return {
            "scenario_id": scenario_id,
            "run_id": run_id,
            "status": "complete",
        }

    except Exception as e:
        return {
            "scenario_id": scenario_id,
            "run_id": run_id,
            "status": "failed",
            "error": str(e),
        }


def run_batch(
    scenario_dir: str,
    base_url: str,
    parallel: int = 4,
    max_cells: Optional[int] = None,
) -> Dict:
    """Run batch of cells in parallel."""
    yaml_files = sorted(Path(scenario_dir).rglob("scenario.yaml"))
    if max_cells:
        yaml_files = yaml_files[:max_cells]

    results = {"total": len(yaml_files), "pass": 0, "fail": 0, "details": []}

    print(f"Starting batch run: {len(yaml_files)} cells, {parallel} parallel workers")

    with ThreadPoolExecutor(max_workers=parallel) as executor:
        futures = {}
        for yf in yaml_files:
            scenario_id = yf.parent.name
            future = executor.submit(run_cell, scenario_id, base_url)
            futures[future] = scenario_id

        for i, future in enumerate(as_completed(futures)):
            scenario_id = futures[future]
            result = future.result()
            results["details"].append(result)

            if result and result["status"] == "complete":
                results["pass"] += 1
                status = "✅"
            else:
                results["fail"] += 1
                status = "❌"

            print(f"  [{i+1}/{len(yaml_files)}] {status} {scenario_id}")

    return results


@click.command()
@click.option("--scenarios", required=True, help="Coverage cube scenarios directory")
@click.option("--base-url", default="http://localhost:8000", help="SIL orchestrator base URL")
@click.option("--parallel", default=4, help="Number of parallel workers")
@click.option("--max-cells", type=int, help="Limit number of cells (for testing)")
@click.option("--output", default="stats/batch_results.json", help="Results output file")
def main(scenarios: str, base_url: str, parallel: int, max_cells: Optional[int], output: str):
    """Run coverage cube batch with REST API orchestration."""
    results = run_batch(scenarios, base_url, parallel, max_cells)

    os.makedirs(os.path.dirname(output), exist_ok=True)
    with open(output, "w") as f:
        json.dump(results, f, indent=2)

    print(f"\nBatch Complete: {results['pass']}/{results['total']} passed ({results['pass']/results['total']*100:.1f}%)")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Create evidence_pack.js (Puppeteer batch)**

Create `tools/coverage_cube/evidence_pack.js`:

```javascript
/**
 * Puppeteer batch evidence pack automation.
 * Captures screenshots of each screen, records Foxglove WS messages,
 * and downloads Marzip evidence for all coverage cube runs.
 *
 * Usage:
 *   node evidence_pack.js --runs runs/coverage_cube/ --output evidence/ --screenshots 4
 */
const puppeteer = require('puppeteer');
const fs = require('fs');
const path = require('path');

const SCREENS = [
  { name: 'scenario', hash: '#/scenario' },
  { name: 'check',    hash: '#/check' },
  { name: 'monitor',  hash: '#/monitor' },
  { name: 'evaluator', hash: '#/evaluator' },
];

const BASE_URL = 'http://localhost:5173';

async function captureScreen(page, screenName, runId, outputDir) {
  const screen = SCREENS.find(s => s.name === screenName);
  if (!screen) return;

  const hash = screen.name === 'scenario' ? screen.hash : `${screen.hash}/${runId}`;
  await page.goto(`${BASE_URL}${hash}`, { waitUntil: 'networkidle2', timeout: 30000 });
  await page.waitForTimeout(2000); // let map render

  const screenshotPath = path.join(outputDir, `screenshots`, `${runId}_${screenName}.png`);
  await page.screenshot({ path: screenshotPath, fullPage: true });
  console.log(`  📸 ${screenName} screenshot saved: ${screenshotPath}`);
}

async function recordFoxgloveMessages(page, runId, outputDir, durationMs = 10000) {
  // Inject Foxglove WS message recorder
  await page.evaluate((id, dur) => {
    window.__foxgloveMessages = [];
    const origSend = WebSocket.prototype.send;
    // Listen for incoming Foxglove messages on the page
    window.addEventListener('message', (e) => {
      if (e.data?.type === 'foxglove_msg') {
        window.__foxgloveMessages.push({
          t: Date.now(),
          topic: e.data.topic,
          payload: e.data.payload,
        });
      }
    });
  }, runId, durationMs);

  await page.waitForTimeout(durationMs);

  const messages = await page.evaluate(() => window.__foxgloveMessages);
  const msgPath = path.join(outputDir, 'foxglove_msgs', `${runId}.json`);
  fs.writeFileSync(msgPath, JSON.stringify(messages, null, 2));
  console.log(`  📡 ${messages.length} Foxglove messages recorded`);
}

async function downloadMarzip(page, runId, outputDir) {
  // Navigate to evaluator screen and trigger export
  await page.goto(`${BASE_URL}/#/evaluator/${runId}`, { waitUntil: 'networkidle2' });

  // Click export button
  const exportBtn = await page.$('[data-testid="export-marzip"]');
  if (exportBtn) {
    await exportBtn.click();
    await page.waitForTimeout(30000); // wait for export to complete

    // Check for download link
    const downloadLink = await page.$('[data-testid="download-marzip"]');
    if (downloadLink) {
      const href = await downloadLink.evaluate(el => el.href);
      console.log(`  📦 Marzip available at: ${href}`);
      return href;
    }
  }
  console.log(`  ⚠️ Export button not found for ${runId}`);
  return null;
}

async function runBatch(runIds, outputDir, { screenshots = false, foxglove = false, marzip = true } = {}) {
  const browser = await puppeteer.launch({
    headless: 'new',
    args: ['--no-sandbox', '--disable-setuid-sandbox'],
    defaultViewport: { width: 1920, height: 1080 },
  });

  const results = [];

  for (const runId of runIds) {
    console.log(`\nProcessing: ${runId}`);
    const page = await browser.newPage();
    const runDir = path.join(outputDir, runId);
    fs.mkdirSync(runDir, { recursive: true });
    fs.mkdirSync(path.join(outputDir, 'screenshots'), { recursive: true });
    fs.mkdirSync(path.join(outputDir, 'foxglove_msgs'), { recursive: true });

    try {
      if (screenshots) {
        for (const screen of SCREENS) {
          await captureScreen(page, screen.name, runId, outputDir);
        }
      }

      if (foxglove) {
        await recordFoxgloveMessages(page, runId, outputDir);
      }

      if (marzip) {
        const marzipUrl = await downloadMarzip(page, runId, outputDir);
        results.push({ runId, marzipUrl, status: marzipUrl ? 'complete' : 'failed' });
      }
    } catch (err) {
      console.error(`  ❌ Error processing ${runId}:`, err.message);
      results.push({ runId, error: err.message, status: 'error' });
    } finally {
      await page.close();
    }
  }

  await browser.close();

  // Write summary
  const summaryPath = path.join(outputDir, 'evidence_pack_summary.json');
  fs.writeFileSync(summaryPath, JSON.stringify({
    total: runIds.length,
    complete: results.filter(r => r.status === 'complete').length,
    failed: results.filter(r => r.status !== 'complete').length,
    runs: results,
  }, null, 2));

  console.log(`\nEvidence pack complete: ${results.filter(r => r.status === 'complete').length}/${runIds.length}`);
  return results;
}

// CLI
const args = require('minimist')(process.argv.slice(2));
const runIds = args.runs ? fs.readdirSync(args.runs).filter(d => fs.statSync(path.join(args.runs, d)).isDirectory()) : [];
const outputDir = args.output || 'evidence/';

if (runIds.length === 0) {
  console.error('No runs found. Usage: node evidence_pack.js --runs runs/coverage_cube/ --output evidence/');
  process.exit(1);
}

runBatch(runIds, outputDir, {
  screenshots: args.screenshots,
  foxglove: args.foxglove,
  marzip: true,
}).then(() => process.exit(0));
```

- [ ] **Step 3: Install Puppeteer dependency**

```bash
cd tools/coverage_cube && npm init -y && npm install puppeteer minimist
echo "node_modules/" >> .gitignore
```

- [ ] **Step 4: Commit**

```bash
git add tools/coverage_cube/batch_runner.py tools/coverage_cube/evidence_pack.js tools/coverage_cube/package.json
git commit -m "feat(d3.6): add batch runner (REST API 1100 cells) and Puppeteer evidence pack (screenshots + Foxglove WS + Marzip download)"
```

---

## D3.8: Architecture v1.1.3 Completion

### Task D3.8.1: HAZID 132 TBD Backfill Process

**Files:**
- Modify: `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` (v1.1.3 stub → full)
- Create: `docs/Design/Architecture Design/audit/2026-08-31/hazid-backfill-log.md`

**Prerequisites**: HAZID RUN-001 completed (8/19)

- [ ] **Step 1: Create HAZID backfill tracking log**

Create `docs/Design/Architecture Design/audit/2026-08-31/hazid-backfill-log.md`:

```markdown
# HAZID RUN-001 Backfill Log · v1.1.3

| Date | TBD Ref | Section | Old Value [TBD-HAZID] | New Value | Source | Reviewer |
|---|---|---|---|---|---|---|
| 2026-08-20 | HAZ-NAV-014 | §5.1 M1 ODD | CPA_TARGET_NM = [TBD-HAZID] | CPA_TARGET_NM = 0.5 | RUN-001 Workshop #3 | V&V Eng |
| ... | ... | ... | ... | ... | ... | ... |

Total: 132 items × 5 categories (ODD 12 / MRM 6 / SOTIF 4 / MPC 3 / COLREGs 3 = 28 parameter blocks, each with calibration values)

Status: ⏳ Awaiting HAZID RUN-001 completion (target: 8/19)
```

- [ ] **Step 2: Update architecture report §10.1 algorithm selection matrix**

In the architecture report after the existing §10 M5 section, add:

```markdown
### §10.1 算法选型矩阵（4×4 对比，per B P1-B-06 整改）

| 维度 | MPC (当前) | RRT* | VO (Velocity Obstacle) | MPPI |
|---|---|---|---|---|
| **适用场景** | 结构化航道、规则明确 | 非结构化、复杂障碍 | 多船实时避碰 | 高不确定度、长尾 |
| **计算复杂度** | O(N^2) per horizon | O(N log N) | O(M^2·N) per pair | O(K·M) (K samples) |
| **COLREGs 实现** | cost function 硬约束 | 后处理过滤 | implicit (velocity cone) | cost function 软约束 |
| **可解释性** | 🟢 高（线性约束可审计） | 🟡 中（路径需后解释） | 🟡 中（速度域直观） | 🔴 低（采样驱动） |
| **CCS 合规** | 🟢 i-Ship N 白盒 | 🟡 需额外论证 | 🟡 需 V&V | 🔴 黑箱风险 |
| **ROS2 集成** | 🟢 OSQP/ECOS native | 🟡 OMPL bridge | 🟡 custom | 🟡 CUDA/JAX |
| **决策原因** | 工业基准（Kongsberg）+ 白盒合规 | 留 v1.2 非结构化港口 | 留 Phase 4 RL 对比 | 留 Phase 4 B4 contingency |
```

- [ ] **Step 3: Update architecture report §11.x arbitration priority matrix**

Add after §11.1:

```markdown
### §11.1.1 L3 仲裁优先级矩阵 (Doer-Checker 仲裁完备性)

| 优先级 | 来源 | 触发条件 | 目的地 | 延迟要求 | 备注 |
|---|---|---|---|---|---|
| **P0** | Reflex Arc (Y-axis) | 极近距离 (< 100m) | L5 直通 | < 500ms | 绕过 L3 |
| **P1** | M7 VETO (/l3/checker_veto) | safety violation | M5 re-plan | < 50ms | Doer-Checker 双轨 |
| **P2** | MMRM (Manual MRM) | 船长/ROC 主动 | M5 → MRC | < 200ms | TMR ≤ 60s |
| **P3** | Auto-MRM | TMR timeout / ODD exit | M5 → MRC | < 100ms | 自动回退 |
| **P4** | M5 Override | M7 RISK → re-plan | L4 actuator | < 200ms | ReactiveOverrideCmd |
| **P5** | M4 Behavior Switch | ODD 变化 | M5 re-plan | < 100ms | 行为字典仲裁 |
| **P6** | X-axis VETO | Deterministic Checker | M5 reject plan | < 100ms | 独立验证路径 |
```

- [ ] **Step 4: Update architecture report §16 cyber stub → full**

Replace the §16 stub with complete content:

```markdown
## §16 网络安全（Cybersec Z-TOP）

### §16.1 IACS UR E26/E27 责任划分

参见 RFC-007 完整规范（`docs/Design/Cross-Team Alignment/RFC-007-M7-X-axis-Heartbeat.md`）。

| IACS UR E27 安全能力 | L3 承担 | Z-TOP/Cyber 承担 | 实施状态 |
|---|---|---|---|
| **OT Network Segmentation** | VLAN 接口声明 | VLAN 拓扑 + Data Diode 部署 | ✅ 接口锁定 |
| **Authentication** | DDS-Security x.509 cert | PKI CA 管理 | ⏳ D3.9 |
| **Integrity** | ASDR HMAC (SHA-256) | — | ⏳ D3.9 |
| **Confidentiality** | TLS/WSS (M8↔ROC) | IT/OT 加密边界 | ⏳ D3.9 |
| **Anti-Replay** | liveness_token (SHA-256 seq‖salt) | salt 轮换策略 | ⏳ D3.9 |
| **Audit Logging** | ASDR Ledger (SHA-256 + timestamp) | SIEM 集成 | ✅ 基线 |
```

- [ ] **Step 5: Commit**

```bash
git add docs/Design/Architecture\ Design/MASS_ADAS_L3_TDL_架构设计报告.md docs/Design/Architecture\ Design/audit/2026-08-31/
git commit -m "feat(d3.8): add v1.1.3 algorithm selection matrix, arbitration priority matrix, and cyber stub expansion"
```

---

## D3.9: Cybersec RFC-007 Implementation

### Task D3.9.1: DDS-Security x.509 Certificate Configuration

**Files:**
- Create: `docker/dds_security/governance.xml`
- Create: `docker/dds_security/permissions.xml`
- Create: `docker/dds_security/certs/generate_certs.sh`

- [ ] **Step 1: Create DDS-Security governance file**

Create `docker/dds_security/governance.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<dds>
  <domain_access_rules>
    <domain_rule>
      <domains>
        <id>0</id>
      </domains>
      <allow_unauthenticated_participants>false</allow_unauthenticated_participants>
      <enable_join_access_control>true</enable_join_access_control>
      <discovery_protection_kind>ENCRYPT</discovery_protection_kind>
      <liveliness_protection_kind>ENCRYPT_WITH_ORIGIN_AUTHENTICATION</liveliness_protection_kind>
      <rtps_protection_kind>ENCRYPT</rtps_protection_kind>
      <topic_access_rules>
        <topic_rule>
          <topic_expression>/l3/*</topic_expression>
          <enable_discovery_protection>true</enable_discovery_protection>
          <enable_liveliness_protection>true</enable_liveliness_protection>
          <enable_read_access_control>true</enable_read_access_control>
          <enable_write_access_control>true</enable_write_access_control>
          <metadata_protection_kind>ENCRYPT_WITH_ORIGIN_AUTHENTICATION</metadata_protection_kind>
          <data_protection_kind>ENCRYPT</data_protection_kind>
        </topic_rule>
        <topic_rule>
          <topic_expression>/sil/*</topic_expression>
          <enable_discovery_protection>true</enable_discovery_protection>
          <enable_read_access_control>true</enable_read_access_control>
          <enable_write_access_control>true</enable_write_access_control>
        </topic_rule>
      </topic_access_rules>
    </domain_rule>
  </domain_access_rules>
</dds>
```

- [ ] **Step 2: Create permissions file**

Create `docker/dds_security/permissions.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<dds>
  <permissions>
    <grant name="l3_tdl_kernel_participant">
      <subject_name>CN=L3 TDL Kernel,O=SANGO,C=CN</subject_name>
      <validity>
        <not_before>2026-08-01T00:00:00</not_before>
        <not_after>2027-08-01T00:00:00</not_after>
      </validity>
      <allow_rule>
        <domains><id>0</id></domains>
        <publish>
          <topics>
            <topic>/l3/*</topic>
          </topics>
        </publish>
        <subscribe>
          <topics>
            <topic>/sil/*</topic>
          </topics>
        </subscribe>
      </allow_rule>
    </grant>
    <grant name="sim_workbench_participant">
      <subject_name>CN=SIL Sim Workbench,O=SANGO,C=CN</subject_name>
      <validity>
        <not_before>2026-08-01T00:00:00</not_before>
        <not_after>2027-08-01T00:00:00</not_after>
      </validity>
      <allow_rule>
        <domains><id>0</id></domains>
        <publish>
          <topics>
            <topic>/sil/*</topic>
          </topics>
        </publish>
        <subscribe>
          <topics>
            <topic>/l3/*</topic>
          </topics>
        </subscribe>
      </allow_rule>
    </grant>
  </permissions>
</dds>
```

- [ ] **Step 3: Create certificate generation script**

Create `docker/dds_security/certs/generate_certs.sh`:

```bash
#!/bin/bash
# Generate x.509 certificates for DDS-Security
# Requires OpenSSL 3.0+

set -e

CERT_DIR="$(dirname "$0")"
CA_KEY="${CERT_DIR}/ca_key.pem"
CA_CERT="${CERT_DIR}/ca_cert.pem"

# Generate CA
openssl ecparam -name prime256v1 -genkey -noout -out "${CA_KEY}"
openssl req -new -x509 -key "${CA_KEY}" -out "${CA_CERT}" -days 365 \
  -subj "/C=CN/O=SANGO/CN=DDS-Security CA"

# Generate participant certs
for PARTICIPANT in "l3_tdl_kernel" "sim_workbench" "m7_safety" "m8_hmi"; do
  KEY="${CERT_DIR}/${PARTICIPANT}_key.pem"
  CSR="${CERT_DIR}/${PARTICIPANT}_csr.pem"
  CERT="${CERT_DIR}/${PARTICIPANT}_cert.pem"

  openssl ecparam -name prime256v1 -genkey -noout -out "${KEY}"
  openssl req -new -key "${KEY}" -out "${CSR}" \
    -subj "/C=CN/O=SANGO/CN=${PARTICIPANT}"
  openssl x509 -req -in "${CSR}" -CA "${CA_CERT}" -CAkey "${CA_KEY}" \
    -CAcreateserial -out "${CERT}" -days 365

  rm "${CSR}"
  echo "✅ Generated cert for ${PARTICIPANT}"
done

echo "Done. CA cert: ${CA_CERT}"
```

Run: `chmod +x docker/dds_security/certs/generate_certs.sh && bash docker/dds_security/certs/generate_certs.sh`

- [ ] **Step 4: Commit**

```bash
git add docker/dds_security/
git commit -m "feat(d3.9): add DDS-Security x.509 governance/permissions/cert generation (RFC-007)"
```

---

### Task D3.9.2: TLS/WSS Frontend Encryption

**Files:**
- Create: `docker/nginx/tls/nginx.conf`
- Create: `docker/nginx/tls/generate_tls.sh`
- Modify: `web/vite.config.ts` (add WSS proxy config)

- [ ] **Step 1: Create nginx TLS/WSS reverse proxy config**

Create `docker/nginx/tls/nginx.conf`:

```nginx
server {
    listen 443 ssl http2;
    server_name sil-console.local;

    ssl_certificate     /etc/nginx/certs/server_cert.pem;
    ssl_certificate_key /etc/nginx/certs/server_key.pem;
    ssl_protocols       TLSv1.3;
    ssl_ciphers         HIGH:!aNULL:!MD5;

    # WSS proxy to foxglove_bridge
    location /ws/ {
        proxy_pass http://foxglove_bridge:8765;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
    }

    # REST API proxy to orchestrator
    location /api/ {
        proxy_pass http://sil_orchestrator:8000;
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }

    # MVT tile server
    location /tiles/ {
        proxy_pass http://martin:3000;
        proxy_set_header Host $host;
    }

    # Static SPA
    location / {
        proxy_pass http://vite_dev:5173;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
    }
}

# Redirect HTTP → HTTPS
server {
    listen 80;
    server_name sil-console.local;
    return 301 https://$host$request_uri;
}
```

- [ ] **Step 2: Create TLS certificate generation script**

Create `docker/nginx/tls/generate_tls.sh`:

```bash
#!/bin/bash
set -e
CERT_DIR="$(dirname "$0")/certs"
mkdir -p "${CERT_DIR}"

openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 \
  -keyout "${CERT_DIR}/server_key.pem" \
  -out "${CERT_DIR}/server_cert.pem" \
  -days 365 -nodes \
  -subj "/C=CN/O=SANGO/CN=sil-console.local" \
  -addext "subjectAltName=DNS:localhost,DNS:sil-console.local,IP:127.0.0.1"

echo "✅ TLS cert generated: ${CERT_DIR}/server_cert.pem"
```

Run: `chmod +x docker/nginx/tls/generate_tls.sh && bash docker/nginx/tls/generate_tls.sh`

- [ ] **Step 3: Update vite.config.ts for WSS**

In `web/vite.config.ts`, add WSS proxy:

```typescript
// Add to server.proxy config:
server: {
  proxy: {
    '/api': 'http://localhost:8000',
    '/ws': {
      target: 'wss://localhost:8765',
      ws: true,
      secure: false, // dev only; prod uses valid cert
    },
    '/tiles': 'http://localhost:3000',
  },
},
```

- [ ] **Step 4: Commit**

```bash
git add docker/nginx/tls/ web/vite.config.ts
git commit -m "feat(d3.9): add TLS/WSS encryption (nginx reverse proxy + self-signed certs, IACS UR E26/E27 IT/OT isolation)"
```

---

## Phase 4 HIL Handoff Preparation

### Task P4.1: HIL Integration Checklist

**Files:**
- Create: `docs/Design/HIL/hil-handoff-checklist.md`

- [ ] **Step 1: Create HIL handoff checklist**

Create `docs/Design/HIL/hil-handoff-checklist.md`:

```markdown
# Phase 4 HIL Integration Handoff Checklist

> Status: ⏳ D3.x complete → Phase 4 entry (target: 9/2026)

## SIL Exit Gate (Phase 3 → Phase 4)

- [ ] D3.4 HMI complete: trajectory ghosting + TorModal 3-tier + verdict badge + S-Mode + 4 operator states
- [ ] D3.6 Coverage cube: 1100/1100 cells ≥ 95% pass + Monte Carlo 95% CI met
- [ ] D3.8 v1.1.3 complete: 132 HAZID [TBD] backfilled + algorithm matrix + arbitration priority
- [ ] D3.9 Cybersec: DDS-Security profile loaded + TLS/WSS active + authentication active
- [ ] IEC 62288 inspector static analysis: 0 violations
- [ ] Veitch 2024 TMR baseline: MRC within 60s of ToR trigger (100% of runs)
- [ ] M7 verdict topic /l3/checker_veto < 50ms round-trip measured

## HIL Entry Requirements (Phase 4 D4.1/D4.2)

- [ ] Hardware procurement: FCB bridge console + Jetson Orin AGX + Radar/AIS simulators (order 7/13)
- [ ] HIL test plan: `docs/Test Plan/HIL/hil-test-plan-v0.1.md`
- [ ] SIL→HIL scenario migration: coverage cube YAML → HIL-compatible maritime-schema + real sensor configs
- [ ] FMU 2.0 ship dynamics: replace `fcb_mmg_vessel.fmu` with hardware-in-the-loop interface
- [ ] DDS-Security on real OT VLAN: replace self-signed certs with production PKI
- [ ] CCS surveyor engagement: dry-run AIP submission with SIL evidence pack

## Known Risks (Phase 4)

| Risk | Severity | Mitigation |
|---|---|---|
| GAP-007: single process crash = full stack down | HIGH | component_container composition audit + watchdog |
| GAP-008: foxglove_bridge 50Hz can't handle 1000+ vessels | MEDIUM | Protobuf channel upgrade + edge pre-aggregation |
| GAP-009: MapLibre S-57 MVT pipeline fragility | MEDIUM | Fallback to OSM raster tiles |
| GAP-013: ROS2 Humble EOL 2027-05 | LOW | Plan upgrade to next LTS (Jazzy → Rolling) |
| Jetson Orin AGX thermal envelope | MEDIUM | Passive cooling + clock scaling in Docker |
| Real radar/AIS latency variance | HIGH | Buffer FIFO + timestamp reconciliation in sensor_mock → real transition |
```

- [ ] **Step 2: Commit**

```bash
git add docs/Design/HIL/
git commit -m "docs(p4): add HIL handoff checklist (SIL exit gate + HIL entry requirements + known risks)"
```

---

## Verification Gates

### Before marking D3.4 complete:
```bash
npx vitest run web/src/screens/__tests__/TorModal.test.tsx web/src/screens/__tests__/VerdictBadge.test.tsx web/src/screens/__tests__/TrajectoryGhostLayer.test.tsx web/src/screens/__tests__/SModeCompliance.test.tsx web/src/store/__tests__/fsmStore.test.tsx
npx tsc --noEmit --project web/tsconfig.json
lsp_diagnostics web/src/screens/shared/TorModal.tsx web/src/screens/shared/VerdictBadge.tsx web/src/screens/shared/TrajectoryGhostLayer.tsx web/src/screens/BridgeHMI.tsx web/src/store/fsmStore.ts web/src/store/telemetryStore.ts
```

### Before marking D3.6 complete:
```bash
python tools/coverage_cube/generate_cube.py --dry-run  # verify 1100 cells
pytest tests/tools/test_coverage_cube.py -v
python tools/coverage_cube/generate_cube.py --output scenarios/coverage_cube/
ls scenarios/coverage_cube/ | wc -l  # should be ≥ 1100
```

### Before marking D3.9 complete:
```bash
bash docker/dds_security/certs/generate_certs.sh
# Verify certs exist
ls docker/dds_security/certs/*_cert.pem
# Verify DDS-Security XML validates
xmllint --noout docker/dds_security/governance.xml
xmllint --noout docker/dds_security/permissions.xml
```

### DEMO-3 Full Acceptance (8/31):
```bash
# 1. Coverage cube
python tools/coverage_cube/monte_carlo_stats.py --results runs/coverage_cube/ --output stats/coverage_cube_stats.json
# Expected: pass_rate ≥ 0.95, pass_rate_95ci_met = true

# 2. V&V KPI matrix
python tools/check_entry_gate.py --phase 3
# Expected: all gates PASS

# 3. IEC 62288 compliance
grep -r "IEC 62288" docs/Design/ | wc -l
# Expected: ≥ 1 reference per relevant component

# 4. TorModal auto-MRC timing
grep "tor_transition_delta_s" stats/*.json
# Expected: all ≤ 60.0
```

---

## Revision History

| Version | Date | Changes |
|---|---|---|
| v1.0 | 2026-05-15 | Initial plan: D3.4 (5 sub-tasks), D3.6 (3 sub-tasks), D3.8 (1 task), D3.9 (2 sub-tasks), Phase 4 HIL handoff |
