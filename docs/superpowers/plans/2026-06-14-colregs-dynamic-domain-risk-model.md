# COLREGs Dynamic Domain Risk Model Implementation Plan

> **For:** approved spec `docs/superpowers/specs/2026-06-14-colregs-dynamic-domain-risk-model-design.md`
> **Branch:** `codex/colregs-release-work`
> **Goal:** add one shared dynamic ship-domain risk model, make 8-probe evaluate safety and seamanship with the same risk semantics, then make M4/M5/M7 consume that model.
> **Workflow:** local branch only, stage explicit touched paths, no push, local targeted tests before local OrbStack gate, A4000 only after local gates pass.

## Completion Criteria

- `scripts/run_6_scenarios.py` reports dynamic-domain and seamanship metrics for all clean COLREGs probes.
- 8-probe gates include danger-domain exposure, warning-domain exposure, risk-score recovery, route XTE, integrated XTE, overshoot count, path length ratio, and primary-threat switches.
- `m4_behavior_arbiter`, `m5_tactical_planner`, and `m7_safety_supervisor` consume a shared C++ risk model package.
- Python runner model and C++ model match golden fixtures within `1e-3`.
- Targeted tests pass for risk model, runner gate tests, M4, M5, and M7.
- Local OrbStack acceptance passes before any A4000 sync.

## Design Constraints

- Keep risk model independent: no dependencies on M4, M5, M7, ROS nodes, or simulator code.
- Keep M7 checker simpler than M4/M5 doers: M7 may evaluate the same domain primitives, but must not consume M4/M5 plan internals as proof of safety.
- Do not replace existing CPA/XTE gates. Add dynamic-domain gates as stricter evidence.
- Preserve COLREGs Rule 15 default: give-way vessel normally avoids by starboard alteration, but speed reduction or combined action can be chosen when risk margin and route-seamanship evidence are better.
- Avoid vessel-specific hardcoding outside model configuration. Defaults use `loa_m=46.0` and can be overridden by inputs.

## Files To Create

- `src/l3_tdl_kernel/l3_risk_model/package.xml`
- `src/l3_tdl_kernel/l3_risk_model/CMakeLists.txt`
- `src/l3_tdl_kernel/l3_risk_model/include/l3_risk_model/risk_model.hpp`
- `src/l3_tdl_kernel/l3_risk_model/src/risk_model.cpp`
- `src/l3_tdl_kernel/l3_risk_model/test/test_risk_model.cpp`
- `src/l3_tdl_kernel/l3_risk_model/test/fixtures/risk_golden_cases.json`
- `src/l3_tdl_kernel/l3_risk_model/python/l3_risk_model/__init__.py`
- `src/l3_tdl_kernel/l3_risk_model/python/l3_risk_model/risk_model.py`
- `tests/risk_model/test_risk_model_py.py`
- `tests/risk_model/test_cpp_python_parity.py`

## Files To Modify

- `scripts/run_6_scenarios.py`
- `tests/scripts/test_run_6_scenarios_gate.py`
- `src/l3_tdl_kernel/m4_behavior_arbiter/CMakeLists.txt`
- `src/l3_tdl_kernel/m4_behavior_arbiter/package.xml`
- `src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/colregs_directive.hpp`
- `src/l3_tdl_kernel/m4_behavior_arbiter/src/colregs_directive.cpp`
- `src/l3_tdl_kernel/m4_behavior_arbiter/src/m4_node.cpp`
- `src/l3_tdl_kernel/m4_behavior_arbiter/test/test_colregs_directive.cpp`
- `src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt`
- `src/l3_tdl_kernel/m5_tactical_planner/package.xml`
- `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/common/types.hpp`
- `src/l3_tdl_kernel/m5_tactical_planner/src/common/geometric_fallback.cpp`
- `src/l3_tdl_kernel/m5_tactical_planner/test/test_geometric_fallback.cpp`
- `src/l3_tdl_kernel/m7_safety_supervisor/CMakeLists.txt`
- `src/l3_tdl_kernel/m7_safety_supervisor/package.xml`
- `src/l3_tdl_kernel/m7_safety_supervisor/include/m7_safety_supervisor/core/performance_monitor.hpp`
- `src/l3_tdl_kernel/m7_safety_supervisor/src/core/performance_monitor.cpp`
- `src/l3_tdl_kernel/m7_safety_supervisor/test/test_performance_monitor.cpp`
- `handoff/workspace_log.md`

## Task 1: Create Shared C++ Risk Model Package

**Goal:** Add a pure C++ package with dynamic-domain math and fixture-backed unit tests.

1. Create failing C++ unit test first:

   File: `src/l3_tdl_kernel/l3_risk_model/test/test_risk_model.cpp`

   Test cases:
   - forward target inside danger domain returns `RiskPhase::Critical` or `RiskPhase::Danger` depending on `tcpa_s`.
   - starboard crossing target has smaller starboard margin than port-side mirror target.
   - opening target outside warning returns `RiskPhase::Clear`.
   - risk score rises when `colregs_duty` changes from `StandOnHold` to `GiveWay`.

   Use exact values:

   ```cpp
   const mass_l3::risk::OwnShipInput own{
     .x_m = 0.0,
     .y_m = 0.0,
     .heading_rad = 0.0,
     .sog_mps = 5.0,
     .loa_m = 46.0,
     .confidence = 0.95,
     .odd_degraded = false
   };

   const mass_l3::risk::TargetInput target{
     .id = "TS001",
     .x_m = 280.0,
     .y_m = 0.0,
     .cog_rad = 3.141592653589793,
     .sog_mps = 5.0,
     .cpa_m = 120.0,
     .tcpa_s = 45.0,
     .confidence = 0.9
   };
   ```

