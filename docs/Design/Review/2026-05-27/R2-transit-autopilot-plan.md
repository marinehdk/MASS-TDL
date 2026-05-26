---
title: D-DEMO1-R2 — TRANSIT Autopilot (heading-hold + speed-hold)
date: 2026-05-27
author: subagent (Claude Haiku 4.5)
estimate_pw: 1.5
blocks: D-DEMO1-R6 (M5 MPC chain hardens)
blocked_by: D-DEMO1-R1 (evidence baseline)
status: draft (pending main-agent audit)
---

# 1. Motivation

From R1 evidence (§3.4–3.5), the decision chain fails in TRANSIT mode (no active avoidance):

- **F-R1-02**: `/sil/actuator_cmd` goes silent after imazu activation → ship SOG drifts from YAML 10 kn to 5.3 kn in ~12 seconds (loss rate ≈ −0.18 m/s²).
- **F-R1-05**: M5 `avoidance_plan.waypoints[0].target_speed_kn` self-feeds from ownship current SOG (not YAML nominal 10 kn) → cascading deceleration spiral.
- **F-R1-01**: M4 IvP solver returns infeasible fallback → M5 input is degraded (speed_max = ownship current SOG, heading unconstrained).

**Root cause**: The bridge `_on_avoidance_plan()` callback (line 309 in `sil_topic_bridge.py`) waits for M5 to publish a real plan, but M5 only publishes when M4 succeeds. Until R3 fixes M4 IvP and R4 mocks L2, no valid plan arrives → no actuator command → ship loses propulsion.

**R2 bridges the gap**: During imazu TRANSIT mode (Rule 14 head-on, no maneuver yet), a simple autopilot maintains YAML-nominal heading (0°) and SOG (10 kn) until M4 produces a real avoidance plan OR a manual override is triggered.

---

# 2. Goals / Non-goals

## Goals

- Maintain heading ≤ 1° drift from YAML initial value over 60 seconds (no wind/current in DEMO).
- Maintain SOG ≤ 0.5 kn drift from YAML initial value over 60 seconds.
- Smoothly yield actuator authority to M5 when a valid `avoidance_plan` arrives (preemption, not interruption).
- Publish `/sil/actuator_cmd` at ≥ 1 Hz (heartbeat) so ship_dynamics node always has a reference.
- Work **independently** of R3 (M4 fix) and R4 (L2 mock) — fallback-safe.
- Zero dependency on L2 PlannedRoute or M3 mission_state (both silent in R1 baseline).

## Non-goals

