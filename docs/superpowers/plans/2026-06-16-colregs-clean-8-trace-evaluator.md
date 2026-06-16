# COLREGs Clean 8 Trace Evaluator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the 7-layer TraceEvaluator for the current clean 8-probe, using YAML as truth, length-scaled CPA thresholds (`4L`, `6L`, `9L`, `20L`), post-pass threat separation, COLREG timing/magnitude gates, route-return gates, and a correctly named `run_colregs_clean_8probe.py` entrypoint.

**Architecture:** Keep L3 behavior code unchanged in this plan. Add host-side evaluation under `tools/sil`, wire it into the clean 8 runner, and migrate scenario metadata so evaluator reports explain why each probe passed or failed. M1-M8 runtime modules remain the system under test; TraceEvaluator is the independent verification layer.

**Tech Stack:** Python 3.11 host scripts, PyYAML scenario metadata, pytest unit tests, existing SIL orchestrator API at `SIL_ORCH_BASE_URL`, existing `runs/trace_current.jsonl` trace format.

---

## File Structure

- Create: `tools/sil/colregs_trace_evaluator.py`
  - Responsibility: pure-Python evaluator, profile thresholds, no-action baseline, trace enrichment, 7-layer verdict, JSON report schema.
- Create: `tests/tools/sil/test_colregs_trace_evaluator.py`
  - Responsibility: focused tests for thresholds, trace phases, post-pass handling, Rule13 exception, Rule17 dynamics, and report composition.
- Modify: `scripts/run_6_scenarios.py`
  - Responsibility: keep current run behavior, call TraceEvaluator after each scenario, include evaluator verdict in per-scenario result and batch summary.
- Add: `scripts/run_colregs_clean_8probe.py`
  - Responsibility: canonical clean 8-probe CLI. It imports and delegates to the existing runner implementation after the evaluator is wired.
- Modify: `tests/scripts/test_run_6_scenarios_gate.py`
  - Responsibility: update clean 8 YAML expectations, prove renamed runner and compatibility wrapper behavior.
- Modify: `scenarios/COLREGs测试/{colreg-rule14-ho,colreg-rule14-ho-port,colreg-rule13-ot,colreg-rule15-cs,colreg-rule15-cs-2,colreg-rule15-cs-edge,colreg-rule15-ot-boundary,colreg-rule17-cr-so}.yaml`
  - Responsibility: make `metadata.expected_outcome.cpa_acceptance` the YAML truth for profile, formula, threshold, LOA multiplier, confidence, and basis.
- Modify: `scenarios/COLREGs测试/README.md`
  - Responsibility: rename runbook from 6-scenario wording to clean 8-probe wording and document 7-layer verdict fields.
- Update: `docs/Design/Review/2026-06-16/COLREGs_8Probe_TraceEvaluator_Spec_v0.2.md`
  - Responsibility: keep Spec aligned with implementation evidence if tests force wording changes.

## Task 1: Length-Scaled CPA Profiles

- [ ] Add failing tests in `tests/tools/sil/test_colregs_trace_evaluator.py`:

```python
import pytest

from tools.sil.colregs_trace_evaluator import (
    CpaProfile,
    derive_cpa_threshold,
)


@pytest.mark.parametrize(
    ("profile", "expected_formula", "expected_m", "expected_multiplier"),
    [
        ("corridor_close_start_4L", "4.0L", 180.0, 4.0),
        ("standon_in_extremis_4L", "4.0L", 180.0, 4.0),
        ("corridor_boundary_6L", "6.0L", 270.0, 6.0),
        ("corridor_follow_or_overtake_6L", "6.0L", 270.0, 6.0),
        ("ideal_corridor_domain_9L", "9.0L", 405.0, 9.0),
        ("open_water_crossing_20L", "20.0L", 900.0, 20.0),
    ],
)
def test_cpa_profiles_are_loa_scaled(profile, expected_formula, expected_m, expected_multiplier):
    derived = derive_cpa_threshold(CpaProfile(profile), loa_m=45.0)

    assert derived.threshold_formula == expected_formula
    assert derived.threshold_m == pytest.approx(expected_m)
    assert derived.loa_multiplier == pytest.approx(expected_multiplier)
```

- [ ] Implement `CpaProfile`, `CpaThreshold`, and `derive_cpa_threshold()` in `tools/sil/colregs_trace_evaluator.py`.
- [ ] Reject nautical-mile legacy formulas in the new evaluator:

```python
def test_cpa_profile_rejects_legacy_nautical_mile_formula():
    with pytest.raises(ValueError, match="unsupported CPA profile"):
        derive_cpa_threshold(CpaProfile("open_water_warning_0p5nm"), loa_m=45.0)
```

- [ ] Run:

```bash
python3 -m pytest tests/tools/sil/test_colregs_trace_evaluator.py -q
```

Expected result: new threshold tests pass, no runner tests touched yet.

## Task 2: Clean 8 YAML Migration

- [ ] Update the 8 clean YAML files with this exact mapping:

| Scenario | Profile | Formula | Threshold | Multiplier | Confidence |
|---|---|---:|---:|---:|---|
| `colreg-rule14-ho` | `corridor_close_start_4L` | `4.0L` | `180.0` | `4.0` | `project_profile_medium` |
| `colreg-rule14-ho-port` | `corridor_close_start_4L` | `4.0L` | `180.0` | `4.0` | `project_profile_medium` |
| `colreg-rule13-ot` | `corridor_follow_or_overtake_6L` | `6.0L` | `270.0` | `6.0` | `project_profile_medium_low` |
| `colreg-rule15-cs` | `open_water_crossing_20L` | `20.0L` | `900.0` | `20.0` | `project_profile_medium_high` |
| `colreg-rule15-cs-2` | `open_water_crossing_20L` | `20.0L` | `900.0` | `20.0` | `project_profile_medium_high` |
| `colreg-rule15-cs-edge` | `corridor_boundary_6L` | `6.0L` | `270.0` | `6.0` | `project_profile_medium_low` |
| `colreg-rule15-ot-boundary` | `corridor_boundary_6L` | `6.0L` | `270.0` | `6.0` | `project_profile_medium_low` |
| `colreg-rule17-cr-so` | `standon_in_extremis_4L` | `4.0L` | `180.0` | `4.0` | `project_profile_medium` |

- [ ] Preserve `ideal_domain_m: 405.0` for corridor quality reporting and set `emergency_floor_m: 180.0` where present.
- [ ] Update `cpa_min_m_ge` to match `cpa_acceptance.threshold_m` in all 8 files.
- [ ] Replace legacy basis text containing `0.1NM`, `0.5NM`, `300m`, `926m`, or `185.2m` with length-scaled wording.
- [ ] Update `tests/scripts/test_run_6_scenarios_gate.py`:

```python
def test_clean_probe_yaml_declares_length_scaled_cpa_acceptance_profile():
    runner = _load_runner()
    root = Path(__file__).resolve().parents[2]
    expected_profiles = {
        "colreg-rule14-ho": ("corridor_close_start_4L", 180.0, "4.0L", 4.0),
        "colreg-rule14-ho-port": ("corridor_close_start_4L", 180.0, "4.0L", 4.0),
        "colreg-rule13-ot": ("corridor_follow_or_overtake_6L", 270.0, "6.0L", 6.0),
        "colreg-rule15-cs": ("open_water_crossing_20L", 900.0, "20.0L", 20.0),
        "colreg-rule15-cs-2": ("open_water_crossing_20L", 900.0, "20.0L", 20.0),
        "colreg-rule15-cs-edge": ("corridor_boundary_6L", 270.0, "6.0L", 6.0),
        "colreg-rule15-ot-boundary": ("corridor_boundary_6L", 270.0, "6.0L", 6.0),
        "colreg-rule17-cr-so": ("standon_in_extremis_4L", 180.0, "4.0L", 4.0),
    }
    for scenario_id in runner.SCENARIOS:
        path = root / "scenarios" / "COLREGs测试" / f"{scenario_id}.yaml"
        expected = yaml.safe_load(path.read_text())["metadata"]["expected_outcome"]
        profile, threshold_m, formula, multiplier = expected_profiles[scenario_id]
        cpa = expected["cpa_acceptance"]

        assert expected["cpa_min_m_ge"] == pytest.approx(threshold_m), scenario_id
        assert cpa["profile"] == profile, scenario_id
        assert cpa["threshold_formula"] == formula, scenario_id
        assert cpa["threshold_m"] == pytest.approx(threshold_m), scenario_id
        assert cpa["loa_m"] == pytest.approx(45.0), scenario_id
        assert cpa["loa_multiplier"] == pytest.approx(multiplier), scenario_id
        assert "NM" not in cpa["basis"], scenario_id
```

- [ ] Run:

```bash
python3 -m pytest tests/scripts/test_run_6_scenarios_gate.py -q
```

Expected result: YAML profile tests pass with the new `4L/6L/9L/20L` values.

## Task 3: Threshold Provenance In Runner

- [ ] Update `scripts/run_6_scenarios.py` so `expected_cpa_floor_m()` validates both YAML truth and derived profile truth:

```python
def expected_cpa_floor_m(expected):
    cpa = expected.get("cpa_acceptance") or {}
    profile = cpa.get("profile")
    if profile:
        derived = derive_cpa_threshold(CpaProfile(profile), loa_m=float(cpa.get("loa_m", 45.0)))
        if abs(float(cpa["threshold_m"]) - derived.threshold_m) > 1.0:
            raise ValueError("cpa_acceptance.threshold_m must match length-scaled profile")
        if cpa.get("threshold_formula") != derived.threshold_formula:
            raise ValueError("cpa_acceptance.threshold_formula must match length-scaled profile")
        legacy_floor = expected.get("cpa_min_m_ge")
        if legacy_floor is not None and abs(float(legacy_floor) - derived.threshold_m) > 1.0:
            raise ValueError("cpa_min_m_ge must match cpa_acceptance threshold")
        return derived.threshold_m
    if expected.get("cpa_min_m_ge") is not None:
        return float(expected["cpa_min_m_ge"])
    return DEFAULT_CPA_FLOOR_M
```

- [ ] Add test coverage for formula mismatch and threshold mismatch.
- [ ] Run:

```bash
python3 -m pytest tests/scripts/test_run_6_scenarios_gate.py tests/tools/sil/test_colregs_trace_evaluator.py -q
```

Expected result: both test files pass.

## Task 4: Trace Phase Model And Post-Pass Separation

- [ ] Add `TraceSample`, `EncounterPhase`, and `classify_encounter_phase()` in `tools/sil/colregs_trace_evaluator.py`.
- [ ] Define active approach risk only when one of these is true:
  - `tcpa_s >= 0`
  - `closing_speed_mps > 0`
  - Rule13 not yet `past_and_clear`
- [ ] Define `post_pass_clearance` when target is abaft, `tcpa_s < 0`, and `closing_speed_mps <= 0`.
- [ ] Add tests:

```python
def test_head_on_target_astern_with_negative_tcpa_is_post_pass_not_approach_threat():
    sample = TraceSample(
        t_s=120.0,
        range_m=120.0,
        cpa_m=120.0,
        tcpa_s=-18.0,
        closing_speed_mps=-1.5,
        rel_bearing_deg=185.0,
        colreg_rule="Rule14",
        own_duty="give_way",
        past_and_clear=False,
    )

    assert classify_encounter_phase(sample) == EncounterPhase.POST_PASS_CLEARANCE


def test_rule13_does_not_release_until_past_and_clear():
    sample = TraceSample(
        t_s=300.0,
        range_m=160.0,
        cpa_m=160.0,
        tcpa_s=-5.0,
        closing_speed_mps=-0.2,
        rel_bearing_deg=170.0,
        colreg_rule="Rule13",
        own_duty="give_way",
        past_and_clear=False,
    )

    assert classify_encounter_phase(sample) == EncounterPhase.APPROACH_RISK
```

- [ ] Split exposure counters:
  - `approach_warning_exposure_s`
  - `approach_danger_exposure_s`
  - `post_pass_warning_exposure_s`
  - `post_pass_danger_exposure_s`
- [ ] Run:

```bash
python3 -m pytest tests/tools/sil/test_colregs_trace_evaluator.py -q
```

Expected result: post-pass head-on close distance no longer counts as active collision threat, while Rule13 remains latched until past-and-clear.

## Task 5: Kinematic No-Action Baseline

- [ ] Implement `compute_no_action_baseline(scenario_yaml)` from YAML initial ownship and target states using straight-line kinematics.
- [ ] Report:
  - `no_action_dcpa_m`
  - `no_action_tcpa_s`
  - `scenario_conflict_valid`
  - expected rule and expected duty from YAML
- [ ] Add tests with synthetic head-on and no-conflict cases:

```python
def test_no_action_baseline_detects_head_on_conflict():
    baseline = compute_no_action_baseline(make_head_on_yaml())

    assert baseline.scenario_conflict_valid is True
    assert baseline.no_action_tcpa_s > 0.0
    assert baseline.no_action_dcpa_m < 180.0


def test_no_action_baseline_rejects_diverging_geometry():
    baseline = compute_no_action_baseline(make_diverging_yaml())

    assert baseline.scenario_conflict_valid is False
    assert baseline.no_action_tcpa_s < 0.0
```

