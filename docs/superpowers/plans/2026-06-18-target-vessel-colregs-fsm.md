# Target Vessel COLREGs FSM Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add opt-in COLREGs Rule 14/15/16/17 behavior for route-driven simulated target ships without changing passive replay baselines, AIS truth, or clean8 semantics.

**Architecture:** Implement the behavior inside `target_vessel_node` using small pure Python helpers for configuration normalization, geometry, and FSM decisions. The node subscribes only to `/sil/own_ship_state`, publishes the existing `/sil/target_vessel_state`, and never reads TDL internal decision topics.

**Tech Stack:** Python ROS2 lifecycle node, `sil_msgs`, existing YAML scenario injection through `lifecycle_bridge.py`, pytest with existing MagicMock ROS stubs.

## Global Constraints

- Work only in `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/target-vessel-colregs-fsm` on branch `codex/target-vessel-colregs-fsm`.
- Do not modify `main`, do not use the main runtime stack, and do not add new intelligent scenarios to clean8 default gates.
- `colregs_rule_fsm` is allowed only for `source.type=route`.
- AIS replay and AIS live targets must remain passive/observed and must not be controlled by FSM.
- Target ships may observe `/sil/own_ship_state` only; they must not read `/l3/m5/avoidance_plan`, `/l3/m4/behavior_plan`, `/l3/m6/colregs_constraint`, or other TDL internal decisions.
- v1 supports at most one `colregs_rule_fsm` target per configured scenario.
- v1 uses the current point-mass target model plus `rot_limit_deg_s`; do not introduce MMG.
- Keep legacy `model` YAML compatible; do not batch-migrate existing scenario files.

---

## File Structure

- Create `src/sim_workbench/sil_nodes/target_vessel/target_vessel/config.py`: normalize legacy `model` plus new `source/behavior` fields into typed target configuration.
- Create `src/sim_workbench/sil_nodes/target_vessel/target_vessel/geometry.py`: pure heading, ENU, CPA/TCPA, relative-bearing, and turn helpers.
- Create `src/sim_workbench/sil_nodes/target_vessel/target_vessel/colregs_behavior.py`: pure FSM state and `TargetAction` production.
- Modify `src/sim_workbench/sil_nodes/target_vessel/target_vessel/node.py`: parse normalized config, subscribe to `/sil/own_ship_state`, call FSM when available, and publish existing target state.
- Modify `src/sil_orchestrator/encounters_routes.py`: allow optional `mode` in `/api/v1/encounters/inject` and pass it through to `AddTarget`.
- Modify `scenarios/fcb_traffic_situation.schema.json`: add optional `source` and `behavior` fields for `targetShips[]`.
- Create unit tests under `src/sim_workbench/sil_nodes/target_vessel/test/`.
- Modify existing orchestrator tests under `tests/sil_orchestrator/` and `src/sil_orchestrator/tests/`.
- Create three new scenarios under `scenarios/COLREGs测试/`; do not edit clean8 runner lists to include them.

### Task 1: Target Config Normalization

**Files:**
- Create: `src/sim_workbench/sil_nodes/target_vessel/target_vessel/config.py`
- Test: `src/sim_workbench/sil_nodes/target_vessel/test/test_target_vessel_config.py`

**Interfaces:**
- Produces:
  - `TargetSourceConfig(type: str)`
  - `TargetBehaviorConfig(...)`
  - `NormalizedTargetConfig(...)`
  - `normalize_target_config(entry: dict) -> NormalizedTargetConfig`
  - `count_colregs_rule_targets(configs: list[NormalizedTargetConfig]) -> int`
- Consumes: no earlier task output.

- [ ] **Step 1: Write config tests**

Create `src/sim_workbench/sil_nodes/target_vessel/test/test_target_vessel_config.py`:

```python
from __future__ import annotations

import pytest

from target_vessel.config import (
    ReservedPolicyError,
    UnsupportedSourcePolicyError,
    count_colregs_rule_targets,
    normalize_target_config,
)


def _base_entry(**extra):
    entry = {
        "static": {"mmsi": 100000001},
        "initial": {
            "position": {"latitude": 63.4, "longitude": 10.4},
            "heading": 180.0,
            "cog": 180.0,
            "sog": 10.0,
        },
        "model": "ais_replay_vessel",
    }
    entry.update(extra)
    return entry


def test_legacy_ais_replay_maps_to_route_passive():
    cfg = normalize_target_config(_base_entry(model="ais_replay_vessel"))
    assert cfg.source.type == "route"
    assert cfg.behavior.policy == "passive"
    assert cfg.legacy_model == "ais_replay_vessel"


def test_legacy_ncdm_maps_to_route_ncdm():
    cfg = normalize_target_config(_base_entry(model="ncdm_vessel"))
    assert cfg.source.type == "route"
    assert cfg.behavior.policy == "ncdm"


def test_legacy_intelligent_maps_to_colregs_rule_fsm():
    cfg = normalize_target_config(_base_entry(model="intelligent_vessel"))
    assert cfg.source.type == "route"
    assert cfg.behavior.policy == "colregs_rule_fsm"


def test_new_fields_override_legacy_model():
    cfg = normalize_target_config(
        _base_entry(
            model="ais_replay_vessel",
            source={"type": "route"},
            behavior={"policy": "colregs_rule_fsm", "min_turn_deg": 35.0},
        )
    )
    assert cfg.behavior.policy == "colregs_rule_fsm"
    assert cfg.behavior.min_turn_deg == 35.0
    assert cfg.legacy_model == "ais_replay_vessel"
    assert cfg.used_new_fields is True


def test_colregs_rule_fsm_rejects_ais_replay_source():
    with pytest.raises(UnsupportedSourcePolicyError):
        normalize_target_config(
            _base_entry(
                source={"type": "ais_replay"},
                behavior={"policy": "colregs_rule_fsm"},
            )
        )


def test_reserved_policies_fail_fast():
    for policy in ("intelligent_planner", "tdl_agent"):
        with pytest.raises(ReservedPolicyError):
            normalize_target_config(_base_entry(behavior={"policy": policy}))


def test_illegal_numeric_parameter_fails_fast():
    with pytest.raises(ValueError, match="rot_limit_deg_s"):
        normalize_target_config(
            _base_entry(
                source={"type": "route"},
                behavior={"policy": "colregs_rule_fsm", "rot_limit_deg_s": 0.0},
            )
        )


def test_count_colregs_rule_targets():
    configs = [
        normalize_target_config(_base_entry(model="ais_replay_vessel")),
        normalize_target_config(
            _base_entry(
                source={"type": "route"},
                behavior={"policy": "colregs_rule_fsm"},
            )
        ),
    ]
    assert count_colregs_rule_targets(configs) == 1
```

- [ ] **Step 2: Run config tests and verify failure**

Run:

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/.worktrees/target-vessel-colregs-fsm
PYTHONPATH=src/sim_workbench/sil_nodes/target_vessel pytest -q src/sim_workbench/sil_nodes/target_vessel/test/test_target_vessel_config.py
```

Expected: FAIL with `ModuleNotFoundError: No module named 'target_vessel.config'`.

- [ ] **Step 3: Implement config module**

Create `src/sim_workbench/sil_nodes/target_vessel/target_vessel/config.py`:

```python
from __future__ import annotations

from dataclasses import dataclass
from typing import Any


class UnsupportedSourcePolicyError(ValueError):
    """Raised when a behavior policy is incompatible with a source type."""


class ReservedPolicyError(ValueError):
    """Raised when a future policy is selected before v1 supports it."""


@dataclass(frozen=True)
class TargetSourceConfig:
    type: str = "route"