2. Run failure command:

   ```bash
   colcon test --packages-select l3_risk_model --event-handlers console_direct+
   ```

   Expected failure before implementation: package not found or missing header.

3. Add package skeleton:

   File: `src/l3_tdl_kernel/l3_risk_model/package.xml`

   Required dependencies:
   - `ament_cmake`
   - `ament_cmake_gtest`

   File: `src/l3_tdl_kernel/l3_risk_model/CMakeLists.txt`

   Required targets:
   - static or shared library `l3_risk_model`
   - exported include directory
   - exported CMake target
   - test target `test_risk_model`

4. Add public C++ API:

   File: `src/l3_tdl_kernel/l3_risk_model/include/l3_risk_model/risk_model.hpp`

   Required namespace and types:

   ```cpp
   namespace mass_l3::risk {

   enum class ColregsDuty : std::uint8_t {
     Free,
     StandOnHold,
     GiveWay,
     BothGiveWay,
     Rule17Action
   };

   enum class RiskPhase : std::uint8_t {
     Clear,
     Monitor,
     Warning,
     Danger,
     Critical
   };

   struct OwnShipInput {
     double x_m{0.0};
     double y_m{0.0};
     double heading_rad{0.0};
     double sog_mps{0.0};
     double loa_m{46.0};
     double confidence{1.0};
     bool odd_degraded{false};
   };

   struct TargetInput {
     std::string id;
     double x_m{0.0};
     double y_m{0.0};
     double cog_rad{0.0};
     double sog_mps{0.0};
     double cpa_m{0.0};
     double tcpa_s{0.0};
     double confidence{1.0};
   };

   struct DomainAxes {
     double forward_m{0.0};
     double astern_m{0.0};
     double starboard_m{0.0};
     double port_m{0.0};
   };

   struct DomainConfig {
     double superellipse_power{2.5};
     double warning_scale{1.8};
     double action_horizon_s{600.0};
     double emergency_horizon_s{180.0};
     double critical_horizon_s{60.0};
   };

   struct RiskVector {
     std::string target_id;
     double range_m{0.0};
     double relative_bearing_deg{0.0};
     double closing_speed_mps{0.0};
     double dcpa_m{0.0};
     double tcpa_s{0.0};
     double warning_margin_m{0.0};
     double danger_margin_m{0.0};
     double warning_ddv{0.0};
     double danger_ddv{0.0};
     double tdv_warning_s{0.0};
     double tdv_danger_s{0.0};
     double tde_warning_s{0.0};
     double tde_danger_s{0.0};
     ColregsDuty colregs_duty{ColregsDuty::Free};
     RiskPhase risk_phase{RiskPhase::Clear};
     double risk_score{0.0};
   };

   DomainAxes danger_axes(const OwnShipInput & own);
   DomainAxes warning_axes(const OwnShipInput & own, const DomainConfig & config);
   RiskVector evaluate_target(
     const OwnShipInput & own,
     const TargetInput & target,
     ColregsDuty duty,
     const DomainConfig & config = {});
   const char * to_string(RiskPhase phase) noexcept;
   const char * to_string(ColregsDuty duty) noexcept;

   }  // namespace mass_l3::risk
   ```

5. Implement domain math in `src/l3_tdl_kernel/l3_risk_model/src/risk_model.cpp`.

   Exact formulas:
   - body-frame coordinates:
     - `x_body = cos(hdg) * dx + sin(hdg) * dy`
     - `y_body = -sin(hdg) * dx + cos(hdg) * dy`
   - danger axes:
     - forward = `max(8L, speed*60 + 2L, 300)`
     - astern = `max(3L, 150)`
     - starboard = `max(5L, speed*30 + L, 220)`
     - port = `max(4L, speed*25 + L, 185)`
   - warning axes = danger axes times `warning_scale`
   - axis selector:
     - `x_body >= 0` uses forward else astern
     - `y_body >= 0` uses starboard else port
   - superellipse norm:
     - `pow(pow(abs(x_body/a), p) + pow(abs(y_body/b), p), 1/p)`
   - boundary range = `range / max(norm, 1e-6)`
   - margin = `range - boundary_range`
   - DDV = `max(0, 1 - norm)`

6. Implement risk phase:

   Rules:
   - `Critical` when `danger_ddv > 0` and `tcpa_s <= critical_horizon_s`.
   - `Danger` when `danger_ddv > 0` or `danger_margin_m < 0`.
   - `Warning` when `warning_ddv > 0` or `warning_margin_m < 0`.
   - `Monitor` when `tcpa_s >= 0`, `tcpa_s <= action_horizon_s`, and `dcpa_m < warning_axes.forward_m`.
   - `Clear` otherwise.

7. Implement risk score:

   ```cpp
   const double domain_component = std::max(warning_ddv * 0.7, danger_ddv);
   const double urgency_component = tcpa_s >= 0.0 ? std::exp(-tcpa_s / 180.0) : 0.0;
   const double closing_component = clamp(closing_speed_mps / 8.0, 0.0, 1.0);
   const double colregs_component = duty_weight(duty);
   const double uncertainty_component = clamp(1.0 - target.confidence, 0.0, 1.0);
   const double score = clamp(
     0.40 * domain_component +
     0.25 * urgency_component +
     0.15 * closing_component +
     0.15 * colregs_component +
     0.05 * uncertainty_component,
     0.0,
     1.0);
   ```

   Duty weights:
   - `GiveWay`: `1.0`
   - `BothGiveWay`: `1.0`
   - `Rule17Action`: `0.6`
   - `StandOnHold`: `0.3`
   - `Free`: `0.0`