- [ ] Wire Layer1 `scenario_validity` to `UNKNOWN` only when required YAML fields are missing; otherwise emit PASS/FAIL from deterministic baseline.
- [ ] Run:

```bash
python3 -m pytest tests/tools/sil/test_colregs_trace_evaluator.py -q
```

Expected result: Layer1 can prove each clean probe is a real conflict without disabling runtime avoidance.

## Task 6: COLREG Compliance Gates

- [ ] Add Rule8 action gate:
  - full give-way action pass requires heading delta `>= 30 deg`
  - restricted partial can report `partial_pass` at `>= 15 deg`, but cannot set full `colregs_pass`
- [ ] Add Rule15/R16 crossing gate:
  - give-way vessel must not cross ahead of stand-on target during approach phase
  - action onset must occur before CPA window becomes late
- [ ] Add Rule17 dynamic latest maneuver point:

```python
def test_rule17_latest_action_uses_dynamic_maneuver_time():
    config = ManeuverTimingConfig(
        required_heading_change_deg=30.0,
        max_effective_rot_deg_s=3.0,
        system_delay_s=5.0,
        actuator_delay_s=2.0,
        hydrodynamic_response_s=8.0,
        safety_margin_s=10.0,
    )

    assert compute_t_last_maneuver_s(config) == pytest.approx(35.0)
```

- [ ] Rule17 report must contain every value used in `T_last_maneuver_s`.
- [ ] Run:

```bash
python3 -m pytest tests/tools/sil/test_colregs_trace_evaluator.py -q
```

Expected result: COLREG gates separate role compliance from geometric CPA pass.

## Task 7: Seven-Layer Report Composition

- [ ] Implement `evaluate_trace()` returning `TraceEvaluationReport` with:
  - `L1_scenario_validity`
  - `L2_safety_floor`
  - `L3_dynamic_risk`
  - `L4_colregs_compliance`
  - `L5_route_recovery`
  - `L6_seamanship`
  - `L7_stability`
  - `first_failure`
  - `threshold_provenance`
- [ ] Compose four top-level gates:

```python
overall_pass = (
    report.verdict.safety_pass
    and report.verdict.mission_pass
    and report.verdict.colregs_pass
    and report.verdict.stability_pass
)
```

- [ ] Add serialization test:

```python
def test_trace_evaluation_report_json_schema_contains_required_layers():
    report = make_minimal_passing_report()
    data = report.to_json_dict()

    assert data["threshold_provenance"]["threshold_formula"] == "4.0L"
    assert data["layers"]["L1_scenario_validity"]["status"] == "PASS"
    assert data["verdict"]["overall_pass"] is True
    assert data["first_failure"] is None
```

- [ ] Run:

```bash
python3 -m pytest tests/tools/sil/test_colregs_trace_evaluator.py -q
```

Expected result: one report object can explain all seven layers without reading console text.

## Task 8: Runner Rename And Integration

- [ ] Add `scripts/run_colregs_clean_8probe.py` as canonical entrypoint.
- [ ] Keep `scripts/run_6_scenarios.py` as compatibility wrapper for one transition period; print a warning to stderr:

```text
run_6_scenarios.py is deprecated; use scripts/run_colregs_clean_8probe.py for clean 8-probe.
```

- [ ] Add CLI support to canonical runner:
  - `--scenario SCENARIO_ID`
  - `--list`
  - `--summary-out PATH`
  - `--trace-report-dir PATH`
  - `--restart-between-runs`
- [ ] Runner writes one per-scenario evaluator report:

```text
runs/trace_eval/<timestamp>/<scenario_id>.json
```

- [ ] Batch summary includes:
  - legacy metrics used today
  - `trace_evaluation_report_path`
  - `safety_pass`
  - `mission_pass`
  - `colregs_pass`
  - `stability_pass`
  - `overall_pass`
- [ ] Add tests:

```python
def test_clean_8_runner_name_lists_eight_scenarios():
    result = subprocess.run(
        [sys.executable, "scripts/run_colregs_clean_8probe.py", "--list"],
        cwd=Path(__file__).resolve().parents[2],
        text=True,
        capture_output=True,
        check=True,
    )

    scenarios = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    assert scenarios == [
        "colreg-rule14-ho",
        "colreg-rule14-ho-port",
        "colreg-rule13-ot",
        "colreg-rule15-cs",
        "colreg-rule15-cs-2",
        "colreg-rule15-cs-edge",
        "colreg-rule15-ot-boundary",
        "colreg-rule17-cr-so",
    ]
```