@dataclass(frozen=True)
class TargetBehaviorConfig:
    policy: str = "passive"
    reaction_delay_s: float = 6.0
    min_turn_deg: float = 30.0
    rot_limit_deg_s: float = 3.0
    role_lock_s: float = 20.0
    target_cpa_m: float = 900.0
    standon_hold_tcpa_s: float = 180.0
    standon_action_tcpa_s: float = 90.0
    emergency_tcpa_s: float = 45.0
    observed_action_heading_delta_deg: float = 8.0
    observed_action_dcpa_gain_m: float = 150.0
    clear_dwell_s: float = 10.0
    return_cooldown_s: float = 20.0


@dataclass(frozen=True)
class NormalizedTargetConfig:
    source: TargetSourceConfig
    behavior: TargetBehaviorConfig
    legacy_model: str | None
    used_new_fields: bool


_LEGACY_MODEL_MAP = {
    "ais_replay_vessel": ("route", "passive"),
    "replay": ("route", "passive"),
    "ncdm_vessel": ("route", "ncdm"),
    "ncdm": ("route", "ncdm"),
    "intelligent_vessel": ("route", "colregs_rule_fsm"),
    "intelligent": ("route", "colregs_rule_fsm"),
}

_SOURCE_TYPES = {"route", "injected_geometry", "ais_replay", "ais_live"}
_POLICIES = {"passive", "ncdm", "colregs_rule_fsm", "intelligent_planner", "tdl_agent"}


def _as_float(raw: dict[str, Any], key: str, default: float, *, positive: bool = False) -> float:
    value = float(raw.get(key, default))
    if positive and value <= 0.0:
        raise ValueError(f"{key} must be > 0")
    if not positive and value < 0.0:
        raise ValueError(f"{key} must be >= 0")
    return value


def normalize_target_config(entry: dict[str, Any]) -> NormalizedTargetConfig:
    legacy_model = entry.get("model")
    legacy_key = str(legacy_model or "ais_replay_vessel")
    source_type, policy = _LEGACY_MODEL_MAP.get(legacy_key, ("route", "passive"))

    source_raw = entry.get("source")
    behavior_raw = entry.get("behavior")
    used_new_fields = isinstance(source_raw, dict) or isinstance(behavior_raw, dict)

    if isinstance(source_raw, dict):
        source_type = str(source_raw.get("type", source_type))
    if isinstance(behavior_raw, dict):
        policy = str(behavior_raw.get("policy", policy))
    else:
        behavior_raw = {}

    if source_type not in _SOURCE_TYPES:
        raise ValueError(f"unsupported source.type: {source_type}")
    if policy not in _POLICIES:
        raise ValueError(f"unsupported behavior.policy: {policy}")
    if policy in {"intelligent_planner", "tdl_agent"}:
        raise ReservedPolicyError(f"behavior.policy={policy} is reserved for a later phase")
    if policy == "colregs_rule_fsm" and source_type != "route":
        raise UnsupportedSourcePolicyError(
            f"colregs_rule_fsm requires source.type=route, got {source_type}"
        )

    behavior = TargetBehaviorConfig(
        policy=policy,
        reaction_delay_s=_as_float(behavior_raw, "reaction_delay_s", 6.0),
        min_turn_deg=_as_float(behavior_raw, "min_turn_deg", 30.0, positive=True),
        rot_limit_deg_s=_as_float(behavior_raw, "rot_limit_deg_s", 3.0, positive=True),
        role_lock_s=_as_float(behavior_raw, "role_lock_s", 20.0),
        target_cpa_m=_as_float(behavior_raw, "target_cpa_m", 900.0, positive=True),
        standon_hold_tcpa_s=_as_float(behavior_raw, "standon_hold_tcpa_s", 180.0, positive=True),
        standon_action_tcpa_s=_as_float(behavior_raw, "standon_action_tcpa_s", 90.0, positive=True),
        emergency_tcpa_s=_as_float(behavior_raw, "emergency_tcpa_s", 45.0, positive=True),
        observed_action_heading_delta_deg=_as_float(
            behavior_raw, "observed_action_heading_delta_deg", 8.0, positive=True
        ),
        observed_action_dcpa_gain_m=_as_float(
            behavior_raw, "observed_action_dcpa_gain_m", 150.0
        ),
        clear_dwell_s=_as_float(behavior_raw, "clear_dwell_s", 10.0),
        return_cooldown_s=_as_float(behavior_raw, "return_cooldown_s", 20.0),
    )
    if behavior.emergency_tcpa_s > behavior.standon_action_tcpa_s:
        raise ValueError("emergency_tcpa_s must be <= standon_action_tcpa_s")
    if behavior.standon_action_tcpa_s > behavior.standon_hold_tcpa_s:
        raise ValueError("standon_action_tcpa_s must be <= standon_hold_tcpa_s")

    return NormalizedTargetConfig(
        source=TargetSourceConfig(type=source_type),
        behavior=behavior,
        legacy_model=legacy_model,
        used_new_fields=used_new_fields,
    )


def count_colregs_rule_targets(configs: list[NormalizedTargetConfig]) -> int:
    return sum(1 for cfg in configs if cfg.behavior.policy == "colregs_rule_fsm")
```

- [ ] **Step 4: Run config tests and verify pass**

Run:

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/.worktrees/target-vessel-colregs-fsm
PYTHONPATH=src/sim_workbench/sil_nodes/target_vessel pytest -q src/sim_workbench/sil_nodes/target_vessel/test/test_target_vessel_config.py
```

Expected: `8 passed`.

- [ ] **Step 5: Commit Task 1**

```bash
git add src/sim_workbench/sil_nodes/target_vessel/target_vessel/config.py \
        src/sim_workbench/sil_nodes/target_vessel/test/test_target_vessel_config.py
git commit -m "feat(target-vessel): normalize source behavior config"
```

### Task 2: Geometry Helpers

**Files:**
- Create: `src/sim_workbench/sil_nodes/target_vessel/target_vessel/geometry.py`
- Test: `src/sim_workbench/sil_nodes/target_vessel/test/test_colregs_geometry.py`

**Interfaces:**
- Produces:
  - `VesselKinematics`
  - `CpaTcpa`
  - `wrap_deg(angle: float) -> float`
  - `signed_delta_deg(target_deg: float, reference_deg: float) -> float`
  - `relative_bearing_deg(own: VesselKinematics, target: VesselKinematics) -> float`
  - `aspect_from_target_deg(target: VesselKinematics, own: VesselKinematics) -> float`
  - `compute_cpa_tcpa(own: VesselKinematics, target: VesselKinematics) -> CpaTcpa`
  - `apply_rot_limit(current_heading_deg: float, desired_heading_deg: float, rot_limit_deg_s: float, dt_s: float) -> tuple[float, float]`
- Consumes: no earlier task output.

- [ ] **Step 1: Write geometry tests**

Create `src/sim_workbench/sil_nodes/target_vessel/test/test_colregs_geometry.py`:

```python
from __future__ import annotations

from target_vessel.geometry import (
    VesselKinematics,
    apply_rot_limit,
    compute_cpa_tcpa,
    relative_bearing_deg,
    signed_delta_deg,
    wrap_deg,
)


def test_wrap_and_signed_delta():
    assert wrap_deg(370.0) == 10.0
    assert wrap_deg(-10.0) == 350.0
    assert signed_delta_deg(10.0, 350.0) == 20.0
    assert signed_delta_deg(350.0, 10.0) == -20.0


def test_relative_bearing_starboard_and_port():
    own = VesselKinematics(lat=63.0, lon=10.0, heading_deg=0.0, sog_mps=5.0)
    starboard = VesselKinematics(lat=63.0, lon=10.01, heading_deg=270.0, sog_mps=5.0)
    port = VesselKinematics(lat=63.0, lon=9.99, heading_deg=90.0, sog_mps=5.0)
    assert 80.0 < relative_bearing_deg(own, starboard) < 100.0
    assert -100.0 < relative_bearing_deg(own, port) < -80.0


def test_cpa_tcpa_for_head_on_collision_course():
    own = VesselKinematics(lat=63.0, lon=10.0, heading_deg=0.0, sog_mps=5.0)
    target = VesselKinematics(lat=63.01, lon=10.0, heading_deg=180.0, sog_mps=5.0)
    result = compute_cpa_tcpa(own, target)
    assert result.tcpa_s > 0.0
    assert result.dcpa_m < 5.0


def test_apply_rot_limit_turns_shortest_way():
    heading, rot = apply_rot_limit(
        current_heading_deg=0.0,
        desired_heading_deg=30.0,
        rot_limit_deg_s=3.0,
        dt_s=2.0,
    )
    assert heading == 6.0
    assert rot == 3.0


def test_apply_rot_limit_handles_wraparound():
    heading, rot = apply_rot_limit(
        current_heading_deg=350.0,
        desired_heading_deg=10.0,
        rot_limit_deg_s=5.0,
        dt_s=1.0,
    )
    assert heading == 355.0
    assert rot == 5.0
```