- Implement full MPC / path following (that is M5's job in R3/R6).
- Implement L2 route planning or mission scheduling (R4's scope).
- Fix M4 IvP infeasible (R3 scope).
- Implement FSM visualization (R5 scope).
- Yaw-rate limiting or anti-sway control (L4/L5 scope, not L3 TRANSIT mode).

---

# 3. Design Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         L3 Tactical Kernel                              │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │ M1 ODD Envelope  M2 World  M3 Mission  M4 Behavior  M6 COLREGs   │   │
│  │  (TRANSIT mode)    (none)    (AWAIT)   (fallback)    (Rule 14)   │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│                                 │                                        │
│                    [NEW] ┌──────▼────────┐                              │
│                     TRANSIT │   Autopilot │ (heading + speed hold)      │
│                     Autopilot└──────┬─────┘ @ 2 Hz (or on M5 override)  │
│                           (extends bridge)                               │
│                                 │                                        │
│  ┌──────────────────────────────▼──────────────────────────────────┐   │
│  │ M5 Tactical Planner (avoidance_plan fallback: empty/micro-WP)   │   │
│  └──────────────────────────────┬──────────────────────────────────┘   │
│                                 │                                        │
└──────────────────────────────────┼────────────────────────────────────────┘
                                   │
                          ┌────────▼───────┐
                          │ bridge.py       │
                          │ _on_avoidance   │
                          │ _plan() + [NEW] │
                          │ TRANSIT fallback│
                          └────────┬────────┘
                                   │
                          ┌────────▼───────────┐
                          │ /sil/actuator_cmd  │
                          │ (rudder + throttle)│
                          └────────┬───────────┘
                                   │
                          ┌────────▼──────────┐
                          │ ship_dynamics_node│
                          │ (50 Hz dynamics)  │
                          └────────┬──────────┘
                                   │
                          ┌────────▼────────────┐
                          │ /sil/own_ship_state │
                          │ position/heading/sog│
                          └─────────────────────┘
```

**Placement**: New logic **extends the bridge's `_on_avoidance_plan()` callback** (no separate ROS2 node). Simple PID controllers (1 heading, 1 speed) run at 2 Hz, triggered by a timer.

**Activation condition**:
- M1 reports `ODD_STATE == TRANSIT` (from `/l3/m1/odd_state`) AND
- No valid avoidance_plan received in the last N=10 seconds (timestamp check)

**Handover protocol**:
- When M5 publishes a valid plan (e.g., non-empty waypoints with turn_radius > epsilon), bridge switches to M5 mode (passes through plan-derived commands).
- TRANSIT autopilot remains dormant while M5 plan is fresh.
- If M5 plan goes stale (>10s silent), autopilot re-engages.

---

# 4. Detailed Design

## 4.1 New Module Placement: Bridge Extension

**Why extend bridge, not create new node?**
1. Minimal lifecycle complexity — bridge already publishes `/sil/actuator_cmd`.
2. No new ROS2 node → no new lifecycle service registration.
3. Timing: bridge runs at 10–50 Hz (event-driven), autopilot needs just 2 Hz slow loop → simple timer inside bridge.
4. Shares ODD state subscription (already listening to M1 heartbeat).

**Alternative (rejected)**: Create a new `transit_autopilot_node` in `sil_nodes/`. Cost: new Dockerfile RUN, lifecycle broadcast, parameter injection, test harness. Benefit: decoupling. **Verdict**: Too heavyweight for DEMO-1 (1.5 pw estimate would balloon). Bridge extension is surgical.

## 4.2 Activation Logic

```python
# Pseudocode: inside bridge __init__
self._autopilot_enabled = False
self._last_valid_plan_time = None  # time.monotonic() of last non-empty M5 plan
self._last_odd_state = None  # latest M1 ODD state

# New subscription: M1 ODD state
self._sub_m1_odd = self.create_subscription(
    ODDState, "/l3/m1/odd_state",
    self._on_odd_state_changed, _sensor_qos())

# New timer: autopilot heartbeat @ 2 Hz
self._autopilot_timer = self.create_timer(0.5, self._autopilot_step)

def _on_odd_state_changed(self, msg: ODDState) -> None:
    self._last_odd_state = msg
    # ODD state change may enable/disable autopilot; checked in _autopilot_step

def _autopilot_step(self) -> None:
    """Runs @ 2 Hz. Decides whether TRANSIT autopilot should be active."""
    if self._last_odd_state is None:
        return
    
    # Activation threshold: ENVELOPE_IN (TRANSIT-equivalent) + no fresh M5 plan OR M4 in fallback
    now = time.monotonic()
    staleness = (now - self._last_valid_plan_time) if self._last_valid_plan_time else float('inf')
    STALE_THRESHOLD_S = 10.0
    
    is_in_envelope = (self._last_odd_state.envelope_state == ODDState.ENVELOPE_IN)
    is_m5_stale = staleness > STALE_THRESHOLD_S
    m4_in_fallback = (self._last_behavior_plan and 
                      "fallback" in self._last_behavior_plan.rationale.lower())
    
    self._autopilot_enabled = is_in_envelope and (is_m5_stale or m4_in_fallback)

def _on_avoidance_plan(self, msg: AvoidancePlan) -> None:
    # ... existing code (line 309) ...
    
    # [NEW] Check plan validity and record timestamp
    has_valid_plan = (
        len(msg.waypoints) > 0 and
        msg.waypoints[0].turn_radius_m > 1e-6  # real turn, not placeholder
    )
    if has_valid_plan:
        self._last_valid_plan_time = time.monotonic()
    
    # [NEW] If autopilot is active, override the plan with autopilot output
    if self._autopilot_enabled and not has_valid_plan:
        out = self._compute_transit_autopilot()
    else:
        # Original M5 passthrough logic
        out = SilOwnShipState()
        # ... existing rudder/throttle extraction ...
    
    self._pub_act.publish(out)
```

**Activation criteria**:
- `M1.envelope_state == ENVELOPE_IN` (from `/l3/m1/odd_state.envelope_state`, uint8 enum per ODDState.msg) — architecture decision: ODD is the only authority (CLAUDE.md §4 Decision 1). ENVELOPE_IN (value 0) corresponds to "system within operational envelope" = TRANSIT-equivalent.
- `time.monotonic() - last_valid_plan_time > 10 seconds` — M5 plan is stale.
- **OR** `M4 behavior_plan.rationale` contains "fallback" — M4 is in degraded mode (per R3 §4.1.1, M4 outputs "IvP infeasible fallback" or "Failsafe TRANSIT" when mission context absent).
- Covers the gap until R3 (M4 fix) allows M5 to produce real plans.

---

## 4.3 Handover Protocol

**Phase A: Autopilot Active (M5 stale)**
1. Bridge timer fires @ 2 Hz → `_autopilot_step()` checks conditions.
2. If both conditions true → `_autopilot_enabled = True`.
3. Next `_on_avoidance_plan()` call checks autopilot flag + plan validity.
4. If autopilot active + plan invalid → compute `(rudder, throttle)` from PID.

**Phase B: M5 Publishes Real Plan (handover)**
1. M5 publishes valid `avoidance_plan` with `turn_radius_m > epsilon`.
2. `_on_avoidance_plan()` receives it, sets `_last_valid_plan_time = now()`.
3. `_autopilot_enabled` **remains True** until next timer tick (≤500ms).
4. Next `_on_avoidance_plan()` call: `has_valid_plan = True` → bypass autopilot, use M5 passthrough.
5. Handover complete; autopilot dormant.

**Phase C: M5 Goes Silent Again (reactivation)**
1. If M5 stops publishing for > 10s (orchestrator issue, MPC timeout, etc.).
2. Next timer tick → `staleness > 10s` → `_autopilot_enabled = True`.
3. Autopilot resumes heading/speed hold.

**Why this works** ✅:
- No race condition: M5 overrides are checked in the same callback (`_on_avoidance_plan`), not in competing threads.
- Graceful degradation: if M5 falters, autopilot catches the ship mid-maneuver and returns to TRANSIT nominal.
- CCS-auditable: state transition is logged in bridge `get_logger().info()`.

---

## 4.4 PID / Controller Choice

### Heading Hold

```python
class HeadingController:
    """Simple P-only controller for heading. Rate-limited to avoid jerky rudder."""
    
    def __init__(self):
        self.Kp_heading = 10.0  # [deg rudder / deg error]
        self.max_rudder_rate = 5.0  # [deg/s] — realistic rate limit
        self.last_rudder_cmd = 0.0
    
    def step(self, heading_error_deg: float, dt: float) -> float:
        """
        Args:
            heading_error_deg: (target_heading - current_heading), wrapped to [-180, 180]
            dt: timestep (0.5 s @ 2 Hz)
        
        Returns:
            rudder_angle_rad: clamped to ±MAX_RUDDER_RAD (35° for DEMO)
        """
        # P-only (no integral, no derivative to keep it simple)
        rudder_cmd_deg = self.Kp_heading * heading_error_deg
        
        # Rate limiting
        max_delta_deg = self.max_rudder_rate * dt  # 2.5° per 0.5s tick
        rudder_cmd_deg = np.clip(
            rudder_cmd_deg,
            self.last_rudder_cmd - max_delta_deg,
            self.last_rudder_cmd + max_delta_deg
        )
        
        # Saturation
        rudder_cmd_deg = np.clip(rudder_cmd_deg, -35.0, 35.0)
        self.last_rudder_cmd = rudder_cmd_deg
        
        return np.radians(rudder_cmd_deg)
```

**Rationale**:
- **P-only**: Heading hold in calm water (no wind/current in DEMO). Integral action unnecessary and risks windup.
- **Rate limit**: Prevents chattering & mimics real autopilot soft-start. 5°/s is reasonable for ~46m vessel.
- **Saturation**: 35° max rudder matches DEMO-1 scenario constraints (CLAUDE.md §15: MAX_RUDDER_DEG).

**Tuning** [TBD-validation]:
- `Kp_heading = 10.0` assumes linear turn-rate model. If actual turn response is nonlinear, may need 3–15 Hz closed-loop validation.
- Expect oscillation ±1° peak around target 0°; steady-state error < 0.1° (good enough for COLREGs Rule 14).

### Speed Hold

```python
class SpeedController:
    """Simple PI controller for speed. Anti-windup + rate limiting."""
    
    def __init__(self):
        self.Kp_speed = 0.15  # [throttle / kn error]
        self.Ki_speed = 0.02  # [throttle / (kn·s)]
        self.max_throttle_rate = 0.5  # [1/s] — soft ramp
        self.integral = 0.0  # [kn·s] accumulator
        self.last_throttle_cmd = 0.0
    
    def step(self, speed_error_kn: float, dt: float) -> float:
        """
        Args:
            speed_error_kn: (target_sog - current_sog), typically small
            dt: 0.5 s @ 2 Hz
        
        Returns:
            throttle_cmd: [0, 1], normalized to MAX_SPEED_KN (25 kn)
        """
        # Proportional term
        p_term = self.Kp_speed * speed_error_kn
        
        # Integral term with anti-windup (clamp accumulator)
        self.integral += speed_error_kn * dt
        self.integral = np.clip(self.integral, -5.0, 5.0)
        i_term = self.Ki_speed * self.integral
        
        # PI output
        throttle_cmd = p_term + i_term
        
        # Rate limiting
        max_delta = self.max_throttle_rate * dt  # 0.25 per 0.5s
        throttle_cmd = np.clip(
            throttle_cmd,
            self.last_throttle_cmd - max_delta,
            self.last_throttle_cmd + max_delta
        )
        
        # Saturation [0, 1]
        throttle_cmd = np.clip(throttle_cmd, 0.0, 1.0)
        self.last_throttle_cmd = throttle_cmd
        
        return throttle_cmd
```

**Rationale**:
- **PI** (not PID): Integrator removes steady-state speed error due to water resistance. Derivative unnecessary (no sensor noise in SIL).
- **Anti-windup**: Clamp integral accumulator to ±5 to prevent integrator saturation at hard rudder limits.
- **Rate limit**: 0.5/s throttle ramp ≈ 2 seconds to full ahead from idle (realistic propulsion soft-start).

**Tuning** [TBD-validation]:
- `Kp_speed = 0.15` / `Ki_speed = 0.02` assume linear throttle-to-thrust mapping. Actual MMG model is nonlinear (X_uu term). Expect 5–15 seconds to reach target SOG from step input.
- Integral anti-windup bound 5.0 empirically chosen; may need adjustment post-validation.

---

## 4.5 Reference Source: YAML Initial Values

**Where to read them?**

Option A: **Publish to ROS2 parameter server** (preferred, centralized).
- Lifecycle bridge already injects scenario YAML → ROS2 params in `_inject_params_to_node()` (line ~200 in `lifecycle_bridge.py`).
- Ship dynamics receives `n_rps_initial` this way (line 145 in `ship_dynamics/node.py`).
- **ACTION**: Extend param injection to include:
  ```python
  {
    "ownship_initial_heading_deg": 0.0,      # from scenario.ownShip.initial.heading
    "ownship_initial_sog_kn": 10.0,          # from scenario.ownShip.initial.sog
    "transit_autopilot_enabled": True        # runtime control
  }
  ```
- Bridge reads these params once at startup (or listen for param change events).

Option B: **Hardcode in bridge** (anti-pattern, inflexible).
- Quick POC, but violates CLAUDE.md §4 Decision 4 (no ship-type constants in A-layer).
- Rejected.

**Implementation**:
```python
# In SilTopicBridge.__init__:
self.declare_parameter("ownship_initial_heading_deg", 0.0)
self.declare_parameter("ownship_initial_sog_kn", 10.0)
self.target_heading_deg = self.get_parameter("ownship_initial_heading_deg").value
self.target_sog_kn = self.get_parameter("ownship_initial_sog_kn").value

# In _autopilot_step():
# Use self.target_heading_deg, self.target_sog_kn as PID references
```

---

## 4.6 Topic / Message Changes

| Topic | Current | New? | Reason |
|---|---|---|---|
| `/sil/actuator_cmd` | OwnShipState (rudder + throttle) | No change | Reuse existing message. |
| `/l3/m1/odd_state` | Subscribed (heartbeat only) | **Add callback** | Extract `envelope_state` field (uint8 enum: ENVELOPE_IN=0/EDGE=1/OUT=2/MRC_PREP=3/MRC_ACTIVE=4) to check TRANSIT-equivalent mode. |
| ROS2 params | `n_rps_initial` only | **Add 2 params** | `ownship_initial_heading_deg`, `ownship_initial_sog_kn`. |
| No new topics | — | **No** | Autopilot output feeds existing `/sil/actuator_cmd`. |

---

# 5. Affected Files (absolute paths from repo root)

| File | Reason | Risk |
|---|---|---|
| `/src/sil_orchestrator/lifecycle_bridge.py` | Extend `_inject_params_to_node()` to add YAML heading/sog params | **Low**: isolated method, no side effects on lifecycle transitions. Testable in isolation. |
| `/docker/sil_topic_bridge.py` | Add autopilot timer + PID controllers + activation logic (lines 309–340 in `_on_avoidance_plan` + new methods). | **Medium**: touches hot path (1 Hz actuator cmd). Must ensure no timing regressions (Hz remains ≥1). Thread-safe access to `_last_odd_state` / `_autopilot_enabled` (use locks if needed). |
| `scenarios/IMAZU标准测试/imazu-01-ho.yaml` | No changes needed (params injected at runtime). | **None**: read-only. |
| `src/sim_workbench/sil_nodes/ship_dynamics/ship_dynamics/node.py` | No changes (autopilot feeds existing `/sil/actuator_cmd` which ship_dynamics already consumes). | **None**: read-only. |

---

# 6. Implementation Steps

### Step 1: Extend Scenario Parameter Injection
**File**: `/src/sil_orchestrator/lifecycle_bridge.py`  
**Action**: Modify `_inject_params_to_node()` method to extract heading/sog from scenario YAML.

```python
# Inside LifecycleBridge._inject_params_to_node(scenario_id: str, ...)
# After loading scenario YAML:

own_initial = scenario_data["ownShip"]["initial"]
heading_deg = own_initial.get("heading", 0.0)
sog_kn = own_initial.get("sog", 10.0)

params_to_set = [
    Parameter(
        name="ownship_initial_heading_deg",
        value=ParameterValue(double_value=heading_deg)
    ),
    Parameter(
        name="ownship_initial_sog_kn",
        value=ParameterValue(double_value=sog_kn)
    ),
]

# Inject into ship_dynamics_node
await self._call_set_parameters("ship_dynamics_node", params_to_set)
```

**Verification**: 
```bash
docker exec mass-l3-tacticallayer-sil-nodes-1 bash -c '
  source /opt/ros/humble/setup.bash
  ros2 param get /ship_dynamics_node ownship_initial_heading_deg
  ros2 param get /ship_dynamics_node ownship_initial_sog_kn
'
# Expected: heading = 0.0, sog = 10.0 (for imazu-01-ho)
```

### Step 2: Add Bridge Heading/Speed Controllers
**File**: `/docker/sil_topic_bridge.py`  
**Action**: Add HeadingController and SpeedController classes (before SilTopicBridge class definition, lines ~119).

```python
import numpy as np
import time

class HeadingController:
    """P-only heading autopilot @ 2 Hz."""
    def __init__(self, Kp=10.0, max_rate_deg_s=5.0):
        self.Kp = Kp
        self.max_rate_deg_s = max_rate_deg_s
        self.last_cmd_deg = 0.0
    
    def step(self, error_deg: float, dt: float) -> float:
        # Wrap error to [-180, 180]
        error_deg = (error_deg + 180) % 360 - 180
        
        cmd_deg = self.Kp * error_deg
        max_delta = self.max_rate_deg_s * dt
        cmd_deg = np.clip(cmd_deg, self.last_cmd_deg - max_delta, self.last_cmd_deg + max_delta)
        cmd_deg = np.clip(cmd_deg, -35.0, 35.0)
        self.last_cmd_deg = cmd_deg
        return np.radians(cmd_deg)

class SpeedController:
    """PI speed autopilot @ 2 Hz."""
    def __init__(self, Kp=0.15, Ki=0.02, max_rate=0.5):
        self.Kp = Kp
        self.Ki = Ki
        self.max_rate = max_rate
        self.integral = 0.0
        self.last_cmd = 0.0
    
    def step(self, error_kn: float, dt: float) -> float:
        p_term = self.Kp * error_kn
        self.integral += error_kn * dt
        self.integral = np.clip(self.integral, -5.0, 5.0)
        i_term = self.Ki * self.integral
        
        cmd = p_term + i_term
        max_delta = self.max_rate * dt
        cmd = np.clip(cmd, self.last_cmd - max_delta, self.last_cmd + max_delta)
        cmd = np.clip(cmd, 0.0, 1.0)
        self.last_cmd = cmd
        return cmd
```

**Verification**: 
```bash
python3 -c "
import sys; sys.path.insert(0, '/path/to/docker')
from sil_topic_bridge import HeadingController, SpeedController
hc = HeadingController(); print(hc.step(5.0, 0.5))  # Should output small rad value
sc = SpeedController(); print(sc.step(-0.5, 0.5))  # Should output small throttle
"
```

### Step 3: Extend SilTopicBridge.__init__ for Autopilot Setup
**File**: `/docker/sil_topic_bridge.py`, lines ~124–206  
**Action**: Add autopilot state variables, M1 subscription, and timer.

```python
class SilTopicBridge(Node):
    def __init__(self) -> None:
        super().__init__("sil_topic_bridge")
        self.get_logger().info("[sil_topic_bridge] Bridge active")
        
        # ... existing QoS + subscriptions (lines 128–200) ...
        
        # [NEW] Autopilot state
        self._autopilot_enabled = False
        self._last_valid_plan_time = None
        self._last_odd_state = None
        self._last_behavior_plan = None  # [NEW] for M4 fallback detection
        self._heading_controller = HeadingController()
        self._speed_controller = SpeedController()
        
        # [NEW] Read initial target heading/speed from ROS2 params
        self.declare_parameter("ownship_initial_heading_deg", 0.0)
        self.declare_parameter("ownship_initial_sog_kn", 10.0)
        self._target_heading_deg = self.get_parameter("ownship_initial_heading_deg").value
        self._target_sog_kn = self.get_parameter("ownship_initial_sog_kn").value
        self.get_logger().info(f"[autopilot] Target heading={self._target_heading_deg}°, sog={self._target_sog_kn} kn")
        
        # [NEW] Subscribe to M1 ODD state
        self._sub_m1_odd = self.create_subscription(
            ODDState, "/l3/m1/odd_state",
            self._on_odd_state, sq)
        
        # [NEW] Subscribe to M4 behavior plan (for fallback detection)
        self._sub_m4_behavior = self.create_subscription(
            BehaviorPlan, "/l3/m4/behavior_plan",
            self._on_behavior_plan, sq)
        
        # [NEW] Timer for autopilot heartbeat @ 2 Hz
        self._autopilot_timer = self.create_timer(0.5, self._autopilot_step)
        
        # ... rest of __init__ (lines 203–206) ...

    def _on_odd_state(self, msg: ODDState) -> None:
        """Record latest M1 ODD state for autopilot activation check."""
        self._last_odd_state = msg

    def _on_behavior_plan(self, msg: BehaviorPlan) -> None:
        """Record latest M4 behavior plan for fallback detection."""
        self._last_behavior_plan = msg

    def _autopilot_step(self) -> None:
        """Runs @ 2 Hz. Checks if autopilot should be active."""
        if self._last_odd_state is None:
            return
        
        now = time.monotonic()
        staleness_s = (now - self._last_valid_plan_time) if self._last_valid_plan_time else float('inf')
        
        is_in_envelope = (self._last_odd_state.envelope_state == ODDState.ENVELOPE_IN)
    is_m5_stale = staleness_s > 10.0
    m4_in_fallback = (self._last_behavior_plan and 
                      "fallback" in self._last_behavior_plan.rationale.lower())
    
    was_enabled = self._autopilot_enabled
    self._autopilot_enabled = is_in_envelope and (is_m5_stale or m4_in_fallback)
        
        if was_enabled != self._autopilot_enabled:
            status = "ENABLED" if self._autopilot_enabled else "DISABLED"
            self.get_logger().info(f"[autopilot] {status} (in_envelope={is_in_envelope}, stale={staleness_s:.1f}s, m4_fallback={m4_in_fallback})")
```

**Verification**:
```bash
docker compose logs sil-bridge | grep "\[autopilot\]"
# Expected output: "[autopilot] ENABLED / DISABLED" entries
```

### Step 4: Modify _on_avoidance_plan to Use Autopilot
**File**: `/docker/sil_topic_bridge.py`, lines 309–337  
**Action**: Check `_autopilot_enabled` and call `_compute_transit_autopilot()` if needed.

```python
def _on_avoidance_plan(self, msg: AvoidancePlan) -> None:
    self._record_pulse(M5)
    out = SilOwnShipState()
    out.stamp = msg.stamp
    
    # [NEW] Check if M5 plan is valid
    has_valid_plan = (
        len(msg.waypoints) > 0 and
        abs(msg.waypoints[0].turn_radius_m) > 1e-6
    )
    
    # [NEW] If autopilot active and no valid plan, use autopilot output
    if self._autopilot_enabled and not has_valid_plan:
        out = self._compute_transit_autopilot(msg.stamp)
        self.get_logger().debug("[autopilot] Active: issuing hold command")
    else:
        # [ORIGINAL] M5 passthrough
        if msg.waypoints:
            wp = msg.waypoints[0]
            if abs(wp.turn_radius_m) > 1e-6:
                radius = max(abs(wp.turn_radius_m), 50.0)
                rudder_rad = math.atan2(SHIP_LENGTH_M, radius)
                out.rudder_angle = max(-MAX_RUDDER_RAD, min(MAX_RUDDER_RAD, rudder_rad))
            else:
                out.rudder_angle = 0.0
            out.throttle = max(0.0, min(1.0, wp.target_speed_kn / MAX_SPEED_KN))
        else:
            out.rudder_angle = 0.0
            out.throttle = 0.0
        
        # [NEW] Record timestamp if valid plan received
        if has_valid_plan:
            self._last_valid_plan_time = time.monotonic()
    
    out.lat = 0.0
    out.lon = 0.0
    out.heading = 0.0
    out.sog = 0.0
    out.cog = 0.0
    out.rot = 0.0
    out.u = 0.0
    out.v = 0.0
    out.r = 0.0
    self._pub_act.publish(out)
```

### Step 5: Add _compute_transit_autopilot Method
**File**: `/docker/sil_topic_bridge.py`, after `_on_ui_state` (after line 349).

```python
def _compute_transit_autopilot(self, stamp) -> SilOwnShipState:
    """Compute rudder + throttle for heading/speed hold using current ownship state."""
    out = SilOwnShipState()
    out.stamp = stamp
    
    # Get current heading/speed from most recent ownship state message
    current_heading_deg = self._last_ownship_state.heading_deg if hasattr(self, '_last_ownship_state') else self._target_heading_deg
    current_sog_kn = self._last_ownship_state.sog_kn if hasattr(self, '_last_ownship_state') else self._target_sog_kn
    
    # Compute errors
    heading_error_deg = self._target_heading_deg - current_heading_deg
    speed_error_kn = self._target_sog_kn - current_sog_kn
    
    # Run controllers @ dt=0.5 s (2 Hz)
    dt = 0.5
    out.rudder_angle = self._heading_controller.step(heading_error_deg, dt)
    out.throttle = self._speed_controller.step(speed_error_kn, dt)
    
    # Zero unused fields
    out.lat = 0.0
    out.lon = 0.0
    out.heading = 0.0
    out.sog = 0.0
    out.cog = 0.0
    out.rot = 0.0
    out.u = 0.0
    out.v = 0.0
    out.r = 0.0
    
    return out

def _on_own_ship_state(self, msg: FilteredOwnShipState) -> None:
    """[EXTEND] Store latest ownship state for autopilot reference."""
    self._record_pulse(M2)
    self._last_ownship_state = msg  # [NEW] Cache for autopilot
    
    # ... existing FilteredOwnShipState bridge logic ...
```

**Verification**:
```bash
# In autopilot mode, should see persistent throttle + rudder commands
docker exec mass-l3-tacticallayer-sil-nodes-1 bash -c '
  source /opt/ros/humble/setup.bash
  source /opt/ws/install/setup.bash
  ros2 topic echo /sil/actuator_cmd --once
'
# Expected: non-zero rudder_angle + throttle (not zero command)
```

---

# 7. Verification Plan

## 7.1 Unit Tests (pytest)

**File**: `src/sil_orchestrator/tests/test_transit_autopilot.py` (new)

```python
import unittest
import numpy as np
from docker.sil_topic_bridge import HeadingController, SpeedController

class TestHeadingController(unittest.TestCase):
    def test_zero_error(self):
        """Zero error → zero rudder."""
        hc = HeadingController(Kp=10.0)
        cmd = hc.step(0.0, 0.5)
        self.assertAlmostEqual(cmd, 0.0, places=5)
    
    def test_positive_error(self):
        """Positive heading error → positive rudder (starboard)."""
        hc = HeadingController(Kp=10.0)
        cmd = hc.step(5.0, 0.5)  # 5° error
        self.assertGreater(cmd, 0.0)
    
    def test_rate_limit(self):
        """Rate limit prevents abrupt changes."""
        hc = HeadingController(Kp=100.0, max_rate_deg_s=5.0)  # high gain, low rate
        cmd1 = hc.step(10.0, 0.5)  # Large error
        cmd2 = hc.step(10.0, 0.5)  # Same error
        delta_deg = abs(np.degrees(cmd2) - np.degrees(cmd1))
        self.assertLessEqual(delta_deg, 2.5 + 0.1)  # 2.5° max per 0.5s step

class TestSpeedController(unittest.TestCase):
    def test_zero_error(self):
        """Zero error → zero throttle."""
        sc = SpeedController(Kp=0.15, Ki=0.02)
        cmd = sc.step(0.0, 0.5)
        self.assertAlmostEqual(cmd, 0.0, places=5)
    
    def test_negative_error_increases_throttle(self):
        """Negative speed error (ship slow) → increase throttle."""
        sc = SpeedController(Kp=0.15, Ki=0.02)
        cmd = sc.step(-1.0, 0.5)  # 1 kn slow
        self.assertGreater(cmd, 0.0)
    
    def test_integral_windup_prevention(self):
        """Integral accumulator clipped to avoid saturation."""
        sc = SpeedController(Kp=0.15, Ki=0.02)
        for _ in range(100):
            sc.step(-1.0, 0.5)  # Continuous negative error
        self.assertLessEqual(abs(sc.integral), 5.0)

if __name__ == '__main__':
    unittest.main()
```

**Run**:
```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer
python -m pytest src/sil_orchestrator/tests/test_transit_autopilot.py -v
# Expected: 6 tests pass
```

## 7.2 SIL Acceptance (ros2 topic commands)

**Scenario**: imazu-01-ho, TRANSIT mode, no M4/M5 real plan yet.

**Setup**:
```bash
# Terminal 1: Start SIL stack
cd /Users/marine/Code/MASS-L3-Tactical\ Layer
docker compose up -d
sleep 10

# Terminal 2: Activate scenario
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/cleanup
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/configure \
  -H "Content-Type: application/json" -d '{"scenario_id":"imazu-01-ho"}'
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/activate
sleep 5
```

**Test 1: Autopilot heartbeat (topic Hz)**
```bash
# First verify ODDState envelope_state field exists and has expected values
docker exec mass-l3-tacticallayer-sil-nodes-1 bash -c '
  source /opt/ros/humble/setup.bash
  source /opt/ws/install/setup.bash
  timeout 4 ros2 topic echo --once /l3/m1/odd_state 2>&1 | grep envelope_state
'
# Expected: envelope_state: 0 (ENVELOPE_IN) in TRANSIT mode

docker exec mass-l3-tacticallayer-sil-nodes-1 bash -c '
  source /opt/ros/humble/setup.bash
  source /opt/ws/install/setup.bash
  timeout 10 ros2 topic hz /sil/actuator_cmd --window 20 2>&1 | grep "average rate"
'
# Expected: "average rate: 1.0" (at least 1 Hz, ideally 2 Hz from autopilot timer)
```

**Test 2: Actuator command non-zero (heading + speed control)**
```bash
docker exec mass-l3-tacticallayer-sil-nodes-1 bash -c '
  source /opt/ros/humble/setup.bash
  source /opt/ws/install/setup.bash
  sleep 20  # Let autopilot stabilize
  timeout 4 ros2 topic echo /sil/actuator_cmd --once
'
# Expected sample output:
# lat: 0.0
# lon: 0.0
# heading: 0.0
# sog: 0.0
# cog: 0.0
# rudder_angle: 0.087 rad  (≈ 5° starboard if heading drifts)
# throttle: 0.4             (≈ 10 kn / 25 kn if speed holds)
# rot: 0.0
# u: 0.0
# v: 0.0
# r: 0.0
```

**Test 3: Own ship state convergence**
```bash
docker exec mass-l3-tacticallayer-sil-nodes-1 bash -c '
  source /opt/ros/humble/setup.bash
  source /opt/ws/install/setup.bash
  for i in {1..6}; do
    sleep 10
    ros2 topic echo /sil/own_ship_state --once | grep -E "heading:|sog:"
  done
'
# Expected progression (every 10s sample):
# heading: 0.0 rad (2π wrap, equivalent 0°)
# sog: 5.1 m/s (≈ 10 kn)
# → (after autopilot engages)
# heading: 0.01 rad (drift ≤1° = 0.017 rad ✓)
# sog: 5.0 m/s (drift ≤0.5 kn = 0.26 m/s ✓)
```

## 7.3 Acceptance Criteria (Quantitative)

| Criterion | Target | How to Verify | Pass/Fail |
|---|---|---|---|
| Heading drift over 60s | ≤1° | Collect 6 samples @ 10s intervals, compute max deviation from 0° | **Pass** if all ≤0.017 rad |
| SOG drift over 60s | ≤0.5 kn | Collect 6 samples @ 10s intervals, compute max deviation from 10 kn target | **Pass** if all ≤0.26 m/s from 5.14 m/s |
| Actuator command heartbeat | ≥1 Hz | `ros2 topic hz /sil/actuator_cmd` | **Pass** if average_rate ≥ 1.0 |
| M5 override (handover) | Smooth | Publish synthetic avoidance_plan with turn_radius=100m, verify bridge switches from autopilot to M5 passthrough in next callback | **Pass** if rudder angle smoothly transitions |
| Autopilot deactivation on AVOID | Graceful | Trigger M1 ODD state → AVOID, verify autopilot disables, bridge returns to M5 listen mode | **Pass** if autopilot_enabled = False, `get_logger()` shows "DISABLED" |

---

# 8. Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| **Heading oscillation** (PID tuning instability) | Medium | Start with conservative Kp=5, test in SIL, iterate. If oscillations exceed ±2°, reduce Kp further. Unit tests validate rate-limit prevents abrupt changes. |
| **Speed integral windup** (accumulator saturates under load) | Low | Anti-windup clamp ±5.0 set empirically. If speed lags >1 kn after 30s, may indicate clamp too tight; adjust Ki or clamp bound. |
| **Thread safety** (M1 state / autopilot flag accessed from multiple callbacks) | Low | ROS2 callbacks are serialized by executor (single-threaded by default in bridge). `_last_odd_state` and `_autopilot_enabled` accessed only in callbacks + timer (no race). |
| **Stale ownship state** (autopilot computes errors from old data) | Low | Ownship is published at 50 Hz, bridge caches it; 0.5s timer tick ensures fresh ≤20ms old sample. |
| **Bridge publish rate drops** if autopilot step takes >20ms | Very Low | Autopilot step is O(1): two simple PID iterations, one publish. Expected <1ms on modern CPU. Monitor with `ros2_latency_tracer` if concerned. |
| **M1 ODD state latency** (autopilot activation delayed if M1 slow) | Low | M1 publishes @ 1 Hz (CLAUDE.md §3), autopilot timer @ 2 Hz → decision made within 1s of ODD change. Acceptable for TRANSIT mode. |
| **Dependency on R4 L2 mock** | **High** (blocks real closure) | R2 intentionally does NOT depend on R4; autopilot uses YAML nominal values. Once R4 mocks L2, M4 may produce real avoidance plans, triggering handover. R2 regression-free. |

---

# 9. Out of Scope

- **L2 route planning** (R4): Autopilot does not read `/l2/planned_route`.
- **M4 IvP fix** (R3): Autopilot activates precisely because M4 is broken; once M4 fixed, autopilot preempted by real M5 plans.
- **FSM visualization** (R5): No new `/l3/fsm_state` topic. Autopilot state visible only via bridge `get_logger()` (suitable for CI verification, not HMI).
- **Yaw-rate control** (L4): Autopilot outputs heading demand (rudder angle), not ROT demand. L4 is downstream.
- **Multi-ship scenarios**: R2 only handles ownship TRANSIT mode. Target vessel behavior unchanged (AIS replay).
- **COLREGs dynamic constraints** (M6): Autopilot ignores M6 output. If M6 forbids a heading, M4 should reject it (R3 scope). Autopilot blind spot [TBD-R3-integration].

---

# 10. Open Questions

- **[TBD-R3-M6-integration]**: If M6 publishes a COLREGs constraint (e.g., "no starboard turn"), and autopilot naively turns starboard to hold heading, does this violate M7 Checker logic? 
  - **Path to resolve**: R3 must define M6→M5/M4 constraint feedback mechanism before R2 hardens. If M7 vetos autopilot output, graceful fallback is automatic (ship holds last valid command until M7 clears).
  - **Current status**: Acceptable for DEMO-1 (simple head-on scenario, no ambiguous COLREGs).

- **[TBD-validation]**: PID gains (Kp_heading=10, Kp_speed=0.15, Ki_speed=0.02) chosen from first-principles (P-only, PI structure). Actual tuning requires closed-loop validation on 50Hz dynamics.
  - **Path to resolve**: Run SIL §7.2 tests post-implementation. If oscillations or sluggishness observed, iterate gains empirically.
  - **Current status**: Conservative estimates; can afford adjustment post-landing.

---

# 11. References

- **R1 Evidence Report**: `docs/Design/Review/2026-05-27/R1-evidence-report.md` (F-R1-02, F-R1-05)
- **Ship Dynamics Node**: `src/sim_workbench/sil_nodes/ship_dynamics/ship_dynamics/node.py` (actuator callback, 50Hz loop)
- **SIL Topic Bridge**: `docker/sil_topic_bridge.py` (current `_on_avoidance_plan` implementation)
- **Lifecycle Bridge**: `src/sil_orchestrator/lifecycle_bridge.py` (parameter injection)
- **Scenario YAML**: `scenarios/IMAZU标准测试/imazu-01-ho.yaml` (initial heading/sog)
- **Architecture v1.1.3-pre-stub**: `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` (§3 M5 role, §4 ODD architecture)
- **CLAUDE.md**: Project documentation (§3 Doer-Checker dual-track, §4 ODD as organization principle, §15 PM2 + Docker)

---

**End of Plan**. Ready for main-agent audit.
