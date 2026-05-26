---
title: D-DEMO1-R5 — Backend FSM Single Source + HMI LeftDrawer FSM Panel
date: 2026-05-27
author: Subagent via superpowers:writing-plans
estimate_pw: 1.5
blocks: D-DEMO1-R6
blocked_by: D-DEMO1-R1 (evidence)
status: draft (pending main-agent audit)
---

# 1. Motivation

**Evidence base**: F-R1-06 from R1 report — Backend has no `/l3/fsm_state` topic; frontend `fsmStore.ts` is isolated local-only state machine with no connection to actual decision chain.

**Problem**: HMI LeftDrawer panels (① ARPA 目标表 / ② M6 决策溯源) cannot display FSM state (TRANSIT | COLREG_AVOIDANCE | TOR | MRC) because there is no canonical backend source. Operator cannot visualize state machine during DEMO-1 imazu head-on scenario.

**User request**: Add third panel slot above existing ARPA + COLREGs panels to display unified FSM state with active rule, rationale, and transition history.

**Architecture alignment** (CLAUDE.md §3 + §4):
- Per §4 decision #3: CMM (Current Mode Manifest) must be exposed via SAT-1/2/3 interface, aggregated by M8, not maintained internally
- Per §3 m8 role: M8 is "唯一对 ROC/船长说话的实体" — FSM aggregation belongs in M8 group
- Per architecture §12 M8 HMI Bridge: M8 publishes `UIState` @ 50–100 Hz for all transparency data
- Per v3.0 mandate: all transparency outputs must carry `confidence ∈ [0,1]` + `rationale` fields

---

# 2. Goals / Non-goals

## Goals

1. **Backend FSM aggregator node** (new)
   - Subscribe to M1 ODD envelope state, M4 behavior plan, M5 avoidance plan presence, M7 safety alert, M3 mission state
   - Aggregate into unified FSM state: `TRANSIT | COLREG_AVOIDANCE | TOR | OVERRIDE | MRC | HANDBACK`
   - Publish `/l3/fsm_state` @ 10 Hz with state + active_rule + rationale + confidence
   - Define new IDL: `l3_msgs/msg/FsmState.msg`

2. **Frontend store refactor** (fsmStore.ts)
   - Switch from local-only mutation to subscription to `/l3/fsm_state`
   - Preserve hotkey overrides (T/M keys) for dev-only testing; gate behind ENV flag
   - Keep transition history logic (last 100 transitions)

3. **LeftDrawer FSM panel** (new 3rd slot)
   - Insert above existing ARPA Target Table (before ① section)
   - Show: current FSM state (large icon + label), active rule (e.g., "Rule 14 head-on"), rationale text, state history (last 5 transitions)
   - Default expanded; collapse toggle button
   - Match existing LeftDrawer style (300px wide, top:16 bottom:80)

4. **Visual style consistency**
   - Inherit design tokens from existing ① ARPA / ② COLREGs panels
   - Use existing color scheme for FSM state borders (FSM_BORDER / FSM_GLOW constants already exist in SimulationMonitor)

## Non-goals

- **Not refactoring M8 HMI bridge internals** — FSM aggregator is a lightweight stateless Doer, not touching sat_aggregator or tor_protocol
- **Not replacing SAT-1/2/3 architecture** — FSM panel complements, not substitutes, existing transparency outputs
- **Not fixing M4 IvP infeasible or actuator_cmd silent** — those are R3 scope (F-R1-01, F-R1-02); R5 assumes R2/R3/R4 landed
- **Not changing M7 Doer-Checker boundary** — FSM aggregator sits in M8 group (Doer side), never consumes M7 VETO directly; only reads published alerts

---

# 3. FSM State Definition

## 3.1 State machine diagram

```
┌─────────────────────────────────────────────────────────────┐
│                      FSM STATE MACHINE                       │
└─────────────────────────────────────────────────────────────┘

                       [TRANSIT]
                          ▲ ◄─────────────────┐
                          │                   │
                   M4 beh=COLREG_AVOID   Handback complete
                    ↓                         │
                  [COLREG_AVOIDANCE] ────────┘
                          │
                   M7 alert TOR trigger
                    ↓
                    [TOR]
                  (60s window)
                    ↓ (operator accept)
          ┌─────[OVERRIDE]
          │       ↓
          │  (operator steering)
          │       ↓
          └──→[HANDBACK]
                    ↑
        M7 MRC (Emergency override)
                    │
                  [MRC]
                    │
         Recover or escalate

```

## 3.2 Transition table