- [ ] **Step 2: Run geometry tests and verify failure**

Run:

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/.worktrees/target-vessel-colregs-fsm
PYTHONPATH=src/sim_workbench/sil_nodes/target_vessel pytest -q src/sim_workbench/sil_nodes/target_vessel/test/test_colregs_geometry.py
```

Expected: FAIL with `ModuleNotFoundError: No module named 'target_vessel.geometry'`.

- [ ] **Step 3: Implement geometry module**

Create `src/sim_workbench/sil_nodes/target_vessel/target_vessel/geometry.py`:

```python
from __future__ import annotations

from dataclasses import dataclass
import math


M_PER_DEG_LAT = 111120.0


@dataclass(frozen=True)
class VesselKinematics:
    lat: float
    lon: float
    heading_deg: float
    sog_mps: float


@dataclass(frozen=True)
class CpaTcpa:
    dcpa_m: float
    tcpa_s: float
    range_m: float


def wrap_deg(angle: float) -> float:
    return angle % 360.0


def signed_delta_deg(target_deg: float, reference_deg: float) -> float:
    return (target_deg - reference_deg + 540.0) % 360.0 - 180.0


def _enu_delta_m(origin_lat: float, origin_lon: float, lat: float, lon: float) -> tuple[float, float]:
    north = (lat - origin_lat) * M_PER_DEG_LAT
    east = (lon - origin_lon) * M_PER_DEG_LAT * math.cos(math.radians(origin_lat))
    return east, north


def _velocity_enu(v: VesselKinematics) -> tuple[float, float]:
    heading = math.radians(v.heading_deg)
    return v.sog_mps * math.sin(heading), v.sog_mps * math.cos(heading)


def _bearing_from_enu(east_m: float, north_m: float) -> float:
    return wrap_deg(math.degrees(math.atan2(east_m, north_m)))


def relative_bearing_deg(own: VesselKinematics, target: VesselKinematics) -> float:
    east, north = _enu_delta_m(own.lat, own.lon, target.lat, target.lon)
    bearing = _bearing_from_enu(east, north)
    return signed_delta_deg(bearing, own.heading_deg)


def aspect_from_target_deg(target: VesselKinematics, own: VesselKinematics) -> float:
    east, north = _enu_delta_m(target.lat, target.lon, own.lat, own.lon)
    bearing = _bearing_from_enu(east, north)
    return signed_delta_deg(bearing, target.heading_deg)


def compute_cpa_tcpa(own: VesselKinematics, target: VesselKinematics) -> CpaTcpa:
    rel_e, rel_n = _enu_delta_m(own.lat, own.lon, target.lat, target.lon)
    own_ve, own_vn = _velocity_enu(own)
    tgt_ve, tgt_vn = _velocity_enu(target)
    rel_ve = tgt_ve - own_ve
    rel_vn = tgt_vn - own_vn
    rel_speed_sq = rel_ve * rel_ve + rel_vn * rel_vn
    range_m = math.hypot(rel_e, rel_n)
    if rel_speed_sq <= 1e-9:
        return CpaTcpa(dcpa_m=range_m, tcpa_s=0.0, range_m=range_m)
    tcpa = -((rel_e * rel_ve) + (rel_n * rel_vn)) / rel_speed_sq
    cpa_e = rel_e + rel_ve * tcpa
    cpa_n = rel_n + rel_vn * tcpa
    return CpaTcpa(dcpa_m=math.hypot(cpa_e, cpa_n), tcpa_s=tcpa, range_m=range_m)


def apply_rot_limit(
    current_heading_deg: float,
    desired_heading_deg: float,
    rot_limit_deg_s: float,
    dt_s: float,
) -> tuple[float, float]:
    max_step = abs(rot_limit_deg_s) * max(dt_s, 0.0)
    delta = signed_delta_deg(desired_heading_deg, current_heading_deg)
    if abs(delta) <= max_step:
        step = delta
    else:
        step = math.copysign(max_step, delta)
    new_heading = wrap_deg(current_heading_deg + step)
    rot_deg_s = 0.0 if dt_s <= 0.0 else step / dt_s
    return new_heading, rot_deg_s
```

- [ ] **Step 4: Run geometry tests and verify pass**

Run:

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/.worktrees/target-vessel-colregs-fsm
PYTHONPATH=src/sim_workbench/sil_nodes/target_vessel pytest -q src/sim_workbench/sil_nodes/target_vessel/test/test_colregs_geometry.py
```

Expected: `5 passed`.

- [ ] **Step 5: Commit Task 2**

```bash
git add src/sim_workbench/sil_nodes/target_vessel/target_vessel/geometry.py \
        src/sim_workbench/sil_nodes/target_vessel/test/test_colregs_geometry.py
git commit -m "feat(target-vessel): add colregs geometry helpers"
```

### Task 3: COLREGs Rule FSM Core

**Files:**
- Create: `src/sim_workbench/sil_nodes/target_vessel/target_vessel/colregs_behavior.py`
- Test: `src/sim_workbench/sil_nodes/target_vessel/test/test_colregs_rule_fsm.py`

**Interfaces:**
- Consumes:
  - `TargetBehaviorConfig` from `target_vessel.config`
  - `VesselKinematics`, `CpaTcpa`, `compute_cpa_tcpa`, `relative_bearing_deg`, `aspect_from_target_deg`, `signed_delta_deg`, `wrap_deg` from `target_vessel.geometry`
- Produces:
  - `TargetBehaviorState`
  - `TargetAction`
  - `ColregsRuleFsm`
  - `ColregsRuleFsm.update(now_s: float, own: VesselKinematics, target: VesselKinematics, nominal_heading_deg: float) -> TargetAction`

- [ ] **Step 1: Write FSM tests**

Create `src/sim_workbench/sil_nodes/target_vessel/test/test_colregs_rule_fsm.py`:

```python
from __future__ import annotations

from target_vessel.colregs_behavior import ColregsRuleFsm, TargetBehaviorState
from target_vessel.config import TargetBehaviorConfig
from target_vessel.geometry import VesselKinematics


def _cfg(**extra):
    kwargs = {"policy": "colregs_rule_fsm"}
    kwargs.update(extra)
    return TargetBehaviorConfig(**kwargs)


def test_rule14_head_on_turns_starboard_after_delay():
    fsm = ColregsRuleFsm(_cfg(reaction_delay_s=0.0, min_turn_deg=30.0))
    own = VesselKinematics(lat=63.0, lon=10.0, heading_deg=0.0, sog_mps=5.0)
    target = VesselKinematics(lat=63.01, lon=10.0, heading_deg=180.0, sog_mps=5.0)
    action = fsm.update(now_s=1.0, own=own, target=target, nominal_heading_deg=180.0)
    assert fsm.state.state == "GIVE_WAY"
    assert action.rule == "Rule 14"
    assert action.reason == "head_on_starboard"
    assert action.desired_heading_deg == 210.0


def test_rule15_target_give_way_when_ownship_on_target_starboard():
    fsm = ColregsRuleFsm(_cfg(reaction_delay_s=0.0, min_turn_deg=30.0))
    own = VesselKinematics(lat=63.0, lon=10.01, heading_deg=270.0, sog_mps=5.0)
    target = VesselKinematics(lat=63.0, lon=10.0, heading_deg=0.0, sog_mps=5.0)
    action = fsm.update(now_s=1.0, own=own, target=target, nominal_heading_deg=0.0)
    assert fsm.state.state == "GIVE_WAY"
    assert action.rule == "Rule 15"
    assert action.desired_heading_deg == 30.0


def test_rule17_stand_on_holds_when_ownship_action_observed():
    fsm = ColregsRuleFsm(_cfg(reaction_delay_s=0.0, standon_action_tcpa_s=90.0))
    own0 = VesselKinematics(lat=63.0, lon=10.01, heading_deg=270.0, sog_mps=5.0)
    target = VesselKinematics(lat=63.0, lon=10.0, heading_deg=0.0, sog_mps=5.0)
    fsm.update(now_s=1.0, own=own0, target=target, nominal_heading_deg=0.0)
    own_turning = VesselKinematics(lat=63.0, lon=10.01, heading_deg=258.0, sog_mps=5.0)
    action = fsm.update(now_s=2.0, own=own_turning, target=target, nominal_heading_deg=0.0)
    assert fsm.state.state == "STAND_ON"
    assert action.reason == "stand_on_hold_observed_ownship_action"
    assert action.desired_heading_deg == 0.0


def test_rule17_independent_action_when_ownship_not_acting_and_tcpa_short():
    fsm = ColregsRuleFsm(
        _cfg(
            reaction_delay_s=0.0,
            standon_hold_tcpa_s=500.0,
            standon_action_tcpa_s=500.0,
            emergency_tcpa_s=30.0,
            min_turn_deg=30.0,
        )
    )
    own = VesselKinematics(lat=63.0, lon=10.01, heading_deg=270.0, sog_mps=5.0)
    target = VesselKinematics(lat=63.0, lon=10.0, heading_deg=0.0, sog_mps=5.0)
    action = fsm.update(now_s=1.0, own=own, target=target, nominal_heading_deg=0.0)
    assert fsm.state.state == "GIVE_WAY"
    assert action.rule == "Rule 17"
    assert action.reason == "rule17_independent_action"
    assert action.desired_heading_deg == 30.0


def test_returning_after_conflict_clears():
    fsm = ColregsRuleFsm(_cfg(reaction_delay_s=0.0, clear_dwell_s=0.0, return_cooldown_s=0.0))
    own = VesselKinematics(lat=63.0, lon=10.0, heading_deg=0.0, sog_mps=5.0)
    target = VesselKinematics(lat=63.01, lon=10.0, heading_deg=180.0, sog_mps=5.0)
    fsm.update(now_s=1.0, own=own, target=target, nominal_heading_deg=180.0)
    cleared_own = VesselKinematics(lat=62.99, lon=10.0, heading_deg=0.0, sog_mps=5.0)
    cleared_target = VesselKinematics(lat=63.02, lon=10.02, heading_deg=210.0, sog_mps=5.0)
    action = fsm.update(
        now_s=20.0,
        own=cleared_own,
        target=cleared_target,
        nominal_heading_deg=180.0,
    )
    assert fsm.state.state in {"RETURNING", "NOMINAL"}
    assert action.desired_heading_deg == 180.0


def test_state_is_dataclass_for_per_mmsi_storage():
    state = TargetBehaviorState()
    assert state.state == "NOMINAL"
    assert state.encounter_start_heading_deg is None
```

- [ ] **Step 2: Run FSM tests and verify failure**

Run:

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/.worktrees/target-vessel-colregs-fsm
PYTHONPATH=src/sim_workbench/sil_nodes/target_vessel pytest -q src/sim_workbench/sil_nodes/target_vessel/test/test_colregs_rule_fsm.py
```

Expected: FAIL with `ModuleNotFoundError: No module named 'target_vessel.colregs_behavior'`.

- [ ] **Step 3: Implement FSM module**

Create `src/sim_workbench/sil_nodes/target_vessel/target_vessel/colregs_behavior.py` with these signatures and behavior:

```python
from __future__ import annotations

from dataclasses import dataclass

from target_vessel.config import TargetBehaviorConfig
from target_vessel.geometry import (
    CpaTcpa,
    VesselKinematics,
    aspect_from_target_deg,
    compute_cpa_tcpa,
    relative_bearing_deg,
    signed_delta_deg,
    wrap_deg,
)


@dataclass
class TargetBehaviorState:
    state: str = "NOMINAL"
    rule: str | None = None
    role: str | None = None
    reason: str = "nominal"
    encounter_start_s: float | None = None
    encounter_start_own_heading_deg: float | None = None
    encounter_start_heading_deg: float | None = None
    encounter_start_dcpa_m: float | None = None
    clear_since_s: float | None = None
    last_transition_s: float = 0.0


@dataclass(frozen=True)
class TargetAction:
    desired_heading_deg: float
    desired_sog_mps: float | None
    state: str
    rule: str | None
    role: str | None
    reason: str
    emergency: bool
    dcpa_m: float
    tcpa_s: float