8. Run C++ package test:

   ```bash
   colcon test --packages-select l3_risk_model --event-handlers console_direct+
   colcon test-result --verbose --test-result-base build/l3_risk_model
   ```

9. Commit Task 1 only:

   ```bash
   git status --short
   git add src/l3_tdl_kernel/l3_risk_model/package.xml \
     src/l3_tdl_kernel/l3_risk_model/CMakeLists.txt \
     src/l3_tdl_kernel/l3_risk_model/include/l3_risk_model/risk_model.hpp \
     src/l3_tdl_kernel/l3_risk_model/src/risk_model.cpp \
     src/l3_tdl_kernel/l3_risk_model/test/test_risk_model.cpp
   git commit -m "feat: add COLREGs dynamic risk model"
   ```

## Task 2: Add Multi-Target Ranking And Golden Fixtures

**Goal:** Rank targets consistently for future multi-ship scenarios and produce C++ fixture outputs.

1. Extend C++ header with ranking types:

   ```cpp
   struct RankingState {
     std::string previous_primary_id;
     std::string candidate_primary_id;
     std::uint32_t candidate_count{0U};
   };

   struct RankingConfig {
     double switch_score_gap{0.12};
     std::uint32_t switch_confirm_samples{2U};
   };

   struct RunRiskSummary {
     std::string primary_threat_id;
     std::uint32_t primary_threat_switches{0U};
     double max_risk_score{0.0};
     double worst_warning_margin_m{0.0};
     double worst_danger_margin_m{0.0};
     double max_warning_ddv{0.0};
     double max_danger_ddv{0.0};
     double warning_domain_exposure_s{0.0};
     double danger_domain_exposure_s{0.0};
     double encounter_complexity_score{0.0};
     std::vector<RiskVector> risks;
   };

   RiskVector select_primary(
     const std::vector<RiskVector> & risks,
     RankingState * state,
     const RankingConfig & config = {});
   ```

2. Add tests in `test_risk_model.cpp`:

   - danger phase outranks warning even when warning target score is close.
   - score gap below `0.12` keeps previous primary on first sample.
   - score gap below `0.12` switches on second consecutive sample.
   - score gap above `0.12` switches immediately.

3. Implement ordering:

   - Compare `RiskPhase` ordinal: `Critical > Danger > Warning > Monitor > Clear`.
   - Then compare `risk_score`.
   - Then compare smaller `tcpa_s` for non-negative TCPA.
   - Then compare smaller `range_m`.
   - Hysteresis only applies when phase is equal and score gap is `< switch_score_gap`.

4. Add golden fixture:

   File: `src/l3_tdl_kernel/l3_risk_model/test/fixtures/risk_golden_cases.json`

   Include three cases:

   ```json
   [
     {
       "case_id": "forward_danger_closing",
       "own": {"x_m": 0.0, "y_m": 0.0, "heading_rad": 0.0, "sog_mps": 5.0, "loa_m": 46.0, "confidence": 0.95, "odd_degraded": false},
       "target": {"id": "TS001", "x_m": 280.0, "y_m": 0.0, "cog_rad": 3.141592653589793, "sog_mps": 5.0, "cpa_m": 120.0, "tcpa_s": 45.0, "confidence": 0.9},
       "duty": "GiveWay"
     },
     {
       "case_id": "starboard_warning_crossing",
       "own": {"x_m": 0.0, "y_m": 0.0, "heading_rad": 0.0, "sog_mps": 5.0, "loa_m": 46.0, "confidence": 0.95, "odd_degraded": false},
       "target": {"id": "TS002", "x_m": 500.0, "y_m": 260.0, "cog_rad": 4.71238898038469, "sog_mps": 6.0, "cpa_m": 220.0, "tcpa_s": 180.0, "confidence": 0.9},
       "duty": "GiveWay"
     },
     {
       "case_id": "opening_clear",
       "own": {"x_m": 0.0, "y_m": 0.0, "heading_rad": 0.0, "sog_mps": 5.0, "loa_m": 46.0, "confidence": 0.95, "odd_degraded": false},
       "target": {"id": "TS003", "x_m": -800.0, "y_m": -500.0, "cog_rad": 3.141592653589793, "sog_mps": 3.0, "cpa_m": 900.0, "tcpa_s": -1.0, "confidence": 0.9},
       "duty": "Free"
     }
   ]
   ```

5. Add a C++ fixture dump test or executable that reads fixture cases and writes evaluated fields to stdout during test failure only. Keep committed fixture inputs human-readable. Store expected numeric outputs in test assertions, not generated files.

6. Run:

   ```bash
   colcon test --packages-select l3_risk_model --event-handlers console_direct+
   colcon test-result --verbose --test-result-base build/l3_risk_model
   ```

7. Commit Task 2 only:

   ```bash
   git status --short
   git add src/l3_tdl_kernel/l3_risk_model/include/l3_risk_model/risk_model.hpp \
     src/l3_tdl_kernel/l3_risk_model/src/risk_model.cpp \
     src/l3_tdl_kernel/l3_risk_model/test/test_risk_model.cpp \
     src/l3_tdl_kernel/l3_risk_model/test/fixtures/risk_golden_cases.json
   git commit -m "feat: rank COLREGs risk targets"
   ```

## Task 3: Add Python Parity Model For 8-Probe Runner

**Goal:** Let Python acceptance compute the same risk vectors without importing ROS packages.