| From | Event | To | Guard | Notes |
|---|---|---|---|---|
| TRANSIT | M4 behavior_plan.behavior == BEHAVIOR_COLREG_AVOID | COLREG_AVOIDANCE | confidence > 0.5 | M4 IvP solved avoidance plan |
| COLREG_AVOIDANCE | M4 behavior_plan.behavior == BEHAVIOR_TRANSIT | TRANSIT | avoidance_plan expires or waypoints reached | M5 handback signal (WP distance < 50m to current pos) |
| COLREG_AVOIDANCE | M7 SafetyAlert.severity == MRC_REQUIRED | MRC | — | M7 triggered emergency maneuver |
| COLREG_AVOIDANCE | M7 SafetyAlert.severity == CRITICAL (non-MRC) | TOR | alert_type != ALERT_IEC61508_FAULT | Operator intervention needed but not emergency |
| TOR | operator clicks "Acknowledge ToR" | OVERRIDE | sim_time < tmr_deadline_s | Human takes steering wheel |
| OVERRIDE | M1 envelope_state changes to ENVELOPE_OUT or CRITICAL health | MRC | — | System escalates to emergency |
| OVERRIDE | operator releases "manual override" (timeout or explicit) | HANDBACK | — | Handback to autonomous mode |
| MRC | M1 envelope_state returns to ENVELOPE_IN + M7 health GREEN | TRANSIT | confidence > 0.8 | System recovered |
| * | M1 envelope_state == ENVELOPE_OUT continuously > 5s | MRC | — | Fallback to emergency if no explicit recovery |

## 3.3 Mapping table: backend signals → FSM state

| Signal Source | Field | Condition | → State | Confidence |
|---|---|---|---|---|
| M1 ODDState | envelope_state | == ENVELOPE_OUT + duration > 5s | MRC | 0.9 |
| M1 ODDState | envelope_state | == ENVELOPE_IN + no M4 avoid active | TRANSIT | 0.95 |
| M4 BehaviorPlan | behavior | == BEHAVIOR_COLREG_AVOID | COLREG_AVOIDANCE | plan.confidence |
| M7 SafetyAlert | severity | == SEVERITY_MRC_REQUIRED | MRC | alert.confidence |
| M7 SafetyAlert | severity | == SEVERITY_CRITICAL + alert_type != FAULT | TOR | alert.confidence |
| M3 MissionState | state | == AWAITING_ROUTE (L2 missing) | TRANSIT | 0.5 [TBD-R4] |
| M5 AvoidancePlan | active waypoints | < 1 + COLREG_AVOIDANCE state | HANDBACK | 0.8 |
| Operator UI (dev-only) | hotkey: T/M | == pressed | TRANSIT / COLREG_AVOIDANCE (toggle) | 0.2 (dev) |

**Rationale**: Each state is sourced from exactly one authoritative module. No implicit state machines in the aggregator; all transitions are event-driven and visible in topic publish rationale.

---

# 4. Detailed Design

## 4.1 Backend FsmAggregator node

### File location
- **New package**: Create under M8 group structure:
  ```
  src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/fsm_aggregator.cpp
  src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/fsm_aggregator.hpp
  src/l3_tdl_kernel/m8_hmi_transparency_bridge/test/test_fsm_aggregator.cpp
  ```
  OR integrate into existing `hmi_transparency_bridge_node.cpp` as a subcomponent class.

  **Recommendation**: New files (cleaner separation; aligns with sat_aggregator pattern already in M8).

### Subscriptions

```cpp
rclcpp::Subscription<l3_msgs::msg::ODDState>::SharedPtr sub_odd_;
rclcpp::Subscription<l3_msgs::msg::BehaviorPlan>::SharedPtr sub_behavior_;
rclcpp::Subscription<l3_msgs::msg::AvoidancePlan>::SharedPtr sub_avoid_;
rclcpp::Subscription<l3_msgs::msg::SafetyAlert>::SharedPtr sub_safety_;
rclcpp::Subscription<l3_msgs::msg::MissionGoal>::SharedPtr sub_mission_;
```

**QoS**: Reliable, transient_local for ODDState (state preservation); BEST_EFFORT keep_last(5) for all others (snapshot).

### Publisher

```cpp
rclcpp::Publisher<l3_msgs::msg::FsmState>::SharedPtr pub_fsm_;
```