class ColregsRuleFsm:
    def __init__(self, cfg: TargetBehaviorConfig) -> None:
        self.cfg = cfg
        self.state = TargetBehaviorState()

    def update(
        self,
        *,
        now_s: float,
        own: VesselKinematics,
        target: VesselKinematics,
        nominal_heading_deg: float,
    ) -> TargetAction:
        cpa = compute_cpa_tcpa(own, target)
        encounter = self._classify(own, target, cpa)
        if encounter is None:
            return self._clear_or_nominal(now_s, target, nominal_heading_deg, cpa)

        rule, role = encounter
        self._ensure_encounter_started(now_s, own, target, cpa)
        if not self._reaction_delay_elapsed(now_s):
            self._set_state("MONITORING", rule, role, "reaction_delay", now_s)
            return self._action(target.heading_deg, None, False, cpa)

        if rule == "Rule 14":
            return self._give_way(now_s, target, nominal_heading_deg, cpa, rule, "head_on_starboard")
        if rule == "Rule 15" and role == "GIVE_WAY":
            return self._give_way(now_s, target, nominal_heading_deg, cpa, rule, "crossing_give_way_starboard")
        if rule == "Rule 17" and role == "STAND_ON":
            if self._ownship_action_observed(own, cpa):
                self._set_state("STAND_ON", rule, role, "stand_on_hold_observed_ownship_action", now_s)
                return self._action(nominal_heading_deg, None, False, cpa)
            if 0.0 < cpa.tcpa_s <= self.cfg.standon_action_tcpa_s:
                return self._give_way(now_s, target, nominal_heading_deg, cpa, rule, "rule17_independent_action")
            self._set_state("STAND_ON", rule, role, "stand_on_hold", now_s)
            return self._action(nominal_heading_deg, None, False, cpa)

        self._set_state("MONITORING", rule, role, "monitoring", now_s)
        return self._action(target.heading_deg, None, False, cpa)

    def _classify(
        self,
        own: VesselKinematics,
        target: VesselKinematics,
        cpa: CpaTcpa,
    ) -> tuple[str, str] | None:
        if cpa.tcpa_s <= 0.0 or cpa.dcpa_m >= self.cfg.target_cpa_m:
            return None
        own_rel = relative_bearing_deg(own, target)
        tgt_aspect = aspect_from_target_deg(target, own)
        heading_diff = abs(signed_delta_deg(target.heading_deg, own.heading_deg))
        if abs(own_rel) <= 10.0 and heading_diff >= 160.0:
            return "Rule 14", "BOTH_GIVE_WAY"
        if abs(tgt_aspect) > 112.5:
            return "Rule 13", "STAND_ON"
        target_sees_own = relative_bearing_deg(target, own)
        if target_sees_own > 0.0:
            return "Rule 15", "GIVE_WAY"
        return "Rule 17", "STAND_ON"

    def _ensure_encounter_started(
        self,
        now_s: float,
        own: VesselKinematics,
        target: VesselKinematics,
        cpa: CpaTcpa,
    ) -> None:
        if self.state.encounter_start_s is not None:
            return
        self.state.encounter_start_s = now_s
        self.state.encounter_start_own_heading_deg = own.heading_deg
        self.state.encounter_start_heading_deg = target.heading_deg
        self.state.encounter_start_dcpa_m = cpa.dcpa_m

    def _reaction_delay_elapsed(self, now_s: float) -> bool:
        if self.state.encounter_start_s is None:
            return False
        return (now_s - self.state.encounter_start_s) >= self.cfg.reaction_delay_s

    def _ownship_action_observed(self, own: VesselKinematics, cpa: CpaTcpa) -> bool:
        start_heading = self.state.encounter_start_own_heading_deg
        start_dcpa = self.state.encounter_start_dcpa_m
        heading_action = (
            start_heading is not None
            and abs(signed_delta_deg(own.heading_deg, start_heading))
            >= self.cfg.observed_action_heading_delta_deg
        )
        dcpa_action = (
            start_dcpa is not None
            and cpa.dcpa_m - start_dcpa >= self.cfg.observed_action_dcpa_gain_m
        )
        passing_safe = cpa.tcpa_s < 0.0 and cpa.dcpa_m >= self.cfg.target_cpa_m
        return heading_action or dcpa_action or passing_safe

    def _give_way(
        self,
        now_s: float,
        target: VesselKinematics,
        nominal_heading_deg: float,
        cpa: CpaTcpa,
        rule: str,
        reason: str,
    ) -> TargetAction:
        self._set_state("GIVE_WAY", rule, "GIVE_WAY", reason, now_s)
        emergency = 0.0 < cpa.tcpa_s <= self.cfg.emergency_tcpa_s
        speed = target.sog_mps * 0.7 if emergency else None
        return self._action(wrap_deg(nominal_heading_deg + self.cfg.min_turn_deg), speed, emergency, cpa)

    def _clear_or_nominal(
        self,
        now_s: float,
        target: VesselKinematics,
        nominal_heading_deg: float,
        cpa: CpaTcpa,
    ) -> TargetAction:
        if self.state.state in {"GIVE_WAY", "STAND_ON", "MONITORING"}:
            if self.state.clear_since_s is None:
                self.state.clear_since_s = now_s
            if now_s - self.state.clear_since_s >= self.cfg.clear_dwell_s:
                self._set_state("RETURNING", self.state.rule, self.state.role, "conflict_clear_returning", now_s)
                return self._action(nominal_heading_deg, None, False, cpa)
            return self._action(target.heading_deg, None, False, cpa)
        if self.state.state == "RETURNING":
            if abs(signed_delta_deg(target.heading_deg, nominal_heading_deg)) <= 1.0:
                self.state = TargetBehaviorState(state="NOMINAL", reason="nominal")
                return self._action(nominal_heading_deg, None, False, cpa)
            return self._action(nominal_heading_deg, None, False, cpa)
        return self._action(nominal_heading_deg, None, False, cpa)

    def _set_state(self, state: str, rule: str | None, role: str | None, reason: str, now_s: float) -> None:
        if self.state.state != state:
            self.state.last_transition_s = now_s
        self.state.state = state
        self.state.rule = rule
        self.state.role = role
        self.state.reason = reason

    def _action(
        self,
        desired_heading_deg: float,
        desired_sog_mps: float | None,
        emergency: bool,
        cpa: CpaTcpa,
    ) -> TargetAction:
        return TargetAction(
            desired_heading_deg=wrap_deg(desired_heading_deg),
            desired_sog_mps=desired_sog_mps,
            state=self.state.state,
            rule=self.state.rule,
            role=self.state.role,
            reason=self.state.reason,
            emergency=emergency,
            dcpa_m=cpa.dcpa_m,
            tcpa_s=cpa.tcpa_s,
        )
```

- [ ] **Step 4: Run FSM tests and adjust only if test geometry proves wrong**

Run:

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/.worktrees/target-vessel-colregs-fsm
PYTHONPATH=src/sim_workbench/sil_nodes/target_vessel pytest -q src/sim_workbench/sil_nodes/target_vessel/test/test_colregs_rule_fsm.py
```

Expected: `6 passed`.

If a test fails because the synthetic geometry does not produce the intended bearing, change the test vessel positions only. Do not broaden the rule thresholds.

- [ ] **Step 5: Commit Task 3**

```bash
git add src/sim_workbench/sil_nodes/target_vessel/target_vessel/colregs_behavior.py \
        src/sim_workbench/sil_nodes/target_vessel/test/test_colregs_rule_fsm.py
git commit -m "feat(target-vessel): add colregs rule fsm"
```

### Task 4: Wire FSM into TargetVesselNode

**Files:**
- Modify: `src/sim_workbench/sil_nodes/target_vessel/target_vessel/node.py`
- Test: `tests/sil/test_target_vessel.py`
- Test: `src/sim_workbench/sil_nodes/target_vessel/test/test_target_vessel_ou.py`
- Test: `src/sim_workbench/sil_nodes/target_vessel/test/test_target_vessel_node_colregs.py`

**Interfaces:**
- Consumes:
  - `normalize_target_config`, `count_colregs_rule_targets`
  - `VesselKinematics`, `apply_rot_limit`
  - `ColregsRuleFsm`
- Produces:
  - `TargetVessel(..., behavior_config: TargetBehaviorConfig | None = None)`
  - `TargetVessel.step(dt: float = 0.1, ownship: VesselKinematics | None = None, now_s: float | None = None) -> dict`
  - `/sil/own_ship_state` subscription in lifecycle active state.

- [ ] **Step 1: Write node FSM tests**

Create `src/sim_workbench/sil_nodes/target_vessel/test/test_target_vessel_node_colregs.py`:

```python
from __future__ import annotations

import json
import math
import sys
from pathlib import Path
from unittest.mock import MagicMock


class DummyLifecycleNode:
    def __init__(self, node_name, **kwargs):
        self._logger = MagicMock()
        self.get_clock = MagicMock()


sys.modules["rclpy"] = MagicMock()
sys.modules["rclpy.lifecycle"] = MagicMock()
sys.modules["rclpy.lifecycle"].LifecycleNode = DummyLifecycleNode
sys.modules["rclpy.qos"] = MagicMock()
sys.modules["sil_msgs"] = MagicMock()
sys.modules["sil_msgs.msg"] = MagicMock()
sys.modules["sil_msgs.srv"] = MagicMock()

sys.path.insert(0, str(Path(__file__).parents[2]))
sys.path.insert(0, str(Path(__file__).parents[3] / "sil_common"))

from target_vessel.config import TargetBehaviorConfig
from target_vessel.geometry import VesselKinematics
from target_vessel.node import TargetMode, TargetVessel, TargetVesselNode


def test_intelligent_target_uses_colregs_fsm_with_ownship_observation():
    target = TargetVessel(
        mmsi=100,
        lat=63.01,
        lon=10.0,
        heading_deg=180.0,
        sog_kn=10.0,
        mode=TargetMode.INTELLIGENT,
        behavior_config=TargetBehaviorConfig(
            policy="colregs_rule_fsm",
            reaction_delay_s=0.0,
            min_turn_deg=30.0,
            rot_limit_deg_s=3.0,
        ),
    )
    own = VesselKinematics(lat=63.0, lon=10.0, heading_deg=0.0, sog_mps=10.0 * 0.514444)
    state = target.step(dt=1.0, ownship=own, now_s=1.0)
    assert state["mode"] == "intelligent"
    assert math.degrees(target.heading) > 180.0
    assert state["rot"] > 0.0


def test_colregs_target_without_ownship_degrades_to_nominal():
    target = TargetVessel(
        mmsi=101,
        lat=63.01,
        lon=10.0,
        heading_deg=180.0,
        sog_kn=10.0,
        mode=TargetMode.INTELLIGENT,
        behavior_config=TargetBehaviorConfig(policy="colregs_rule_fsm", reaction_delay_s=0.0),
    )
    state = target.step(dt=1.0, ownship=None, now_s=1.0)
    assert math.degrees(target.heading) == 180.0
    assert state["rot"] == 0.0


def test_node_configure_rejects_multiple_colregs_targets():
    node = TargetVesselNode()
    entries = [
        {
            "static": {"mmsi": 100},
            "initial": {"position": {"latitude": 63.0, "longitude": 10.0}, "heading": 0.0, "sog": 10.0},
            "source": {"type": "route"},
            "behavior": {"policy": "colregs_rule_fsm"},
        },
        {
            "static": {"mmsi": 101},
            "initial": {"position": {"latitude": 63.1, "longitude": 10.0}, "heading": 180.0, "sog": 10.0},
            "source": {"type": "route"},
            "behavior": {"policy": "colregs_rule_fsm"},
        },
    ]
    params = {"default_targets_json": json.dumps(entries), "root_seed": 0, "episode": 0, "worker": 0}
    node.declare_parameter = MagicMock()
    node.get_parameter = lambda name: type("P", (), {"value": params[name]})()
    result = node.on_configure(MagicMock())
    assert result.name == "ERROR" or str(result).endswith("ERROR")
```