1. Add Python module:

   File: `src/l3_tdl_kernel/l3_risk_model/python/l3_risk_model/risk_model.py`

   Required API:

   ```python
   from dataclasses import dataclass
   from enum import Enum
   from math import cos, sin, hypot, atan2, degrees, exp

   class ColregsDuty(str, Enum):
       FREE = "Free"
       STAND_ON_HOLD = "StandOnHold"
       GIVE_WAY = "GiveWay"
       BOTH_GIVE_WAY = "BothGiveWay"
       RULE17_ACTION = "Rule17Action"

   class RiskPhase(str, Enum):
       CLEAR = "Clear"
       MONITOR = "Monitor"
       WARNING = "Warning"
       DANGER = "Danger"
       CRITICAL = "Critical"

   @dataclass(frozen=True)
   class OwnShipInput:
       x_m: float
       y_m: float
       heading_rad: float
       sog_mps: float
       loa_m: float = 46.0
       confidence: float = 1.0
       odd_degraded: bool = False

   @dataclass(frozen=True)
   class TargetInput:
       id: str
       x_m: float
       y_m: float
       cog_rad: float
       sog_mps: float
       cpa_m: float
       tcpa_s: float
       confidence: float = 1.0
   ```

   Implement formulas exactly as Task 1.

2. Add Python tests:

   File: `tests/risk_model/test_risk_model_py.py`

   Cases:
   - same fixture inputs as C++ tests.
   - assert phase strings.
   - assert `risk_score` bounds.
   - assert starboard crossing warning margin is tighter than port mirror.

3. Add fixture parity tests:

   File: `tests/risk_model/test_cpp_python_parity.py`

   - Load `src/l3_tdl_kernel/l3_risk_model/test/fixtures/risk_golden_cases.json`.
   - Evaluate Python outputs.
   - Compare stable expected values committed in the test file for:
     - `risk_phase`
     - `warning_margin_m`
     - `danger_margin_m`
     - `warning_ddv`
     - `danger_ddv`
     - `risk_score`
   - Tolerance: `1e-3` for numeric fields.

4. Export package module:

   File: `src/l3_tdl_kernel/l3_risk_model/python/l3_risk_model/__init__.py`

   Export:
   - `OwnShipInput`
   - `TargetInput`
   - `ColregsDuty`
   - `RiskPhase`
   - `evaluate_target`
   - `select_primary`

5. Run:

   ```bash
   PYTHONPATH=src/l3_tdl_kernel/l3_risk_model/python \
     PYTHONDONTWRITEBYTECODE=1 \
     pytest tests/risk_model -q
   ```

6. Commit Task 3 only:

   ```bash
   git status --short
   git add src/l3_tdl_kernel/l3_risk_model/python/l3_risk_model/__init__.py \
     src/l3_tdl_kernel/l3_risk_model/python/l3_risk_model/risk_model.py \
     tests/risk_model/test_risk_model_py.py \
     tests/risk_model/test_cpp_python_parity.py
   git commit -m "test: add Python risk model parity"
   ```

## Task 4: Add 8-Probe Risk Metrics And Gates

**Goal:** Make acceptance fail when a route is CPA-safe but domain-unsafe or poor seamanship.

1. Inspect current runner structures in `scripts/run_6_scenarios.py`.

   Locate:
   - scenario loop
   - telemetry extraction
   - `min_cpa_m`
   - `max_route_xte_m`
   - `compute_route_return_status`
   - result dict creation

2. Add import fallback near other imports:

   ```python
   try:
       from l3_risk_model import (
           ColregsDuty,
           OwnShipInput,
           TargetInput,
           RiskPhase,
           evaluate_target,
           select_primary,
       )
   except ImportError:
       import sys
       from pathlib import Path
       repo_root = Path(__file__).resolve().parents[1]
       sys.path.insert(0, str(repo_root / "src/l3_tdl_kernel/l3_risk_model/python"))
       from l3_risk_model import (
           ColregsDuty,
           OwnShipInput,
           TargetInput,
           RiskPhase,
           evaluate_target,
           select_primary,
       )
   ```

3. Add pure helper functions in runner:

   - `_heading_deg_to_rad(heading_deg: float) -> float`
   - `_knots_to_mps(knots: float) -> float`
   - `_infer_colregs_duty(sample: dict) -> ColregsDuty`
   - `_target_input_from_sample(sample: dict, target: dict) -> TargetInput`
   - `_own_input_from_sample(sample: dict) -> OwnShipInput`
   - `_compute_integrated_abs_xte(samples: list[dict]) -> float`
   - `_compute_route_crossing_overshoots(samples: list[dict]) -> int`
   - `_compute_path_length_ratio(samples: list[dict], route_distance_m: float) -> float`
   - `_compute_risk_metrics(samples: list[dict]) -> dict`

4. Implement risk metric accumulation:

   For each telemetry sample:
   - evaluate each target with `evaluate_target`.
   - select primary with hysteresis.
   - track primary ID switches.
   - update max score, worst margins, max DDV.
   - add `dt` to `warning_domain_exposure_s` if any target phase is `Warning`, `Danger`, or `Critical`.
   - add `dt` to `danger_domain_exposure_s` if any target phase is `Danger` or `Critical`.
   - append compact trace row:

     ```python
     {
         "t_s": t_s,
         "primary_threat_id": primary.target_id,
         "risk_phase": primary.risk_phase.value,
         "risk_score": primary.risk_score,
         "warning_margin_m": primary.warning_margin_m,
         "danger_margin_m": primary.danger_margin_m,
         "warning_ddv": primary.warning_ddv,
         "danger_ddv": primary.danger_ddv,
     }
     ```