- [ ] Run:

```bash
python3 -m pytest tests/scripts/test_run_6_scenarios_gate.py tests/tools/sil/test_colregs_trace_evaluator.py -q
```

Expected result: old import tests still work, new canonical CLI lists exactly clean 8.

## Task 9: Documentation And Local Evidence

- [ ] Update `scenarios/COLREGs测试/README.md` run command:

```bash
SIL_ORCH_BASE_URL=https://127.0.0.1:18000/api/v1 \
python3 scripts/run_colregs_clean_8probe.py --restart-between-runs \
  --summary-out runs/local_clean8_traceeval_$(date +%Y%m%d_%H%M%S).json \
  --trace-report-dir runs/trace_eval/$(date +%Y%m%d_%H%M%S)
```

- [ ] Update README summary formula:

```text
overall_pass = safety_pass AND mission_pass AND colregs_pass AND stability_pass
```

- [ ] Run local unit tests:

```bash
python3 -m pytest tests/tools/sil/test_colregs_trace_evaluator.py tests/scripts/test_run_6_scenarios_gate.py -q
```

Expected result: all selected tests pass.

- [ ] Run clean 8 when local SIL stack is already owned by this worktree:

```bash
SIL_ORCH_BASE_URL=https://127.0.0.1:18000/api/v1 \
python3 scripts/run_colregs_clean_8probe.py --restart-between-runs \
  --summary-out runs/local_clean8_traceeval_$(date +%Y%m%d_%H%M%S).json \
  --trace-report-dir runs/trace_eval/$(date +%Y%m%d_%H%M%S)
```

Expected result: batch summary exists; any scenario failure names first failing layer instead of only `overall_pass=false`.

## Task 10: Final Review Guard

- [ ] Search for legacy clean 8 threshold constants in edited evaluator path:

```bash
rg -n "185\\.2|300\\.0|926\\.0|0\\.1NM|0\\.5NM|open_water_warning_0p5nm|corridor_close_start_0p1nm" \
  tools/sil scripts tests/scripts scenarios/COLREGs测试 docs/Design/Review/2026-06-16/COLREGs_8Probe_TraceEvaluator_Spec_v0.2.md
```

Expected result: no matches in active clean 8 evaluator/YAML/spec paths.

- [ ] Inspect changed paths:

```bash
git status --short
git diff -- docs/Design/Review/2026-06-16/COLREGs_8Probe_TraceEvaluator_Spec_v0.2.md
git diff -- tools/sil tests/tools/sil scripts tests/scripts scenarios/COLREGs测试
```

Expected result: diff is limited to TraceEvaluator, runner naming, clean 8 YAML metadata, tests, and docs.

- [ ] Commit only after tests and local clean 8 evidence are captured:

```bash
git add \
  docs/Design/Review/2026-06-16/COLREGs_8Probe_TraceEvaluator_Spec_v0.2.md \
  docs/superpowers/plans/2026-06-16-colregs-clean-8-trace-evaluator.md \
  tools/sil/colregs_trace_evaluator.py \
  tests/tools/sil/test_colregs_trace_evaluator.py \
  scripts/run_colregs_clean_8probe.py \
  scripts/run_6_scenarios.py \
  tests/scripts/test_run_6_scenarios_gate.py \
  scenarios/COLREGs测试/README.md \
  scenarios/COLREGs测试/colreg-rule14-ho.yaml \
  scenarios/COLREGs测试/colreg-rule14-ho-port.yaml \
  scenarios/COLREGs测试/colreg-rule13-ot.yaml \
  scenarios/COLREGs测试/colreg-rule15-cs.yaml \
  scenarios/COLREGs测试/colreg-rule15-cs-2.yaml \
  scenarios/COLREGs测试/colreg-rule15-cs-edge.yaml \
  scenarios/COLREGs测试/colreg-rule15-ot-boundary.yaml \
  scenarios/COLREGs测试/colreg-rule17-cr-so.yaml
git commit -m "Add COLREGs clean 8 trace evaluator plan"
```

Expected result: commit includes no generated `runs/`, no `.preflight/`, no unrelated compose/runtime dirty files.
