# COLREGs Dynamic Domain Risk Model Design

## Goal

Replace fixed, scenario-specific CPA thinking with one shared dynamic ship-domain
risk model. The same model must first make the strict COLREGs 8-probe reject
unsafe or poor-seamanship runs, then be consumed by M4, M5, and M7 in the same
work package after the 8-probe baseline passes.

## Source-Backed Context

Current repo evidence:

- `scripts/run_6_scenarios.py` gates on minimum DCPA, route return, max route
  XTE, overtake completion when required, and behavioral stability. It does not
  compute instantaneous OS-TS separation, ship-domain violation, or time inside
  a dynamic safety domain.
- `src/sim_workbench/sil_nodes/scoring/scoring/stability_scorer.py` detects
  behavior flaps, plan-valid segments, steering reversals, ROT roughness, and
  premature stand-on give-way, but it does not score domain intrusion.
- `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp`
  currently compresses multi-target information to nearest target range,
  nearest CPA, and one COLREG directive when scaling the turn demand.
- `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` often
  falls back to a geometric COLREG plan driven by the M4 heading window.
- `src/l3_tdl_kernel/m7_safety_supervisor/src/sotif/performance_monitor.cpp`
  monitors minimum CPA trend and close-target count, not a shared dynamic
  domain margin.

Regulatory and research context from project NLM:

- Maritime regulations notebook, high confidence: COLREGs Rule 15 makes the
  vessel that has the other on its starboard side the give-way vessel; Rules 8
  and 16 require early and substantial action to keep well clear; Rule 17 keeps
  the stand-on vessel on course and speed until independent or critical action
  is justified. COLREGs do not mandate a fixed 0.5 NM CPA.
- COLAV algorithms notebook, high confidence: CPA/TCPA alone are insufficient
  for safety and naturalness. Ship-domain violation, degree of domain violation
  (DDV), time to domain violation (TDV), time to domain exit (TDE), XTE/route
  economy, and maneuver indecision are standard complementary metrics.
- COLAV algorithms notebook, high confidence: multi-ship threat handling should
  combine distance, DCPA/TCPA, dynamic ship-domain violation, COLREG duty, and
  multi-obstacle escape-route constraints. A single nearest-distance or
  nearest-CPA winner is not robust.

## Scope

This work package includes both evaluation and control consumption.

Phase order is mandatory:

1. Implement the shared dynamic-domain risk model and deterministic test
   fixtures.
2. Wire 8-probe metrics and gates to the model.
3. Pass local targeted tests and local 8-probe baseline with controller behavior
   unchanged.
4. Wire M4, M5, and M7 to consume the same risk semantics.
5. Re-run targeted tests, local 8-probe, and then the local OrbStack gate before
   any A4000 sync.

## Non-Goals

- Do not replace M5 with a full nonlinear planner rewrite in this pass.
- Do not remove existing CPA, XTE, route-return, or stability gates.
- Do not relax COLREGs Rule 15/16/17 obligations to make runs pass.
- Do not make M7 hard veto depend only on the doer-side M4/M5 risk output.
- Do not push to GitHub/GitLab or sync A4000 before local targeted tests and
  local OrbStack gate pass.

## Architecture

Create one shared risk model with clear ownership:

- A pure C++ risk-model library computes dynamic ship-domain metrics and
  per-target threat rankings from own-ship state, target states, route/corridor
  context, uncertainty, and optional COLREG role hints.
- A Python parity implementation is used by host-side runner tests and
  `scripts/run_6_scenarios.py`.
- Golden fixtures assert that the C++ and Python implementations produce the
  same risk phase, primary threat, domain margins, DDV, and risk score within
  explicit tolerances.
- This pass does not add a new ROS message. M4, M5, and M7 call the shared C++
  library from their existing WorldState/M6 inputs. A live
  `ThreatAssessment` message is deferred to a separate interface pass if needed.

The first consumer is the 8-probe runner. M4/M5/M7 consumption follows only
after the 8-probe risk baseline is green.