5. Add risk recovery slope check:

   - Find avoidance onset from first sample where avoidance mode or M4 phase indicates active avoidance.
   - Use first 60 seconds after onset.
   - Fit simple slope with two-point delta:
     - `score_at_window_end - score_at_window_start`
   - Gate passes when delta `< -0.02`.
   - Bypass only when first post-onset primary is outside warning and opening:
     - `warning_ddv == 0`
     - `closing_speed_mps <= 0`

6. Add new result fields:

   ```python
   result.update({
       "primary_threat_id": risk["primary_threat_id"],
       "primary_threat_switches": risk["primary_threat_switches"],
       "max_risk_score": risk["max_risk_score"],
       "worst_warning_margin_m": risk["worst_warning_margin_m"],
       "worst_danger_margin_m": risk["worst_danger_margin_m"],
       "max_warning_ddv": risk["max_warning_ddv"],
       "max_danger_ddv": risk["max_danger_ddv"],
       "warning_domain_exposure_s": risk["warning_domain_exposure_s"],
       "danger_domain_exposure_s": risk["danger_domain_exposure_s"],
       "risk_recovery_ok": risk["risk_recovery_ok"],
       "integrated_abs_xte_m_s": seamanship["integrated_abs_xte_m_s"],
       "route_crossing_overshoot_count": seamanship["route_crossing_overshoot_count"],
       "path_length_ratio": seamanship["path_length_ratio"],
   })
   ```

7. Add gates:

   - `danger_domain_exposure_s == 0.0` after first 5 seconds.
   - `warning_domain_exposure_s <= 120.0` for ordinary probes.
   - `max_danger_ddv == 0.0` unless scenario metadata marks close-start emergency.
   - `risk_recovery_ok is True`.
   - `max_route_xte_m < 500.0`.
   - `integrated_abs_xte_m_s <= 300000.0`.
   - `route_crossing_overshoot_count <= 1`.
   - `path_length_ratio <= 1.35`.
   - `primary_threat_switches <= 2` for single-target scenarios.

8. Update runner tests:

   File: `tests/scripts/test_run_6_scenarios_gate.py`

   Add cases:
   - CPA passes but danger-domain exposure fails.
   - max XTE below 500 but integrated XTE too high fails.
   - route crossing overshoot count `2` fails.
   - risk score does not decrease after onset fails.
   - all risk metrics present in JSON result.

9. Run:

   ```bash
   PYTHONPATH=src/l3_tdl_kernel/l3_risk_model/python \
     PYTHONDONTWRITEBYTECODE=1 \
     pytest tests/risk_model tests/scripts/test_run_6_scenarios_gate.py -q
   ```

10. Commit Task 4 only:

   ```bash
   git status --short
   git add scripts/run_6_scenarios.py \
     tests/scripts/test_run_6_scenarios_gate.py
   git commit -m "feat: gate COLREG probes on dynamic risk"
   ```

## Task 5: Evaluation-Only 8-Probe Baseline

**Goal:** Run strict local evaluation before changing M4/M5/M7 behavior.

1. Ensure local stack is running:

   ```bash
   docker compose ps
   curl -sk https://127.0.0.1:18000/api/v1/health
   curl -s http://localhost:3000/catalog >/tmp/martin_catalog.json
   ```

2. Run clean 8-probe:

   ```bash
   source scripts/local-a4000-env.sh
   PYTHONPATH=src/l3_tdl_kernel/l3_risk_model/python \
     PYTHONDONTWRITEBYTECODE=1 \
     SIL_ORCH_BASE_URL=https://127.0.0.1:18000/api/v1 \
     python scripts/run_6_scenarios.py
   ```

3. Save evidence path from runner output under `runs/`.

4. Inspect failures:

   ```bash
   python - <<'PY'
   import json
   from pathlib import Path
   path = max(Path("runs").glob("*8*probe*.json"), key=lambda p: p.stat().st_mtime)
   data = json.loads(path.read_text())
   print(path)
   for item in data.get("results", []):
       print(item.get("scenario"), item.get("pass"), item.get("failures"))
       print({
           "max_route_xte_m": item.get("max_route_xte_m"),
           "danger_domain_exposure_s": item.get("danger_domain_exposure_s"),
           "warning_domain_exposure_s": item.get("warning_domain_exposure_s"),
           "max_danger_ddv": item.get("max_danger_ddv"),
           "risk_recovery_ok": item.get("risk_recovery_ok"),
           "path_length_ratio": item.get("path_length_ratio"),
       })
   PY
   ```

5. Do not loosen gates to match current behavior. If baseline fails, record which probes fail and proceed to M4/M5/M7 consumption tasks.

6. No commit for evidence unless project convention requires committed acceptance JSON. Keep generated `runs/` evidence untracked unless user requests.

## Task 6: Make M4 Consume Risk Model

**Goal:** M4 selects behavior from primary risk target and can choose speed reduction when safer and more ship-like than a large route deviation.

1. Modify dependencies:

   Files:
   - `src/l3_tdl_kernel/m4_behavior_arbiter/package.xml`
   - `src/l3_tdl_kernel/m4_behavior_arbiter/CMakeLists.txt`

   Add:
   - `find_package(l3_risk_model REQUIRED)`
   - link `m4_core` or `m4_node_lib` to `l3_risk_model`

2. Extend directive data:

   File: `include/m4_behavior_arbiter/colregs_directive.hpp`

   Add fields:

   ```cpp
   std::string primary_threat_id;
   double primary_risk_score{0.0};
   double primary_warning_margin_m{0.0};
   double primary_danger_margin_m{0.0};
   std::string primary_risk_phase;
   bool speed_reduction_preferred{false};
   ```