- [ ] **Step 2: Run node FSM tests and verify failure**

Run:

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/.worktrees/target-vessel-colregs-fsm
PYTHONPATH=src/sim_workbench/sil_nodes/target_vessel:src/sim_workbench/sil_nodes/sil_common pytest -q src/sim_workbench/sil_nodes/target_vessel/test/test_target_vessel_node_colregs.py
```

Expected: FAIL because `TargetVessel.__init__` does not accept `behavior_config`.

- [ ] **Step 3: Update `TargetVessel` constructor and step**

Modify `src/sim_workbench/sil_nodes/target_vessel/target_vessel/node.py`:

- Add imports:

```python
from target_vessel.colregs_behavior import ColregsRuleFsm
from target_vessel.config import (
    TargetBehaviorConfig,
    count_colregs_rule_targets,
    normalize_target_config,
)
from target_vessel.geometry import VesselKinematics, apply_rot_limit
```

- Extend `TargetVessel.__init__` signature:

```python
        behavior_config: TargetBehaviorConfig | None = None,
```

- In constructor, set:

```python
        self._nominal_heading = self.heading
        self._last_rot_deg_s = 0.0
        self._behavior_config = behavior_config
        self._fsm = (
            ColregsRuleFsm(behavior_config)
            if behavior_config is not None and behavior_config.policy == "colregs_rule_fsm"
            else None
        )
```

- Replace the beginning of `step()` with:

```python
    def step(
        self,
        dt: float = 0.1,
        ownship: VesselKinematics | None = None,
        now_s: float | None = None,
    ) -> dict:
        self._time += dt
        self._last_rot_deg_s = 0.0
        if self.mode == TargetMode.NCDM:
            dH = (-self._ou_theta * (self.heading - self._heading_ref) * dt
                  + self._ou_sigma * math.sqrt(dt) * self.rng.normal())
            self.heading += dH
        elif self._fsm is not None and ownship is not None:
            target = VesselKinematics(
                lat=self.lat,
                lon=self.lon,
                heading_deg=math.degrees(self.heading) % 360.0,
                sog_mps=self.sog,
            )
            action = self._fsm.update(
                now_s=self._time if now_s is None else now_s,
                own=ownship,
                target=target,
                nominal_heading_deg=math.degrees(self._nominal_heading) % 360.0,
            )
            next_heading_deg, rot_deg_s = apply_rot_limit(
                math.degrees(self.heading) % 360.0,
                action.desired_heading_deg,
                self._behavior_config.rot_limit_deg_s,
                dt,
            )
            self.heading = math.radians(next_heading_deg)
            self._last_rot_deg_s = rot_deg_s
            if action.desired_sog_mps is not None:
                self.sog = min(self.sog, action.desired_sog_mps)
```

- In return dict, set:

```python
            "rot": math.radians(self._last_rot_deg_s),
```

- [ ] **Step 4: Update `TargetVesselNode` target loading**

In `TargetVesselNode.__init__`, add:

```python
        self._latest_ownship = None
        self._ownship_sub = None
```

In `add_target(...)`, add optional `behavior_config=None` parameter and pass it to `TargetVessel(...)`.

In `on_configure`, replace per-entry handling with:

```python
                entries = json.loads(raw)
                configs = [normalize_target_config(entry) for entry in entries]
                if count_colregs_rule_targets(configs) > 1:
                    self._logger.error("Only one colregs_rule_fsm target is supported in v1")
                    return TransitionCallbackReturn.ERROR
                for entry, cfg in zip(entries, configs):
                    if isinstance(entry, dict) and "static" in entry and "initial" in entry:
                        mmsi = int(entry["static"].get("mmsi", 0))
                        initial = entry["initial"]
                        pos = initial.get("position", {})
                        lat = float(pos.get("latitude", 0.0))
                        lon = float(pos.get("longitude", 0.0))
                        heading_deg = float(initial.get("heading", initial.get("cog", 0.0)))
                        sog_kn = float(initial.get("sog", 0.0))
                        mode = "intelligent" if cfg.behavior.policy == "colregs_rule_fsm" else cfg.behavior.policy
                        if mode == "passive":
                            mode = "replay"
                        self.add_target(
                            mmsi,
                            lat,
                            lon,
                            heading_deg,
                            sog_kn,
                            mode,
                            behavior_config=cfg.behavior,
                        )
```

When catching config errors, log the exception and return `TransitionCallbackReturn.ERROR`.

- [ ] **Step 5: Add ownship subscription**

In `on_activate`, after creating target publisher, subscribe:

```python
        self._ownship_sub = self.create_subscription(
            OwnShipState, "/sil/own_ship_state", self._handle_ownship_state, qos
        )
```

Add method:

```python
    def _handle_ownship_state(self, msg):
        self._latest_ownship = VesselKinematics(
            lat=msg.lat,
            lon=msg.lon,
            heading_deg=math.degrees(msg.heading) % 360.0,
            sog_mps=msg.sog,
        )
```

In `on_deactivate`, destroy `_ownship_sub` if not `None`.

In `_step_callback`, replace `t.step(dt=dt)` with:

```python
                t.step(dt=dt, ownship=self._latest_ownship, now_s=self._time if hasattr(self, "_time") else None)
```

If no node-level `_time` exists, pass `None`; each target tracks its own `_time`.

- [ ] **Step 6: Run target-vessel tests**

Run:

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/.worktrees/target-vessel-colregs-fsm
PYTHONPATH=src/sim_workbench/sil_nodes/target_vessel:src/sim_workbench/sil_nodes/sil_common pytest -q \
  tests/sil/test_target_vessel.py \
  src/sim_workbench/sil_nodes/target_vessel/test/test_target_vessel_ou.py \
  src/sim_workbench/sil_nodes/target_vessel/test/test_target_vessel_node_colregs.py
```

Expected: all selected tests pass.

- [ ] **Step 7: Commit Task 4**

```bash
git add src/sim_workbench/sil_nodes/target_vessel/target_vessel/node.py \
        src/sim_workbench/sil_nodes/target_vessel/test/test_target_vessel_node_colregs.py \
        tests/sil/test_target_vessel.py \
        src/sim_workbench/sil_nodes/target_vessel/test/test_target_vessel_ou.py
git commit -m "feat(target-vessel): wire colregs fsm into runtime node"
```

### Task 5: Orchestrator and Schema Compatibility