**Topic**: `/l3/fsm_state`  
**Frequency**: 10 Hz (triggered on subscription change or timer)  
**QoS**: BEST_EFFORT keep_last(10) (HMI doesn't need reliability; freshness > history)

### Timer

```cpp
rclcpp::TimerBase::SharedPtr timer_;  // 10 Hz = 100ms
```

### Transition logic (pseudocode)

```cpp
void FsmAggregator::on_timer() {
  // 1. Check M1 envelope hard constraint (highest priority)
  if (last_odd_.envelope_state == ENVELOPE_OUT && 
      duration_in_envelope_out > 5.0s) {
    fsm_state = FsmState::MRC;
    active_rule = "M1 ODD ENVELOPE_OUT > 5s";
    confidence = 0.9;
  }
  
  // 2. Check M7 safety alert (emergency path)
  else if (last_safety_.severity == SEVERITY_MRC_REQUIRED) {
    fsm_state = FsmState::MRC;
    active_rule = last_safety_.recommended_mrm;  // e.g., "MRM-01"
    confidence = last_safety_.confidence;
  }
  else if (last_safety_.severity == SEVERITY_CRITICAL && 
           last_safety_.alert_type != ALERT_IEC61508_FAULT) {
    fsm_state = FsmState::TOR;
    active_rule = fmt::format("SAT-1:{}", last_safety_.description);
    confidence = last_safety_.confidence;
  }
  
  // 3. Check M4 behavior (normal path)
  else if (last_behavior_.behavior == BEHAVIOR_COLREG_AVOID) {
    fsm_state = FsmState::COLREG_AVOIDANCE;
    active_rule = fmt::format("Rule {} head-on", colregs_rule_inferred_from_geometry);
    confidence = last_behavior_.confidence;
  }
  
  // 4. M5 handback signal (back to TRANSIT)
  else if (last_avoid_.waypoints.empty() || 
           distance_to_first_wp < 50.0) {
    fsm_state = FsmState::TRANSIT;
    active_rule = "Avoidance complete / handback";
    confidence = 0.95;
  }
  
  // 5. Default
  else {
    fsm_state = FsmState::TRANSIT;
    active_rule = "Nominal autopilot";
    confidence = last_odd_.confidence;  // inherit M1 confidence
  }
  
  // 6. Publish with timestamp + rationale
  auto msg = std::make_unique<l3_msgs::msg::FsmState>();
  msg->stamp = get_clock()->now();
  msg->schema_version = 1;
  msg->current_state = static_cast<uint8_t>(fsm_state);
  msg->active_rule = active_rule;
  msg->rationale = fmt::format(
    "state={} rule='{}' odd_env={} beh={} safety_sev={} avoid_wp={}",
    fsm_state_str(fsm_state),
    active_rule,
    last_odd_.envelope_state,
    last_behavior_.behavior,
    last_safety_.severity,
    last_avoid_.waypoints.size()
  );
  msg->confidence = confidence;
  
  pub_fsm_->publish(std::move(msg));
}
```

---

## 4.2 IDL: l3_msgs/msg/FsmState.msg

Create new file: `/Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/l3_msgs/msg/FsmState.msg`

```ros2msg
# FSM State aggregated from M1/M4/M5/M7
# Per v1.1.3-pre-stub §12.2 M8 transparency; per CLAUDE.md §3 v3.0 mandate
uint16 schema_version  # 1 (v1.0)
builtin_interfaces/Time stamp

# FSM state enumerator
uint8 current_state
uint8 STATE_TRANSIT = 0
uint8 STATE_COLREG_AVOIDANCE = 1
uint8 STATE_TOR = 2
uint8 STATE_OVERRIDE = 3
uint8 STATE_MRC = 4
uint8 STATE_HANDBACK = 5

# Active rule source (e.g., "Rule 14 head-on", "MRM-01", "SAT-1: Loss of Perception")
string active_rule

# Structured rationale for HMI display
# Format: state=TRANSIT rule='Nominal' odd_env=0 beh=0 safety_sev=0 avoid_wp=0
string rationale

# Confidence [0, 1] per v3.0 mandate (CLAUDE.md §3)
float32 confidence
```

---

## 4.3 Frontend store refactor (fsmStore.ts)

**File**: `/Users/marine/Code/MASS-L3-Tactical Layer/web/src/store/fsmStore.ts`

### Current state (lines 1–43)

```ts
// BEFORE: Zustand store with local-only mutations
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
  // ...
}));
```

### After refactor

```ts
import { create } from 'zustand';
import { useEffect } from 'react';

export type FsmState = 'TRANSIT' | 'COLREG_AVOIDANCE' | 'TOR' | 'OVERRIDE' | 'MRC' | 'HANDBACK';

export interface FsmTransition {
  from: FsmState;
  to: FsmState;
  reason: string;
  timestamp: number;  // sim_time seconds or wall-clock ms
}

export interface TorRequest {
  reason: string;
  triggeredAtSimTime: number;
  tmrDeadlineSimTime: number;
  currentSituation: string;
  proposedAction: string;
  recommendedMrm?: 'MRM-01' | 'MRM-02' | 'MRM-03' | 'MRM-04';
}

interface FsmStore {
  currentState: FsmState;
  activeRule: string;
  confidence: number;
  transitionHistory: FsmTransition[];
  torRequest: TorRequest | null;
  
  // Internal subscriptions
  _subscriptionActive: boolean;
  
  // Mutations (called by subscription handler or dev override)
  _updateState: (
    state: FsmState,
    rule: string,
    confidence: number,
    simTime: number
  ) => void;
  setTorRequest: (req: TorRequest | null) => void;
  clearHistory: () => void;
  
  // Dev-only override (gated by ENV variable)
  _devToggleState: (simTime: number) => void;
}

export const useFsmStore = create<FsmStore>((set, get) => ({
  currentState: 'TRANSIT',
  activeRule: 'Nominal autopilot',
  confidence: 0.95,
  transitionHistory: [],
  torRequest: null,
  _subscriptionActive: false,
  
  _updateState: (nextState, rule, conf, simTime) => set((s) => {
    const transition: FsmTransition = {
      from: s.currentState,
      to: nextState,
      reason: rule,
      timestamp: simTime,
    };
    return {
      currentState: nextState,
      activeRule: rule,
      confidence: conf,
      transitionHistory: [
        ...s.transitionHistory,
        transition,
      ].slice(-100),  // Keep last 100 transitions
    };
  }),
  
  setTorRequest: (req) => set({ torRequest: req }),
  clearHistory: () => set({ transitionHistory: [] }),
  
  _devToggleState: (simTime) => {
    const { currentState } = get();
    // Dev only: T toggles between TRANSIT/COLREG_AVOIDANCE
    if (currentState === 'TRANSIT') {
      get()._updateState('COLREG_AVOIDANCE', '[DEV-TOGGLE] Rule 14', 0.2, simTime);
    } else if (currentState === 'COLREG_AVOIDANCE') {
      get()._updateState('TRANSIT', '[DEV-TOGGLE] Handback', 0.2, simTime);
    }
  },
}));

/**
 * Hook to subscribe fsmStore to /l3/fsm_state topic
 * Use in your top-level app or in LeftDrawer component
 */
export function useFsmStateSubscription() {
  useEffect(() => {
    const store = useFsmStore.getState();
    
    // Guard: only subscribe if WebSocket is connected
    // Vite dev server proxies /foxglove-ws → ws://127.0.0.1:8765 (see web/vite.config.ts L21-37)
    // foxglove-bridge listens on port 8765 (see docker-compose.yml L47-49)
    const wsUrl = `${window.location.protocol === 'https:' ? 'wss:' : 'ws:'}//${window.location.host}/foxglove-ws`;
    const ws = new WebSocket(wsUrl);
    
    ws.onopen = () => {
      console.log('[FSM] WebSocket connected, subscribing to /l3/fsm_state');
      // Foxglove uses JSON subscription format
      ws.send(JSON.stringify({
        type: 'subscribe',
        channel: '/l3/fsm_state',  // ROS2 DDS topic name
      }));
    };
    
    ws.onmessage = (event) => {
      try {
        const data = JSON.parse(event.data);
        if (data.channel === '/l3/fsm_state' && data.message) {
          const msg = data.message;
          const stateMap: Record<number, FsmState> = {
            0: 'TRANSIT',
            1: 'COLREG_AVOIDANCE',
            2: 'TOR',
            3: 'OVERRIDE',
            4: 'MRC',
            5: 'HANDBACK',
          };
          
          const nextState = stateMap[msg.current_state] || 'TRANSIT';
          useFsmStore.getState()._updateState(
            nextState,
            msg.active_rule || 'N/A',
            msg.confidence || 0.5,
            Date.now() / 1000  // Seconds
          );
        }
      } catch (e) {
        console.error('[FSM] Parse error:', e);
      }
    };
    
    ws.onerror = (e) => console.error('[FSM] WebSocket error:', e);
    
    return () => {
      if (ws.readyState === WebSocket.OPEN) ws.close();
    };
  }, []);
}