3. Add helper in `src/colregs_directive.cpp`:

   ```cpp
   mass_l3::risk::ColregsDuty map_role_to_duty(
     std::uint8_t primary_role,
     bool conflict_active,
     bool rule15_active);
   ```

   Mapping:
   - Rule 15 active and own vessel give-way -> `GiveWay`
   - Rule 17 action phase -> `Rule17Action`
   - stand-on hold -> `StandOnHold`
   - both-action state -> `BothGiveWay`
   - no conflict -> `Free`

4. Change required deviation computation:

   Current range/CPA-only deviation stays as fallback.

   Add risk-aware scaling:

   ```cpp
   const double margin_pressure = clamp((-risk.primary_warning_margin_m) / 300.0, 0.0, 1.0);
   const double urgency_pressure = risk.tcpa_s >= 0.0 ? std::exp(-risk.tcpa_s / 180.0) : 0.0;
   const double risk_turn_deg = 10.0 + 50.0 * std::max(margin_pressure, urgency_pressure);
   ```

   Use:
   - min alteration `10-20 deg` for monitor/warning and opening.
   - stronger alteration only for danger/critical.
   - cap by existing `max_deviation_deg`.

5. Add speed-reduction preference:

   Conditions:
   - duty is `GiveWay` or `BothGiveWay`.
   - `tcpa_s > 180.0`.
   - target is outside danger domain.
   - predicted lower speed improves warning margin over 120 seconds.
   - route XTE pressure is high or planned heading change would push XTE farther from route.

   Result:
   - set `direction = ColregsDirection::ReduceSpeed` when speed-only wins.
   - set `speed_reduction_preferred = true`.
   - preserve `Starboard` for Rule 15 when speed-only does not produce margin improvement.

6. Update M4 rationale in `src/m4_node.cpp`:

   Include:
   - `primary_threat_id`
   - `primary_risk_phase`
   - `primary_risk_score`
   - `primary_warning_margin_m`
   - `primary_danger_margin_m`
   - `speed_reduction_preferred`

7. Add tests:

   File: `src/l3_tdl_kernel/m4_behavior_arbiter/test/test_colregs_directive.cpp`

   Cases:
   - far Rule 15 crossing with ample TCPA chooses reduce speed or small starboard alteration, not fixed 60 degree turn.
   - danger crossing chooses starboard alteration.
   - stand-on hold has lower risk score contribution than give-way.
   - hysteresis prevents primary threat flip for two close scores.

8. Run:

   ```bash
   colcon test --packages-select l3_risk_model m4_behavior_arbiter \
     --ctest-args -R 'test_risk_model|test_colregs_directive|test_m4_node_lifecycle' \
     --event-handlers console_direct+
   colcon test-result --verbose --test-result-base build/m4_behavior_arbiter
   ```

9. Commit Task 6 only:

   ```bash
   git status --short
   git add src/l3_tdl_kernel/m4_behavior_arbiter/package.xml \
     src/l3_tdl_kernel/m4_behavior_arbiter/CMakeLists.txt \
     src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/colregs_directive.hpp \
     src/l3_tdl_kernel/m4_behavior_arbiter/src/colregs_directive.cpp \
     src/l3_tdl_kernel/m4_behavior_arbiter/src/m4_node.cpp \
     src/l3_tdl_kernel/m4_behavior_arbiter/test/test_colregs_directive.cpp
   git commit -m "feat: make M4 arbitrate with dynamic risk"
   ```

## Task 7: Make M5 Score Plans With Risk And Corridor Pressure

**Goal:** M5 route generation stops blindly extending XTE and compares course, speed, and combined plans against risk margins.

1. Modify dependencies:

   Files:
   - `src/l3_tdl_kernel/m5_tactical_planner/package.xml`
   - `src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt`

   Add:
   - `find_package(l3_risk_model REQUIRED)`
   - link `m5_shared_lib` to `l3_risk_model`

2. Extend planner input:

   File: `include/m5_tactical_planner/common/types.hpp`

   Add a lightweight risk snapshot:

   ```cpp
   struct TargetRiskSnapshot {
     std::string target_id;
     double risk_score{0.0};
     double warning_margin_m{0.0};
     double danger_margin_m{0.0};
     double tcpa_s{0.0};
     bool primary{false};
   };
   ```

   Add to `MidMpcInput`:

   ```cpp
   std::vector<TargetRiskSnapshot> target_risks;
   double route_xte_m{0.0};
   double route_corridor_limit_m{500.0};
   ```

3. Compute risk snapshots in M5 node input assembly before fallback planning.

   For each target:
   - convert ownship and target state into `mass_l3::risk` inputs.
   - map M4 direction/role into `ColregsDuty`.
   - store primary target from `select_primary`.

4. Add plan-scoring function in `src/common/geometric_fallback.cpp`:

   ```cpp
   struct CandidateScore {
     double total{0.0};
     double predicted_min_warning_margin_m{0.0};
     double predicted_min_danger_margin_m{0.0};
     double projected_max_xte_m{0.0};
     double speed_loss_mps{0.0};
   };
   ```

   Score components:
   - `+10000` for predicted danger-domain intrusion.
   - `+2500` for warning-domain intrusion beyond 120 seconds.
   - `+6 * max(0, projected_max_xte_m - 350)`.
   - `+10000` for projected XTE above 500m.
   - `+path_length_ratio_penalty`.
   - `+speed_loss_penalty` only when speed reduction does not improve margin.
   - `-margin_recovery_bonus` when warning margin increases and XTE decreases.