**Files:**
- Modify: `src/sil_orchestrator/encounters_routes.py`
- Modify: `src/sil_orchestrator/tests/test_encounters_routes.py`
- Modify: `tests/sil_orchestrator/test_scenario_injection.py`
- Modify: `scenarios/fcb_traffic_situation.schema.json`

**Interfaces:**
- Consumes: `mode` already accepted by `LifecycleBridge.add_target(...)`.
- Produces:
  - `InjectBody.mode: Optional[str] = None`
  - REST body can pass `mode="intelligent"` while default remains `replay`.
  - Schema accepts optional `source` and `behavior`.

- [ ] **Step 1: Add REST mode tests**

In `src/sil_orchestrator/tests/test_encounters_routes.py`, add:

```python
@pytest.mark.asyncio
async def test_inject_accepts_intelligent_mode(monkeypatch):
    inject = next(route.endpoint for route in router.routes if route.path == "/api/v1/encounters/inject")
    bridge = FakeBridge()
    bridge.added = []

    async def fake_add_target(**kwargs):
        bridge.added.append(kwargs)
        return SimpleNamespace(success=True, message="ok")

    bridge.add_target = fake_add_target
    request = SimpleNamespace(app=SimpleNamespace(state=SimpleNamespace(bridge=bridge)))
    body = InjectBody(rule="head_on", mode="intelligent")
    result = await inject(body, request)
    assert result["accepted"] is True
    assert bridge.added[0]["mode"] == "intelligent"
```

If the test file does not already define `FakeBridge` or `SimpleNamespace`, reuse the existing fixture style in that file and import `types.SimpleNamespace`.

- [ ] **Step 2: Run REST test and verify failure**

Run:

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/.worktrees/target-vessel-colregs-fsm
PYTHONPATH=src pytest -q src/sil_orchestrator/tests/test_encounters_routes.py::test_inject_accepts_intelligent_mode
```

Expected: FAIL because `InjectBody` has no `mode` field or route ignores it.

- [ ] **Step 3: Implement REST mode field**

In `src/sil_orchestrator/encounters_routes.py`:

```python
class InjectBody(BaseModel):
    rule: str
    range_nm: Optional[float] = None
    construct_cpa_m: Optional[float] = None
    approach_angle_deg: Optional[float] = None
    mode: Optional[str] = None
```

Replace:

```python
            heading_deg=spawn.course_deg, sog_kn=spawn.sog_kn, mode="replay")
```

with:

```python
            heading_deg=spawn.course_deg,
            sog_kn=spawn.sog_kn,
            mode=body.mode or "replay")
```

- [ ] **Step 4: Add scenario injection compatibility tests**

In `tests/sil_orchestrator/test_scenario_injection.py`, add a test that `_extract_injection_params` preserves `source/behavior` inside `default_targets_json`:

```python
def test_target_vessel_params_preserve_source_behavior():
    scenario = {
        "targetShips": [
            {
                "id": "ts1",
                "static": {"mmsi": 100000001},
                "initial": {
                    "position": {"latitude": 63.0, "longitude": 10.0},
                    "heading": 180.0,
                    "sog": 10.0,
                },
                "source": {"type": "route"},
                "behavior": {"policy": "colregs_rule_fsm", "reaction_delay_s": 6.0},
            }
        ]
    }
    result = _extract_injection_params(scenario)
    targets = json.loads(result["target_vessel_node"]["default_targets_json"][0])
    assert targets[0]["source"]["type"] == "route"
    assert targets[0]["behavior"]["policy"] == "colregs_rule_fsm"
```

Run:

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/.worktrees/target-vessel-colregs-fsm
PYTHONPATH=src pytest -q tests/sil_orchestrator/test_scenario_injection.py::test_target_vessel_params_preserve_source_behavior
```

Expected: PASS without code change because `lifecycle_bridge.py` already JSON-dumps `targetShips`.

- [ ] **Step 5: Update JSON schema**

In `scenarios/fcb_traffic_situation.schema.json`, under each `targetShips.items.properties`, add:

```json
"source": {
  "type": "object",
  "properties": {
    "type": {
      "type": "string",
      "enum": ["route", "injected_geometry", "ais_replay", "ais_live"]
    },
    "trajectory_file": {
      "type": "string"
    }
  },
  "additionalProperties": true
},
"behavior": {
  "type": "object",
  "properties": {
    "policy": {
      "type": "string",
      "enum": ["passive", "ncdm", "colregs_rule_fsm", "intelligent_planner", "tdl_agent"]
    },
    "reaction_delay_s": {"type": "number", "minimum": 0},
    "min_turn_deg": {"type": "number", "exclusiveMinimum": 0},
    "rot_limit_deg_s": {"type": "number", "exclusiveMinimum": 0},
    "role_lock_s": {"type": "number", "minimum": 0},
    "target_cpa_m": {"type": "number", "exclusiveMinimum": 0},
    "standon_hold_tcpa_s": {"type": "number", "exclusiveMinimum": 0},
    "standon_action_tcpa_s": {"type": "number", "exclusiveMinimum": 0},
    "emergency_tcpa_s": {"type": "number", "exclusiveMinimum": 0},
    "observed_action_heading_delta_deg": {"type": "number", "exclusiveMinimum": 0},
    "observed_action_dcpa_gain_m": {"type": "number", "minimum": 0},
    "clear_dwell_s": {"type": "number", "minimum": 0},
    "return_cooldown_s": {"type": "number", "minimum": 0}
  },
  "additionalProperties": true
}
```

Keep `model` in the schema for compatibility.

- [ ] **Step 6: Run orchestrator tests**

Run:

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/.worktrees/target-vessel-colregs-fsm
PYTHONPATH=src pytest -q \
  src/sil_orchestrator/tests/test_encounters_routes.py \
  tests/sil_orchestrator/test_scenario_injection.py
```

Expected: pass.

- [ ] **Step 7: Commit Task 5**

```bash
git add src/sil_orchestrator/encounters_routes.py \
        src/sil_orchestrator/tests/test_encounters_routes.py \
        tests/sil_orchestrator/test_scenario_injection.py \
        scenarios/fcb_traffic_situation.schema.json
git commit -m "feat(sil): allow route target behavior policy config"
```

### Task 6: New Intelligent Target Scenarios

**Files:**
- Create: `scenarios/COLREGs测试/colreg-rule14-ho-intelligent.yaml`
- Create: `scenarios/COLREGs测试/colreg-rule15-cs-intelligent.yaml`
- Create: `scenarios/COLREGs测试/colreg-rule17-cr-so-target-giveway.yaml`
- Do not modify clean8 scenario lists or default gates.

**Interfaces:**
- Consumes: YAML compatibility from Task 5 and `colregs_rule_fsm` runtime from Task 4.
- Produces: three opt-in scenarios for targeted testing only.

- [ ] **Step 1: Copy baseline scenarios**

Run:

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/.worktrees/target-vessel-colregs-fsm
cp scenarios/COLREGs测试/colreg-rule14-ho.yaml scenarios/COLREGs测试/colreg-rule14-ho-intelligent.yaml
cp scenarios/COLREGs测试/colreg-rule15-cs.yaml scenarios/COLREGs测试/colreg-rule15-cs-intelligent.yaml
cp scenarios/COLREGs测试/colreg-rule17-cr-so.yaml scenarios/COLREGs测试/colreg-rule17-cr-so-target-giveway.yaml
```

- [ ] **Step 2: Edit each copied scenario**

For every target in the three copied files, add:

```yaml
  source:
    type: route
  behavior:
    policy: colregs_rule_fsm
    reaction_delay_s: 6.0
    min_turn_deg: 30.0
    rot_limit_deg_s: 3.0
    target_cpa_m: 900.0
```

Change metadata `scenario_id` in each file:

```yaml
metadata:
  scenario_id: colreg-rule14-ho-intelligent-v1.0
```

```yaml
metadata:
  scenario_id: colreg-rule15-cs-intelligent-v1.0
```