/**
 * Dev-only hotkey override (gated by VITE_DEV_FSM_OVERRIDE env var)
 */
export function useFsmDevHotkeys() {
  useEffect(() => {
    if (import.meta.env.VITE_DEV_FSM_OVERRIDE !== 'true') {
      return;  // Disabled in production
    }
    
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.key === 't' || e.key === 'T') {
        e.preventDefault();
        useFsmStore.getState()._devToggleState(Date.now() / 1000);
        console.log('[FSM-DEV] Hotkey T triggered FSM toggle');
      }
    };
    
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, []);
}
```

**Key changes**:
- Removed local `setState` mutation; added `_updateState` (read-only from subscription handler)
- Added `_updateState`, `activeRule`, `confidence` fields (mapped from backend `/l3/fsm_state`)
- Added `useFsmStateSubscription()` hook to subscribe to Foxglove WebSocket
- Added `useFsmDevHotkeys()` hook for dev-only toggle (gated by `VITE_DEV_FSM_OVERRIDE` env var)
- Preserved transition history logic (last 100)

**Integration point** (in SimulationMonitor.tsx or App.tsx):
```ts
useEffect(() => {
  useFsmStateSubscription();  // Auto-subscribe on mount
  if (import.meta.env.DEV) {
    useFsmDevHotkeys();  // Enable hotkey override only in dev
  }
}, []);
```

---

## 4.4 LeftDrawer slot insertion

**File**: `/Users/marine/Code/MASS-L3-Tactical Layer/web/src/screens/SimulationMonitor.tsx`

**Current layout** (lines 77–107):
```tsx
function LeftDrawer() {
  return (
    <div style={{ position: 'absolute', top: 16, left: 0, bottom: 80, width: 300, ... }}>
      
      {/* ① ARPA 目标表 */}
      <div style={{ borderBottom: '1px solid var(--line-2)', ... }}>
        <span>① ARPA 目标表</span>
      </div>
      <ArpaTargetTable targets={targets} compact />
      
      {/* ② M6 决策溯源 */}
      <div style={{ borderBottom: '1px solid var(--line-2)', ... }}>
        <span>② M6 决策溯源</span>
      </div>
      <ColregsRationaleTree chain={sat2?.colregs_chain ?? []} ... />
      
    </div>
  );
}
```

**After insertion** (new ③ FSM State panel before ①):
```tsx
function LeftDrawer() {
  const sat2 = useTelemetryStore((s) => s.sat2);
  const targets = useTelemetryStore((s) => s.targets);
  const fsmState = useFsmStore((s) => s.currentState);           // NEW
  const fsmRule = useFsmStore((s) => s.activeRule);             // NEW
  const fsmConf = useFsmStore((s) => s.confidence);             // NEW
  const fsmHistory = useFsmStore((s) => s.transitionHistory);   // NEW
  const [fsmExpanded, setFsmExpanded] = useState(true);         // NEW

  return (
    <div style={{
      position: 'absolute', top: 16, left: 0, bottom: 80, width: 300,
      background: 'rgba(7,12,19,0.92)', backdropFilter: 'blur(8px)',
      borderRight: '1px solid var(--line-2)', zIndex: 20, overflowY: 'auto',
    }}>
      
      {/* ③ FSM STATE PANEL (NEW) */}
      <FsmStatePanel
        state={fsmState}
        activeRule={fsmRule}
        confidence={fsmConf}
        history={fsmHistory}
        expanded={fsmExpanded}
        onToggleExpand={() => setFsmExpanded(!fsmExpanded)}
      />
      
      {/* ① ARPA 目标表 */}
      <div style={{ borderBottom: '1px solid var(--line-2)', padding: '6px 12px' }}>
        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--c-phos)', letterSpacing: '0.1em', textTransform: 'uppercase' }}>
          ① ARPA 目标表
        </span>
      </div>
      <div style={{ maxHeight: 180, overflowY: 'auto' }}>
        <ArpaTargetTable targets={targets} compact />
      </div>

      {/* ② M6 决策溯源 */}
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
```

---

## 4.5 FsmStatePanel component design

**File**: `/Users/marine/Code/MASS-L3-Tactical Layer/web/src/components/FsmStatePanel.tsx` (NEW)

```tsx
import { memo, useState } from 'react';
import { LucideChevronDown, LucideChevronUp } from 'lucide-react';
import type { FsmState, FsmTransition } from '../store/fsmStore';

interface FsmStatePanelProps {
  state: FsmState;
  activeRule: string;
  confidence: number;
  history: FsmTransition[];
  expanded: boolean;
  onToggleExpand: () => void;
}

// State icon & label mapping
const FSM_DISPLAY: Record<FsmState, { icon: string; label: string; color: string }> = {
  TRANSIT: { icon: '🚢', label: 'TRANSIT', color: '#34d399' },
  COLREG_AVOIDANCE: { icon: '⚠️', label: 'COLREG AVOIDANCE', color: '#fbbf24' },
  TOR: { icon: '⛔', label: 'TOR (Operator)', color: '#f87171' },
  OVERRIDE: { icon: '🎮', label: 'OVERRIDE', color: '#f87171' },
  MRC: { icon: '🛑', label: 'EMERGENCY (MRC)', color: '#8b0000' },
  HANDBACK: { icon: '🔄', label: 'HANDBACK', color: '#06b6d4' },
};

export const FsmStatePanel = memo(function FsmStatePanel({
  state,
  activeRule,
  confidence,
  history,
  expanded,
  onToggleExpand,
}: FsmStatePanelProps) {
  const display = FSM_DISPLAY[state];
  const confPercent = Math.round(confidence * 100);
  const recent5 = history.slice(-5).reverse();  // Last 5, newest first

  return (
    <div style={{
      borderBottom: '1px solid var(--line-2)',
      padding: '6px 12px',
      background: display.color + '08',  // Subtle background tint
    }}>
      {/* Header: State badge + collapse toggle */}
      <div style={{
        display: 'flex',
        justifyContent: 'space-between',
        alignItems: 'center',
        marginBottom: expanded ? 8 : 0,
      }}>
        <span style={{
          fontFamily: 'var(--f-disp)',
          fontSize: 9,
          color: 'var(--c-phos)',
          letterSpacing: '0.1em',
          textTransform: 'uppercase',
        }}>
          ③ FSM State
        </span>
        <button
          onClick={onToggleExpand}
          style={{
            background: 'transparent',
            border: 'none',
            color: 'var(--c-text-2)',
            cursor: 'pointer',
            padding: '0 4px',
            display: 'flex',
            alignItems: 'center',
          }}
          title="Toggle FSM details"
        >
          {expanded ? <LucideChevronUp size={14} /> : <LucideChevronDown size={14} />}
        </button>
      </div>

      {/* Content (visible when expanded) */}
      {expanded && (
        <div style={{ fontSize: 12, color: 'var(--c-text-1)', lineHeight: 1.6 }}>
          {/* Current state badge */}
          <div style={{
            display: 'flex',
            alignItems: 'center',
            gap: 8,
            marginBottom: 8,
            padding: 8,
            background: display.color + '12',
            borderRadius: 4,
            border: `1px solid ${display.color}`,
          }}>
            <span style={{ fontSize: 16 }}>{display.icon}</span>
            <div style={{ flex: 1 }}>
              <div style={{ fontWeight: 'bold', color: display.color }}>
                {display.label}
              </div>
              <div style={{ fontSize: 10, color: 'var(--c-text-2)' }}>
                confidence {confPercent}%
              </div>
            </div>
          </div>

          {/* Active rule */}
          <div style={{ marginBottom: 6 }}>
            <div style={{ fontSize: 9, color: 'var(--c-phos)', textTransform: 'uppercase', marginBottom: 2 }}>
              Active Rule
            </div>
            <div style={{
              padding: '4px 6px',
              background: 'var(--bg-2)',
              borderRadius: 2,
              fontFamily: 'var(--f-mono)',
              fontSize: 10,
              color: 'var(--c-info)',
              wordBreak: 'break-word',
            }}>
              {activeRule}
            </div>
          </div>

          {/* Transition history (last 5) */}
          {recent5.length > 0 && (
            <div style={{ marginTop: 6 }}>
              <div style={{ fontSize: 9, color: 'var(--c-phos)', textTransform: 'uppercase', marginBottom: 2 }}>
                History (Last 5)
              </div>
              <FsmHistoryList transitions={recent5} />
            </div>
          )}
        </div>
      )}
    </div>
  );
});
```

---

## 4.6 FsmStateBadge and FsmHistoryList components

**File**: `/Users/marine/Code/MASS-L3-Tactical Layer/web/src/components/FsmStateBadge.tsx` (NEW)

```tsx
import { memo } from 'react';
import type { FsmState } from '../store/fsmStore';

interface FsmStateBadgeProps {
  state: FsmState;
  size?: 'sm' | 'md' | 'lg';
}

const FSM_COLORS: Record<FsmState, string> = {
  TRANSIT: '#34d399',
  COLREG_AVOIDANCE: '#fbbf24',
  TOR: '#f87171',
  OVERRIDE: '#f87171',
  MRC: '#8b0000',
  HANDBACK: '#06b6d4',
};

export const FsmStateBadge = memo(function FsmStateBadge({ state, size = 'md' }: FsmStateBadgeProps) {
  const color = FSM_COLORS[state];
  const padding = size === 'sm' ? '2px 6px' : size === 'lg' ? '6px 12px' : '4px 8px';
  const fontSize = size === 'sm' ? 10 : size === 'lg' ? 14 : 12;

  return (
    <span
      style={{
        display: 'inline-block',
        padding,
        background: color + '20',
        border: `1px solid ${color}`,
        borderRadius: 3,
        color,
        fontWeight: 'bold',
        fontSize,
        fontFamily: 'var(--f-mono)',
        textTransform: 'uppercase',
      }}
    >
      {state}
    </span>
  );
});
```

**File**: `/Users/marine/Code/MASS-L3-Tactical Layer/web/src/components/FsmHistoryList.tsx` (NEW)

```tsx
import { memo } from 'react';
import type { FsmTransition } from '../store/fsmStore';

interface FsmHistoryListProps {
  transitions: FsmTransition[];
}

const ARROW = ' → ';

export const FsmHistoryList = memo(function FsmHistoryList({ transitions }: FsmHistoryListProps) {
  return (
    <div style={{
      display: 'flex',
      flexDirection: 'column',
      gap: 4,
    }}>
      {transitions.map((t, i) => (
        <div key={i} style={{
          padding: '4px 6px',
          background: 'var(--bg-2)',
          borderRadius: 2,
          fontSize: 9,
          fontFamily: 'var(--f-mono)',
          color: 'var(--c-text-2)',
          whiteSpace: 'nowrap',
          overflow: 'hidden',
          textOverflow: 'ellipsis',
        }}>
          <span style={{ color: 'var(--c-info)' }}>{t.from}</span>
          <span style={{ color: 'var(--c-text-2)' }}>{ARROW}</span>
          <span style={{ color: 'var(--c-warn)' }}>{t.to}</span>
          <span style={{ color: 'var(--c-text-3)', marginLeft: 4 }}>
            ({t.reason.substring(0, 30)})
          </span>
        </div>
      ))}
    </div>
  );
});
```

---

## 4.7 Visual style references

**Existing design tokens** (from SimulationMonitor.tsx + shared styles):

```tsx
const FSM_BORDER: Record<string, string> = {
  TRANSIT: 'transparent',
  COLREG_AVOIDANCE: 'transparent',
  TOR: 'var(--c-warn)',
  OVERRIDE: 'transparent',
  MRC: '#8b0000',
};

const FSM_GLOW: Record<string, string> = {
  TOR: '0 0 0 3px rgba(251,191,36,0.4)',
  MRC: '0 0 0 3px rgba(139,0,0,0.6)',
};
```

**LeftDrawer constants**:
- Width: 300px
- Top: 16px
- Bottom: 80px
- Background: `rgba(7,12,19,0.92)` (dark with blur)
- Border color: `var(--line-2)` (subtle gray)
- Font: `var(--f-disp)` (display face), `var(--f-mono)` (monospace)
- Colors: `var(--c-phos)` (phosphor green), `var(--c-text-1)` (primary text)

**FSM panel styling**: Inherit ① ARPA / ② COLREGs header pattern (borderBottom, padding 6px 12px). Use FSM_BORDER/FSM_GLOW for state visual emphasis.

---

# 5. Affected Files

| File | Change | Risk |
|---|---|---|
| `/Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/l3_msgs/msg/FsmState.msg` | NEW | Low (new IDL, no breaking changes) |
| `/Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/fsm_aggregator.cpp` | NEW | Low (new stateless Doer component) |
| `/Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/fsm_aggregator.hpp` | NEW | Low |
| `/Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m8_hmi_transparency_bridge/CMakeLists.txt` | EDIT (add fsm_aggregator sources) | Low |
| `/Users/marine/Code/MASS-L3-Tactical Layer/web/src/store/fsmStore.ts` | EDIT (refactor to subscription; keep history logic) | Medium (behavior change: local → remote source) |
| `/Users/marine/Code/MASS-L3-Tactical Layer/web/src/screens/SimulationMonitor.tsx` | EDIT (insert FsmStatePanel before ①) | Medium (layout reflow; must preserve ARPA/COLREGs) |
| `/Users/marine/Code/MASS-L3-Tactical Layer/web/src/components/FsmStatePanel.tsx` | NEW | Low |
| `/Users/marine/Code/MASS-L3-Tactical Layer/web/src/components/FsmStateBadge.tsx` | NEW | Low |
| `/Users/marine/Code/MASS-L3-Tactical Layer/web/src/components/FsmHistoryList.tsx` | NEW | Low |
| `/Users/marine/Code/MASS-L3-Tactical Layer/docker/sil_topic_bridge.py` | EDIT (add `/l3/fsm_state` to forward list if not already) | Low |
| `/Users/marine/Code/MASS-L3-Tactical Layer/.env.example` | EDIT (add `VITE_DEV_FSM_OVERRIDE=false`) | Very Low (dev config) |

---

# 6. Implementation Steps

1. **Define IDL** (30 min)
   - Create `/Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/l3_msgs/msg/FsmState.msg`
   - Add schema_version, current_state (enum), active_rule, rationale, confidence fields
   - Update CMakeLists in l3_msgs to generate rosidl code

2. **Backend FSM aggregator** (45 min)
   - Create `/Users/marine/Code/MASS-L3-Tactical Layer/src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/fsm_aggregator.{cpp,hpp}`
   - Implement 5 subscriptions (M1/M4/M5/M7/M3), 1 publisher (`/l3/fsm_state`), 10 Hz timer
   - Implement state machine transition logic (pseudocode in §4.1)
   - Update M8 package CMakeLists to compile fsm_aggregator
   - Verification: `docker exec ... ros2 topic hz /l3/fsm_state --window 5` shows ~10 Hz

3. **Frontend store refactor** (30 min)
   - Refactor `fsmStore.ts` to remove local setState, add `_updateState` mutation
   - Add `useFsmStateSubscription()` hook (WebSocket → Foxglove → `/l3/fsm_state`)
   - Add `useFsmDevHotkeys()` hook for dev-only T/M toggle (gated by VITE_DEV_FSM_OVERRIDE)
   - Export dev hooks for use in SimulationMonitor

4. **Frontend components** (30 min)
   - Create FsmStatePanel component (header + badge + rule + history)
   - Create FsmStateBadge component (styled pill)
   - Create FsmHistoryList component (last 5 transitions)

5. **LeftDrawer integration** (15 min)
   - Insert FsmStatePanel before ① ARPA section in SimulationMonitor.tsx
   - Add state/rule/confidence/history selectors from useFsmStore
   - Add expand/collapse state management

6. **Docker integration** (15 min)
   - Update `docker/sil_topic_bridge.py` to forward `/l3/fsm_state` if not already in forward list
   - Rebuild sil-nodes container

7. **Configuration** (10 min)
   - Add `VITE_DEV_FSM_OVERRIDE=false` to `.env.example`
   - Set `VITE_DEV_FSM_OVERRIDE=true` in dev `.env.local` to enable hotkeys

---

# 7. Verification Plan

## 7.1 Backend smoke test

After backend compilation:

```bash
docker compose up -d
docker exec mass-l3-tacticallayer-sil-nodes-1 bash -c '
  source /opt/ros/humble/setup.bash
  source /opt/ws/install/setup.bash
  ros2 topic hz /l3/fsm_state --window 10
'
```

**Expected**: `average rate: 10.05 Hz` (±0.5 Hz)

## 7.2 Content verification

```bash
docker exec mass-l3-tacticallayer-sil-nodes-1 bash -c '
  source /opt/ros/humble/setup.bash
  source /opt/ws/install/setup.bash
  ros2 topic echo --once /l3/fsm_state
'
```

**Expected output**:
```yaml
stamp: { sec: ..., nanosec: ... }
schema_version: 1
current_state: 0  # 0 = TRANSIT initially
active_rule: 'Nominal autopilot'
rationale: 'state=TRANSIT rule=Nominal odd_env=0 beh=0 safety_sev=0 avoid_wp=0'
confidence: 0.95
```

## 7.3 Frontend subscription test

1. **WebSocket connectivity test** (prerequisite):
   ```bash
   # Verify foxglove-bridge is running on port 8765
   docker compose ps | grep foxglove
   # Test direct WebSocket connection
   wscat -c ws://127.0.0.1:8765
   # Or via Vite proxy:
   wscat -c ws://127.0.0.1:5173/foxglove-ws
   ```
   Expected: Connection established without error.

2. Open HMI at `http://localhost:5173/monitor/imazu-01-ho`
 3. Verify ③ FSM State panel appears above ① ARPA table
 4. Verify current_state badge shows "TRANSIT" (green, ✅)
 5. Check browser console: `[FSM] WebSocket connected, subscribing to /l3/fsm_state`

## 7.4 Imazu scenario transition trace (assumes R2+R3+R4 landed)

Precondition: `/l2/planned_route` publishing (R4 mock L2), M4 IvP solver working (R3 fix), actuator_cmd forwarding (R3 fix).

1. T+0s: Current state = TRANSIT
   - Panel shows: TRANSIT (green), rule="Nominal autopilot"

2. T+~300s (ownship heading toward target): Current state = COLREG_AVOIDANCE
   - Panel shows: COLREG_AVOIDANCE (amber), rule="Rule 14 head-on"
   - History shows: TRANSIT → COLREG_AVOIDANCE

3. T+~600s (avoidance complete, ownship at right heading): Current state = TRANSIT
   - Panel shows: TRANSIT (green), rule="Avoidance complete / handback"
   - History shows last 5: ... → COLREG_AVOIDANCE → TRANSIT

## 7.5 Frontend visual stability

- ① ARPA Target Table and ② M6 COLREGs Rationale Tree remain visible (no layout collapse)
- LeftDrawer total height: top:16 + 3 panels + bottom:80 should fit without vertical overflow
- Test on 1280×800 (desktop tablet preset) to verify responsive layout

## 7.6 Hotkey backward compat (dev-only)

With `VITE_DEV_FSM_OVERRIDE=true`:
1. Press T key
2. Verify FSM state toggles between TRANSIT ↔ COLREG_AVOIDANCE
3. Check console: `[FSM-DEV] Hotkey T triggered FSM toggle`
4. Verify hotkey override is **clearly marked** in code as dev-only (env-gated)

## 7.7 Acceptance criteria

- ✅ `/l3/fsm_state` publishes @ 10 Hz with valid FsmState message (schema_version=1)
- ✅ Frontend FsmStatePanel renders in LeftDrawer above ① ARPA, below ③ header
- ✅ FSM state matches backend source (TRANSIT → COLREG_AVOIDANCE → TRANSIT trace on imazu)
- ✅ Active rule displays (e.g., "Rule 14 head-on")
- ✅ Confidence [0,1] displayed as percentage
- ✅ Transition history shows last 5 (timestamp + reason + from → to)
- ✅ Expand/collapse toggle functional
- ✅ Hotkey override works when `VITE_DEV_FSM_OVERRIDE=true`, disabled otherwise
- ✅ ARPA + COLREGs panels remain visible (no layout regression)

---

# 8. Risks

| Risk | Severity | Mitigation |
|---|---|---|
| **WebSocket subscription race condition** | Medium | Ensure `useFsmStateSubscription()` only runs once (useEffect dependency guard); defer subscribe until Foxglove ready |
| **Layout reflow in LeftDrawer** | Medium | Test on 3 screen sizes (mobile/tablet/desktop); set explicit maxHeight on FsmStatePanel |
| **M4 confidence still low during fallback (R3 dependency)** | Medium | FSM inherits M4 confidence; if R3 not landed, FSM confidence will be 0.3 (acceptable; mark as TBD-R3 in rationale) |
| **M7 alert message format variation** | Low | Fallback to generic description if recommended_mrm not set; format string defensively |
| **Hotkey conflicts with browser/OS shortcuts** | Low | T key is unlikely conflict; test on target OS (macOS/Linux); document as dev-only |

---

# 9. Out of Scope

- **Avoidance behavior fix** (F-R1-01, M4 IvP infeasible) — R3 scope
- **Actuator_cmd forwarding break** (F-R1-02, bridge silent) — R3 scope
- **Mock L2 publisher** (F-R1-04, `/l2/planned_route` missing) — R4 scope
- **Refactor M8 HMI bridge internals** — sat_aggregator, tor_protocol, asdr_logger untouched
- **Replace SAT-1/2/3 architecture** — FSM panel complements, not substitutes
- **M7 Doer-Checker internal rewire** — FSM reads published alerts only
- **Multi-screen layout** — LeftDrawer layout fixed to one primary panel set
- **Certification evidence collection** — R5 is functional delivery, not cert artifacts

---

# 10. Open Questions [TBD-<reason>]

1. **[TBD-HAZID]** When is M4 behavior = COLREG_AVOID considered "confidence > 0.5"?  
   - Root cause: F-R1-01 shows M4 fallback confidence = 0.3, so transition to COLREG_AVOIDANCE never fires  
   - Resolution: R3 plan must fix M4 IvP solver; R5 will inherit correct confidence  
   - Fallback: Set threshold to 0.2 if R3 slip; FSM will show COLREG_AVOIDANCE even in fallback (cosmetic)

2. **[TBD-R3]** Does M5 AvoidancePlan empty waypoints list when no avoidance is active?  
   - Root cause: F-R1-05 shows M5 outputs "micro-step original position wp" even in fallback  
   - Resolution: R3 plan must clarify M5 behavior; R5 handback trigger may need adjustment  
   - Fallback: Check `waypoints.size() < 1` AND `current_state == COLREG_AVOIDANCE` AND `timestamp - last_avoid_ts > 2s`

3. **[TBD-frontend-init]** Does fsmStore hydrate from `/l3/fsm_state` or start at TRANSIT default?  
   - Root cause: WebSocket connection latency; first frame may show stale TRANSIT  
   - Resolution: Accept 1–2 frame latency; add visual "syncing..." spinner on ③ until first message  
   - Fallback: Mark as [DEV] and document in release notes

---

**Plan ready for main-agent audit and execution.**