5. Generate three candidate families when TCPA allows:

   - course-only starboard.
   - speed-only throttle reduction.
   - combined small starboard plus speed reduction.

   Candidate eligibility:
   - speed-only allowed when primary TCPA is above 180 seconds and target outside danger domain.
   - combined allowed when warning phase persists after course-only prediction.
   - port alteration remains blocked for Rule 15 give-way except emergency recovery chosen by M7.

6. Add route return behavior:

   - If primary target is outside warning domain and closing speed is non-positive, penalize headings that increase absolute XTE.
   - If current XTE is above 350m, prefer candidate with decreasing absolute XTE unless that creates danger-domain intrusion.
   - Never choose a candidate with projected max XTE above 500m unless every candidate is unsafe and M7 emergency state is active.

7. Update rationale fields published by M5:

   Include:
   - primary threat ID
   - predicted min warning margin
   - predicted min danger margin
   - candidate family selected
   - projected max XTE

8. Add tests:

   File: `src/l3_tdl_kernel/m5_tactical_planner/test/test_geometric_fallback.cpp`

   Cases:
   - far Rule 15 target with ample TCPA selects speed-only or combined plan over large starboard offset.
   - close danger target selects course or combined action with positive margin recovery.
   - candidate increasing XTE beyond 500m is rejected.
   - outside-warning opening target returns toward route instead of parallel off-route travel.

9. Run:

   ```bash
   colcon test --packages-select l3_risk_model m5_tactical_planner \
     --ctest-args -R 'test_risk_model|test_geometric_fallback' \
     --event-handlers console_direct+
   colcon test-result --verbose --test-result-base build/m5_tactical_planner
   ```

10. Commit Task 7 only:

   ```bash
   git status --short
   git add src/l3_tdl_kernel/m5_tactical_planner/package.xml \
     src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt \
     src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/common/types.hpp \
     src/l3_tdl_kernel/m5_tactical_planner/src/common/geometric_fallback.cpp \
     src/l3_tdl_kernel/m5_tactical_planner/test/test_geometric_fallback.cpp
   git commit -m "feat: make M5 score plans with risk margins"
   ```

## Task 8: Make M7 Consume Risk Model And Add Domain Veto

**Goal:** M7 reports the same risk semantics and independently vetoes unsafe danger-domain behavior.

1. Modify dependencies:

   Files:
   - `src/l3_tdl_kernel/m7_safety_supervisor/package.xml`
   - `src/l3_tdl_kernel/m7_safety_supervisor/CMakeLists.txt`

   Add:
   - `find_package(l3_risk_model REQUIRED)`
   - link `m7_lib` to `l3_risk_model`

2. Extend performance monitor state:

   File: `include/m7_safety_supervisor/core/performance_monitor.hpp`

   Add:

   ```cpp
   struct DomainRiskMonitorState {
     std::string primary_threat_id;
     double max_risk_score{0.0};
     double worst_warning_margin_m{0.0};
     double worst_danger_margin_m{0.0};
     double warning_domain_exposure_s{0.0};
     double danger_domain_exposure_s{0.0};
     bool danger_veto_active{false};
   };
   ```

3. Implement M7 risk evaluation:

   File: `src/core/performance_monitor.cpp`

   Per update:
   - evaluate all targets from current world state.
   - select primary threat.
   - update exposure timers with `dt`.
   - set `danger_veto_active` when current primary phase is `Danger` or `Critical` and commanded action does not reduce risk score over the next prediction step.

4. Keep veto independent from M4/M5:

   - Inputs are ownship state, target state, and current command.
   - Do not trust M4/M5 rationale as safety proof.
   - Use M4/M5 rationale only for log text.

5. Add tests:

   File: `src/l3_tdl_kernel/m7_safety_supervisor/test/test_performance_monitor.cpp`

   Cases:
   - danger-domain intrusion increments danger exposure.
   - warning-only intrusion does not increment danger exposure.
   - command that increases risk while in danger activates veto.
   - opening command while exiting danger does not activate veto.

6. If existing M7 independence test exists, extend it:

   - Assert no include from M4 or M5 directories in M7 headers.
   - Assert M7 links `l3_risk_model`, not `m4_core` or `m5_shared_lib`.

7. Run:

   ```bash
   colcon test --packages-select l3_risk_model m7_safety_supervisor \
     --ctest-args -R 'test_risk_model|test_performance_monitor|test_doer_checker_independence' \
     --event-handlers console_direct+
   colcon test-result --verbose --test-result-base build/m7_safety_supervisor
   ```

8. Commit Task 8 only:

   ```bash
   git status --short
   git add src/l3_tdl_kernel/m7_safety_supervisor/package.xml \
     src/l3_tdl_kernel/m7_safety_supervisor/CMakeLists.txt \
     src/l3_tdl_kernel/m7_safety_supervisor/include/m7_safety_supervisor/core/performance_monitor.hpp \
     src/l3_tdl_kernel/m7_safety_supervisor/src/core/performance_monitor.cpp \
     src/l3_tdl_kernel/m7_safety_supervisor/test/test_performance_monitor.cpp
   git commit -m "feat: let M7 supervise dynamic domain risk"
   ```

## Task 9: Run Full Targeted Test Set And Clean 8-Probe

**Goal:** Verify evaluation and control consume one risk model, then fix narrow failures without weakening gates.