## Dynamic Ship Domain

The model uses two nested, asymmetric super-ellipse domains per target:

- `danger` domain: hard safety boundary. A run fails if the vessel enters this
  domain after the initial stabilization window.
- `warning` domain: early action boundary. A run may enter warning briefly, but
  exposure time and risk trend are scored.

Coordinates use own-ship body axes:

- `x_m`: positive forward from own ship.
- `y_m`: positive starboard from own ship.
- `range_m = hypot(x_m, y_m)`.

For a domain zone `z`, choose quadrant-specific axes:

- `a_forward_z`
- `a_astern_z`
- `b_starboard_z`
- `b_port_z`

Use `a_z = a_forward_z` when `x_m >= 0`, otherwise `a_astern_z`.
Use `b_z = b_starboard_z` when `y_m >= 0`, otherwise `b_port_z`.

The normalized super-ellipse distance is:

```text
norm_z = ((abs(x_m / a_z) ^ p) + (abs(y_m / b_z) ^ p)) ^ (1 / p)
```

Use `p = 2.5` for a rounded rectangle-like domain. The point is outside the
domain when `norm_z > 1`, on the boundary when `norm_z = 1`, and inside when
`norm_z < 1`.

Boundary margin is:

```text
boundary_range_m = range_m / max(norm_z, 1e-6)
domain_margin_m = range_m - boundary_range_m
ddv = max(0, 1 - norm_z)
```

The first implementation uses the local SIL vessel length already used by L4
when dimensions are unavailable:

```text
own_loa_m_default = 46.0
target_loa_m_default = 46.0
effective_loa_m = max(own_loa_m, target_loa_m, 46.0)
```

Danger-domain base axes:

```text
a_forward = max(8.0 * L, own_speed_mps * 60.0 + 2.0 * L, 300.0)
a_astern = max(3.0 * L, 150.0)
b_starboard = max(5.0 * L, own_speed_mps * 30.0 + 1.0 * L, 220.0)
b_port = max(4.0 * L, own_speed_mps * 25.0 + 1.0 * L, 185.0)
```

Warning-domain axes are `1.8 * danger_axes`.

Dynamic modifiers are multiplicative and bounded:

- Closing-speed inflation:
  `1.0 + clamp(closing_speed_mps / 10.0, 0.0, 0.35)`.
- Target-speed inflation:
  `1.0 + clamp(target_speed_mps / 12.0, 0.0, 0.20)`.
- Uncertainty inflation:
  `1.0 + clamp((1.0 - confidence) * 0.30, 0.0, 0.30)`.
- ODD degraded inflation:
  `1.15` in degraded visibility or degraded sensor state, otherwise `1.0`.
- Corridor pressure does not shrink the domain. It raises maneuver-economy cost
  when a candidate response would continue increasing XTE near the 500 m pass
  limit.

## Risk Vector

For every target at every evaluated sample, the risk model emits:

```text
target_id
range_m
relative_bearing_deg
closing_speed_mps
dcpa_m
tcpa_s
warning_margin_m
danger_margin_m
warning_ddv
danger_ddv
tdv_warning_s
tdv_danger_s
tde_warning_s
tde_danger_s
colregs_duty
risk_phase
risk_score
```

Risk phases are ordered:

- `clear`: outside warning domain and no near-term violation.
- `monitor`: outside warning domain, but closing trend or TCPA indicates a
  developing encounter.
- `warning`: warning-domain intrusion or predicted warning-domain violation
  within the action horizon.
- `danger`: danger-domain intrusion or predicted danger-domain violation within
  the emergency horizon.
- `critical`: danger-domain intrusion with worsening trend, collision geometry,
  or Rule17 critical action phase.

Default horizons:

```text
action_horizon_s = 600
emergency_horizon_s = 180
critical_horizon_s = 60
```

Risk score is normalized to `[0, 1]`:

```text
domain_component = max(warning_ddv * 0.7, danger_ddv)
urgency_component = exp(-max(tcpa_s, 0) / 180) when tcpa_s >= 0 else 0
closing_component = clamp(closing_speed_mps / 8.0, 0, 1)
colregs_component = 1.0 for own give-way or both-give-way,
                    0.6 for Rule17 independent/critical action,
                    0.3 for stand-on hold,
                    0.0 for free
uncertainty_component = clamp(1.0 - confidence, 0, 1)

risk_score = clamp(
    0.40 * domain_component +
    0.25 * urgency_component +
    0.15 * closing_component +
    0.15 * colregs_component +
    0.05 * uncertainty_component,
    0, 1)
```

## Multi-Ship Threat Ranking

Threat ranking is deterministic and stable:

1. Highest `risk_phase` wins.
2. Within the same phase, highest `risk_score` wins.
3. If risk score differs by less than `0.12`, keep the previous
   `primary_threat_id` unless the new threat remains higher for two consecutive
   samples.
4. A secondary target that blocks the current escape side raises
   `encounter_complexity_score`, but it does not replace the primary target
   unless it also satisfies the phase/score rule above.

The run-level summary records:

```text
primary_threat_id
primary_threat_switches
max_risk_score
worst_warning_margin_m
worst_danger_margin_m
max_warning_ddv
max_danger_ddv
warning_domain_exposure_s
danger_domain_exposure_s
encounter_complexity_score
risk_trace
```

## 8-Probe Acceptance

Keep existing gates and add dynamic-domain gates.

Existing gates remain:

- minimum DCPA floor
- behavior stability
- final route return when required
- max route XTE under scenario pass limit, currently 500 m for strict probes
- overtake completion where required

New safety gates:

- `danger_domain_exposure_s == 0.0` after a 5 s startup grace window.
- If the scenario starts inside danger domain, it must exit within 90 s and
  `danger_margin_m` must improve monotonically over the first 30 s after the
  action starts.
- `warning_domain_exposure_s <= 120.0` for ordinary crossing/head-on/overtaking
  probes.
- `max_danger_ddv == 0.0` except for explicit close-start emergency probes.
- `risk_score` must decrease after avoidance onset; the first post-onset
  60-second window must show a negative risk-score slope unless the target is
  already opening and outside warning domain.

New seamanship gates:

- `max_route_xte_m < 500.0`.
- `integrated_abs_xte_m_s` must be reported and capped by scenario route length.
  For the current 8-probe, cap at `500 m * 600 s`.
- `route_crossing_overshoot_count <= 1` after release.
- `path_length_ratio <= 1.35` against the nominal route segment distance during
  the run.
- `primary_threat_switches <= 2` unless multiple targets are configured and the
  switch is explained by a higher risk phase.
- Existing steering and speed indecision checks remain authoritative.

The runner must print these fields in each scenario summary and write them into
the JSON batch result.

## M4 Consumption

After the 8-probe risk baseline passes, M4 uses the shared model for
threat-aware arbitration.

M4 must:

- compute the same per-target risk vector through the shared C++ library;
- log the `primary_threat_id`, `risk_phase`, `danger_margin_m`, and
  `risk_score` in the behavior rationale;
- scale COLREG turn demand from the primary threat's domain margin and urgency,
  not from nearest range/nearest CPA alone;
- choose `ReduceSpeed` or combined speed/course action when the risk model shows
  speed reduction improves domain margin with lower XTE and sufficient time;
- keep Rule15 give-way starboard preference unless the risk model enters
  `critical` and Rule2/Rule17 emergency handling is needed;
- keep primary-threat hysteresis to avoid target flapping.

M4 must not:

- use a secondary low-risk target to force a larger turn than the primary threat
  requires;
- drop COLREG give-way duty just because own-ship action temporarily improves CPA;
- use route economy to override danger-domain safety.

## M5 Consumption

After M4 consumes the risk model, M5 uses the same risk vector to score plans.

M5 must:

- include dynamic-domain margin recovery in candidate plan scoring;
- penalize candidate waypoints that increase XTE when the vessel is already near
  the 500 m route-corridor pass limit;