```yaml
metadata:
  scenario_id: colreg-rule17-cr-so-target-giveway-v1.0
```

Do not edit original scenario files.

- [ ] **Step 3: Validate new files parse**

Run:

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/.worktrees/target-vessel-colregs-fsm
python3 - <<'PY'
import yaml
from pathlib import Path
for name in [
    "colreg-rule14-ho-intelligent.yaml",
    "colreg-rule15-cs-intelligent.yaml",
    "colreg-rule17-cr-so-target-giveway.yaml",
]:
    path = Path("scenarios/COLREGs测试") / name
    data = yaml.safe_load(path.read_text())
    target = data["targetShips"][0]
    assert target["source"]["type"] == "route"
    assert target["behavior"]["policy"] == "colregs_rule_fsm"
    print(path, data["metadata"]["scenario_id"])
PY
```

Expected: prints all three paths and scenario IDs without assertion failure.

- [ ] **Step 4: Commit Task 6**

```bash
git add scenarios/COLREGs测试/colreg-rule14-ho-intelligent.yaml \
        scenarios/COLREGs测试/colreg-rule15-cs-intelligent.yaml \
        scenarios/COLREGs测试/colreg-rule17-cr-so-target-giveway.yaml
git commit -m "test(scenarios): add opt-in intelligent target cases"
```

### Task 7: Targeted Verification

**Files:**
- Modify only if test discovery requires path fixes:
  - `tests/sil/test_target_vessel.py`
  - `src/sim_workbench/sil_nodes/target_vessel/test/test_target_vessel_ou.py`
  - `src/sil_orchestrator/tests/test_encounters_routes.py`
  - `tests/sil_orchestrator/test_scenario_injection.py`

**Interfaces:**
- Consumes all prior tasks.
- Produces evidence that local pure/unit/orchestrator layers pass.

- [ ] **Step 1: Run target-vessel unit suite**

Run:

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/.worktrees/target-vessel-colregs-fsm
PYTHONPATH=src/sim_workbench/sil_nodes/target_vessel:src/sim_workbench/sil_nodes/sil_common pytest -q \
  tests/sil/test_target_vessel.py \
  src/sim_workbench/sil_nodes/target_vessel/test/test_target_vessel_ou.py \
  src/sim_workbench/sil_nodes/target_vessel/test/test_target_vessel_config.py \
  src/sim_workbench/sil_nodes/target_vessel/test/test_colregs_geometry.py \
  src/sim_workbench/sil_nodes/target_vessel/test/test_colregs_rule_fsm.py \
  src/sim_workbench/sil_nodes/target_vessel/test/test_target_vessel_node_colregs.py
```

Expected: all selected tests pass.

- [ ] **Step 2: Run orchestrator unit suite**

Run:

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/.worktrees/target-vessel-colregs-fsm
PYTHONPATH=src pytest -q \
  src/sil_orchestrator/tests/test_encounters_routes.py \
  tests/sil_orchestrator/test_scenario_injection.py
```

Expected: all selected tests pass.

- [ ] **Step 3: Run static grep guard against TDL internal decision topic subscriptions**

Run:

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/.worktrees/target-vessel-colregs-fsm
python3 - <<'PY'
from pathlib import Path
text = Path("src/sim_workbench/sil_nodes/target_vessel/target_vessel/node.py").read_text()
for forbidden in [
    "/l3/m5/avoidance_plan",
    "/l3/m4/behavior_plan",
    "/l3/m6/colregs_constraint",
]:
    assert forbidden not in text, forbidden
print("target_vessel_node does not subscribe to forbidden TDL decision topics")
PY
```

Expected: prints guard pass.

- [ ] **Step 4: Run scenario parser check**

Run the same parser command from Task 6 Step 3.

Expected: all three intelligent scenario files parse and contain `colregs_rule_fsm`.

- [ ] **Step 5: Run old clean8 separately**

Use the existing project clean8 command from current repo practice. Keep this run separate from new intelligent scenarios:

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/.worktrees/target-vessel-colregs-fsm
TDL_RUNTIME_PROFILE=internal-local ./scripts/run_6_scenarios.py --strict --restart-between-runs
```

Expected: old passive clean8 behavior is not changed by default. If the script currently expects a different endpoint or argument set, first run:

```bash
./scripts/run_6_scenarios.py --help
```

and use the documented strict/restart equivalent. Record the exact command and result in final handoff.

- [ ] **Step 6: Run local OrbStack gate only after targeted tests pass**

Run:

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/.worktrees/target-vessel-colregs-fsm
source scripts/local-a4000-env.sh
RECLAIM_STALE_LOCAL_PROJECT=1 ./scripts/local-a4000-acceptance.sh
```

Expected: local A4000-equivalent gate passes and writes evidence under `runs/`.

- [ ] **Step 7: Commit verification-only fixes if any**

If Step 1-6 required test harness fixes, commit only those fixes:

```bash
git add <changed-test-or-harness-files>
git commit -m "test(target-vessel): verify colregs fsm integration"
```

If no files changed, do not create an empty commit.

### Task 8: Final Documentation and Handoff

**Files:**
- Modify: `handoff/workspace_log.md`
- Optional modify: `docs/superpowers/specs/2026-06-18-target-vessel-source-behavior-design.md`
- Optional modify: `docs/superpowers/plans/2026-06-18-target-vessel-colregs-fsm.md`

**Interfaces:**
- Consumes verification evidence paths and command outputs from Task 7.
- Produces a handoff entry with changed paths, test commands, test results, and A4000 requirement.

- [ ] **Step 1: Append workspace handoff**

Append to `handoff/workspace_log.md`:

```markdown
## [2026-06-18] Codex / Git Commit / Target Vessel COLREGs FSM

- **Task Goal**: Add opt-in COLREGs rule-FSM behavior for route-driven simulated target vessels while preserving passive replay, AIS truth, and clean8 defaults.
- **Core Changes**: Added target source/behavior normalization, COLREGs geometry helpers, route-only Rule 14/15/16/17 FSM, target_vessel_node ownship observation wiring, REST intelligent injection support, schema fields, and three opt-in intelligent target scenarios.
- **Current Status**: <GREEN/AMBER/RED based on verification>.
- **Verification**: <exact commands and pass/fail results>.
- **Evidence**: <local runs paths if generated>.
- **Handoff Notes**: New intelligent scenarios are targeted-only and not part of clean8. A4000 validation is required before promotion if runtime behavior changes are accepted for integration.
```

Replace angle-bracket fields with real values from Task 7.

- [ ] **Step 2: Run final status and diff check**

Run:

```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/.worktrees/target-vessel-colregs-fsm
git status --short
git diff --check
```

Expected: `git diff --check` produces no output.

- [ ] **Step 3: Final commit**

Commit handoff and any doc updates:

```bash
git add handoff/workspace_log.md \
        docs/superpowers/specs/2026-06-18-target-vessel-source-behavior-design.md \
        docs/superpowers/plans/2026-06-18-target-vessel-colregs-fsm.md
git commit -m "docs(target-vessel): document colregs fsm rollout"
```

If the docs were already committed before implementation and only `handoff/workspace_log.md` changed, stage only `handoff/workspace_log.md`.

## Self-Review

- Spec coverage: source/behavior schema, route-only FSM, AIS restriction, observation-only target behavior, one-FSM-target v1 limit, point-mass plus ROT, diagnostics, clean8 isolation, and worktree isolation are covered by tasks.
- Placeholder scan: no bare `TBD` or `TODO` appears in implementation tasks.
- Type consistency: `TargetBehaviorConfig`, `NormalizedTargetConfig`, `VesselKinematics`, `TargetAction`, and `ColregsRuleFsm.update(...)` are consistently named across tasks.
- Known implementation caution: Task 3 synthetic geometry may need coordinate adjustment if bearing classification differs from the intended scenario. Only adjust test coordinates, not thresholds, unless current repo evidence shows a stricter rule threshold is required.