1. Run pure unit tests:

   ```bash
   PYTHONPATH=src/l3_tdl_kernel/l3_risk_model/python \
     PYTHONDONTWRITEBYTECODE=1 \
     pytest tests/risk_model tests/scripts/test_run_6_scenarios_gate.py -q

   colcon test --packages-select l3_risk_model m4_behavior_arbiter m5_tactical_planner m7_safety_supervisor \
     --event-handlers console_direct+
   colcon test-result --verbose --test-result-base build/l3_risk_model
   colcon test-result --verbose --test-result-base build/m4_behavior_arbiter
   colcon test-result --verbose --test-result-base build/m5_tactical_planner
   colcon test-result --verbose --test-result-base build/m7_safety_supervisor
   ```

2. Rebuild local containers only if C++ packages used by SIL container changed:

   ```bash
   source scripts/local-a4000-env.sh
   docker compose build sil-nodes
   docker compose up -d sil-nodes sil-orchestrator foxglove-bridge martin-tile-server
   docker compose ps
   ```

3. Run clean 8-probe:

   ```bash
   source scripts/local-a4000-env.sh
   PYTHONPATH=src/l3_tdl_kernel/l3_risk_model/python \
     PYTHONDONTWRITEBYTECODE=1 \
     SIL_ORCH_BASE_URL=https://127.0.0.1:18000/api/v1 \
     python scripts/run_6_scenarios.py
   ```

4. Inspect results:

   Required pass conditions per scenario:
   - `pass == true`
   - `max_route_xte_m < 500`
   - `danger_domain_exposure_s == 0`
   - `warning_domain_exposure_s <= 120`
   - `max_danger_ddv == 0`
   - `risk_recovery_ok == true`
   - `route_crossing_overshoot_count <= 1`
   - `path_length_ratio <= 1.35`

5. If a probe fails:

   - Determine whether failure is evaluation bug or control behavior.
   - For evaluation bug, fix only runner or risk model.
   - For control behavior, fix the owning module:
     - M4 for action selection or release.
     - M5 for route geometry or candidate score.
     - M7 for safety veto.
   - Rerun the smallest failing test first.
   - Rerun clean 8-probe after the targeted test passes.

6. Commit narrow fixes by module. Stage explicit files only.

## Task 10: Local OrbStack Gate And Handoff Entry

**Goal:** Produce local integration evidence and a concise handoff for A4000 decision.

1. Run local OrbStack gate:

   ```bash
   source scripts/local-a4000-env.sh
   ./scripts/local-a4000-acceptance.sh
   ```

2. Record evidence paths:

   - clean 8-probe JSON under `runs/`
   - local acceptance JSON under `runs/local_a4000_container_probe_*.json`

3. Append handoff entry:

   File: `handoff/workspace_log.md`

   Entry format:

   ```markdown
   ## 2026-06-14 Agent

   - Git Commit: output of `git rev-parse --short HEAD` after final local gate.
   - Task Goal: COLREGs dynamic-domain risk model for 8-probe, M4, M5, and M7.
   - Core Changes: shared `l3_risk_model`, Python parity runner model, dynamic-domain gates, M4 risk arbitration, M5 risk-aware plan scoring, M7 domain supervision.
   - Current Status: local targeted tests passed; local OrbStack gate result recorded at the newest `runs/local_a4000_container_probe_*.json` file.
   - Handoff Notes: A4000 sync required before promotion. Sync only touched paths. Do not use git pull/reset or rsync --delete on A4000.
   ```

4. Commit handoff only:

   ```bash
   git status --short
   git add handoff/workspace_log.md
   git commit -m "docs: record COLREG risk model handoff"
   ```

5. Report to user:

   - branch name
   - changed paths
   - test commands and results
   - 8-probe evidence path
   - local OrbStack evidence path
   - whether A4000 narrow sync is required
   - whether A4000 acceptance is required

## Final Verification Commands

Run before claiming completion:

```bash
git branch --show-current
git status --short

PYTHONPATH=src/l3_tdl_kernel/l3_risk_model/python \
  PYTHONDONTWRITEBYTECODE=1 \
  pytest tests/risk_model tests/scripts/test_run_6_scenarios_gate.py -q

colcon test --packages-select l3_risk_model m4_behavior_arbiter m5_tactical_planner m7_safety_supervisor \
  --event-handlers console_direct+
colcon test-result --verbose --test-result-base build/l3_risk_model
colcon test-result --verbose --test-result-base build/m4_behavior_arbiter
colcon test-result --verbose --test-result-base build/m5_tactical_planner
colcon test-result --verbose --test-result-base build/m7_safety_supervisor

source scripts/local-a4000-env.sh
PYTHONPATH=src/l3_tdl_kernel/l3_risk_model/python \
  PYTHONDONTWRITEBYTECODE=1 \
  SIL_ORCH_BASE_URL=https://127.0.0.1:18000/api/v1 \
  python scripts/run_6_scenarios.py

source scripts/local-a4000-env.sh
./scripts/local-a4000-acceptance.sh
```

## A4000 Gate Decision

A4000 sync is required after local gates pass because this changes SIL backend behavior and acceptance criteria.

Touched-path sync set:

```text
src/l3_tdl_kernel/l3_risk_model/
src/l3_tdl_kernel/m4_behavior_arbiter/
src/l3_tdl_kernel/m5_tactical_planner/
src/l3_tdl_kernel/m7_safety_supervisor/
scripts/run_6_scenarios.py
tests/risk_model/
tests/scripts/test_run_6_scenarios_gate.py
handoff/workspace_log.md
```

A4000 commands after narrow sync:

```bash
source scripts/a4000-env.sh
npm run sys:start
./scripts/a4000-acceptance.sh
```

Promotion remains blocked until local targeted tests, local OrbStack gate, A4000 clean 8-probe, and A4000 acceptance all pass.