- compare course-only, speed-only, and combined course/speed responses when
  there is enough TCPA/TDV for a speed response to be visible and safe;
- include `primary_threat_id` and worst predicted domain margin in
  `AvoidancePlan.rationale`;
- avoid extending the geometric fallback farther from route after the primary
  threat has moved outside warning domain and range is opening.

The first M5 consumption pass may keep the existing geometric fallback, but it
must use the risk vector to choose target heading magnitude and target speed.

## M7 Consumption

M7 must consume risk semantics without losing checker independence.

M7 may:

- compute the shared risk vector for transparency and ASDR;
- emit alerts when M4/M5 rationale references a different primary threat than
  the risk model.

M7 hard veto must:

- recompute simple independent safety checks from WorldState;
- veto on current or predicted danger-domain violation;
- veto if M4/M5 action keeps increasing risk score while in `danger` or
  `critical` phase;
- preserve a simpler implementation than M4/M5 planning logic.

## Traceability and HMI

Every 8-probe run and control consumer log must expose:

- active `primary_threat_id`;
- current `risk_phase`;
- current `warning_margin_m` and `danger_margin_m`;
- current `risk_score`;
- reason for primary-threat switch;
- whether action is course-only, speed-only, or combined.

HMI changes are limited to existing SIL monitor surfaces if needed for manual
debugging. This spec does not require new UI layout work.

## Testing Strategy

Unit tests:

- C++ dynamic-domain evaluator: quadrant axes, warning/danger margin, DDV, TDV,
  and ranking hysteresis.
- Python dynamic-domain evaluator: same cases as C++.
- C++/Python parity fixture: identical expected phase, primary threat, DDV, and
  risk score tolerance `<= 1e-3` for deterministic fixtures.

Runner tests:

- `tests/scripts/test_run_6_scenarios_gate.py` must assert the new JSON fields
  and hard gates.
- Existing CPA, route-return, corridor, and stability tests remain.

Control tests:

- M4 tests prove risk model can select speed reduction or combined action in a
  long-TCPA Rule15 case without violating Rule16.
- M4 tests prove danger-domain violations still force clear positive action.
- M5 tests prove target speed/heading choice changes with risk vector while
  respecting corridor guard.
- M7 tests prove veto recomputation catches danger-domain violation even if
  M4/M5 risk rationale is missing or stale.

Scenario acceptance:

- All eight `scenarios/COLREGs测试` probes pass after evaluation-only wiring.
- All eight probes pass again after M4/M5/M7 consumption.
- `colreg-rule15-cs.yaml` and `colreg-rule15-cs-2.yaml` must no longer pass with
  excessive mechanical right-loop behavior if a lower-risk speed/course response
  exists.
- `colreg-rule15-cs-edge.yaml` must not overcorrect across the route more than
  once during return.
- `colreg-rule17-cr-so.yaml` must not allow close visual passes to score green
  if danger-domain exposure is present.

## Rollout Gates

Local gate sequence:

1. Targeted unit tests for risk model, runner, M4, M5, and M7.
2. Local clean 8-probe evaluation-only baseline.
3. Local clean 8-probe after M4/M5/M7 risk consumption.
4. Local OrbStack gate:

```bash
source scripts/local-a4000-env.sh
./scripts/local-a4000-acceptance.sh
```

A4000 gate is allowed only after the local gates pass. Sync only touched paths.
Do not use broad `rsync --delete`, `git pull`, `git reset`, or hosted push.

## Open Risks

- The dynamic-domain constants are physically grounded defaults, but they still
  need regression evidence across the existing eight scenarios before promotion.
- A pure shared library plus Python parity tests avoids immediate ROS message
  churn. If later consumers need live cross-module transport, a dedicated
  `ThreatAssessment` message can be added in a separate interface pass.
- Multi-ship scenarios are in scope for the model and ranking design, but the
  current 8-probe acceptance remains the first required evidence set.
