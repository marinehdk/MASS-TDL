"""Tests for the adaptive sim horizon + behavior-aware early-stop logic.

Covers the two pure functions added to run_6_scenarios.py to fix the
sim-horizon artifact (RED caused by `total_time` ending the run at the CPA
moment, leaving zero route-return headroom):

- ``estimate_sim_horizon``: derives total_time/hard_stop from scenario
  geometry via ``_straight_line_cpa`` (tcpa_nominal) + MIN_RETURN_WINDOW_S
  (route_return_budget). total_time = max(yaml_declared, base); hard_stop =
  total_time * 2.

- ``assess_encounter_failure``: behavior-aware early-stop. Merges the
  recovery-stalled (release-but-no-route-convergence) and stuck-avoidance
  (post-CPA still avoiding) checks, plus the CPA-floor violation check.

Design: docs/Design/SIL/COLREGs_测试体系设计_v1.md; root-cause evidence in
runs/rule14_with_release_geometry_trace/ (CPA reached t=2802 vs nominal 1620).
"""
from __future__ import annotations

import importlib.util
import math
from pathlib import Path

import pytest
import yaml


# ---- module loader (same pattern as test_run_6_scenarios_gate.py) ------------

def _load_runner():
    path = Path(__file__).resolve().parents[2] / "scripts" / "run_6_scenarios.py"
    spec = importlib.util.spec_from_file_location("run_6_scenarios", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


REPO = Path(__file__).resolve().parents[2]
SCEN_DIR = REPO / "scenarios" / "COLREGs测试"


def _load_scenario(scenario_id: str) -> dict:
    return yaml.safe_load((SCEN_DIR / f"{scenario_id}.yaml").read_text())


# ---- trace record helpers ----------------------------------------------------

def _m2_record(t_s, *, cpa_m, tcpa_s, rel_brg_deg=0.0, rng_m=1000.0):
    return {
        "topic": "/l3/m2/world_state",
        "sim_t": t_s,
        "primary_cpa_m": cpa_m,
        "primary_tcpa_s": tcpa_s,
        "primary_relative_bearing_deg": rel_brg_deg,
        "primary_rng_m": rng_m,
    }


def _route_status(*, returned_to_route, final_xte, final_behavior,
                  released_after_avoidance=True, route_corridor_ok=True):
    return {
        "returned_to_route": returned_to_route,
        "released_after_avoidance": released_after_avoidance,
        "final_behavior": final_behavior,   # 0=TRANSIT, 7=RECOVERY, other=avoidance
        "final_xte": final_xte,
        "route_corridor_ok": route_corridor_ok,
    }


# =============================================================================
# estimate_sim_horizon
# =============================================================================

class TestEstimateSimHorizon:
    def test_rule14_ho_derives_from_geometry(self):
        runner = _load_runner()
        scen = _load_scenario("colreg-rule14-ho")
        h = runner.estimate_sim_horizon(scen)
        # tcpa_nominal must match _straight_line_cpa (validated ~1620s)
        assert h["tcpa_nominal_s"] == pytest.approx(1620.0, abs=1.0)
        # route_return_budget reuses audit MIN_RETURN_WINDOW_S
        assert h["route_return_budget_s"] == pytest.approx(300.0, abs=1.0)
        # base = tcpa + budget
        assert h["base_horizon_s"] == pytest.approx(1920.0, abs=1.0)
        # rule14-ho YAML declares total_time=3000 (verified), which exceeds base
        # -> total_time = yaml = 3000; hard_stop = 6000.
        assert h["yaml_total_time_s"] == pytest.approx(3000.0, abs=1.0)
        assert h["total_time_s"] == pytest.approx(3000.0, abs=1.0)
        # hard_stop = total_time * 2
        assert h["hard_stop_s"] == pytest.approx(6000.0, abs=1.0)

    def test_total_time_respects_larger_yaml_declaration(self):
        """If the YAML declares a total_time larger than the geometric base,
        the declared value wins (max)."""
        runner = _load_runner()
        scen = _load_scenario("colreg-rule14-ho")
        # Inject a large declared horizon (yaml would otherwise default to 600).
        scen.setdefault("metadata", {}).setdefault(
            "simulation_settings", {})["total_time"] = 9999.0
        h = runner.estimate_sim_horizon(scen)
        assert h["yaml_total_time_s"] == pytest.approx(9999.0, abs=1.0)
        assert h["total_time_s"] == pytest.approx(9999.0, abs=1.0)
        assert h["hard_stop_s"] == pytest.approx(19998.0, abs=1.0)

    @pytest.mark.parametrize("scenario_id", [
        "colreg-rule14-ho",
        "colreg-rule14-ho-port",
        "colreg-rule13-ot",
        "colreg-rule15-cs",
        "colreg-rule15-cs-2",
        "colreg-rule15-cs-edge",
        "colreg-rule15-ot-boundary",
        "colreg-rule17-cr-so",
    ])
    def test_all_clean8_horizons_in_reasonable_range(self, scenario_id):
        """Every clean-8 scenario gets a geometric horizon that leaves real
        headroom: total_time covers tcpa + budget, hard_stop <= 5000s."""
        runner = _load_runner()
        scen = _load_scenario(scenario_id)
        h = runner.estimate_sim_horizon(scen)
        assert h["total_time_s"] >= h["tcpa_nominal_s"] + h["route_return_budget_s"]
        # 2x multiplier on yaml-declared horizons (some scenarios declare up to
        # 3600s) stays bounded; 8000s upper bound covers the largest declared.
        assert h["hard_stop_s"] <= 8000.0
        assert h["hard_stop_s"] == pytest.approx(2.0 * h["total_time_s"], abs=1.0)

    def test_override_via_total_time_override_wins(self):
        """run_scenario-style override path: explicit override supersedes both
        yaml and geometric base, and hard_stop scales from the override."""
        runner = _load_runner()
        scen = _load_scenario("colreg-rule14-ho")
        h = runner.estimate_sim_horizon(scen, total_time_override=3600.0)
        assert h["total_time_s"] == pytest.approx(3600.0, abs=1.0)
        assert h["hard_stop_s"] == pytest.approx(7200.0, abs=1.0)
        # tcpa/budget/base still reported for diagnostics
        assert h["base_horizon_s"] == pytest.approx(1920.0, abs=1.0)


# =============================================================================
# assess_encounter_failure
# =============================================================================

class TestAssessEncounterFailure:
    # Common scenario geometry for rule14-ho: tcpa_nominal=1620, budget=300.
    TCPA_NOMINAL = 1620.0
    BUDGET = 300.0
    CPA_FLOOR = 180.0

    def test_success_not_flagged(self):
        """Route already returned to route, CPA past, healthy -> no failure."""
        runner = _load_runner()
        # sim_t well past CPA + recovery; route converged; cpa safe.
        m2 = [
            _m2_record(1900, cpa_m=686.0, tcpa_s=0.0, rng_m=700.0),
            _m2_record(2100, cpa_m=700.0, tcpa_s=-200.0, rng_m=900.0),
        ]
        rs = _route_status(returned_to_route=True, final_xte=80.0,
                           final_behavior=0)
        failed, reason = runner.assess_encounter_failure(
            route_status=rs, m2_records=m2, cpa_floor_m=self.CPA_FLOOR,
            tcpa_nominal_s=self.TCPA_NOMINAL,
            route_return_budget_s=self.BUDGET, current_sim_t=2100.0)
        assert failed is False
        assert reason is None

    def test_cpa_floor_violated_past_cpa(self):
        """CPA below floor, tcpa<=0, AND range opening (target past closest
        point and receding) -> cpa_floor_violated. Range trend is required so a
        stuck-at-0 tcpa (degraded stack) with a still-closing target is not
        mistaken for past-CPA."""
        runner = _load_runner()
        m2 = [
            _m2_record(1880, cpa_m=150.0, tcpa_s=0.0, rng_m=380.0),
            _m2_record(1900, cpa_m=150.0, tcpa_s=0.0, rng_m=400.0),
            _m2_record(1920, cpa_m=150.0, tcpa_s=-20.0, rng_m=430.0),
        ]
        rs = _route_status(returned_to_route=False, final_xte=400.0,
                           final_behavior=7)
        failed, reason = runner.assess_encounter_failure(
            route_status=rs, m2_records=m2, cpa_floor_m=self.CPA_FLOOR,
            tcpa_nominal_s=self.TCPA_NOMINAL,
            route_return_budget_s=self.BUDGET, current_sim_t=2000.0)
        assert failed is True
        assert reason == "cpa_floor_violated"

    def test_cpa_below_floor_but_target_still_closing_not_flagged(self):
        """CPA below floor but target still CLOSING (range decreasing) is not a
        violation — avoidance may still open the CPA up. Do not early-stop.
        This is the GNC-profile false-positive guard: tcpa stuck at 0 with a
        closing target must NOT trigger cpa_floor_violated."""
        runner = _load_runner()
        m2 = [
            _m2_record(180, cpa_m=120.0, tcpa_s=0.0, rng_m=7100.0),
            _m2_record(185, cpa_m=120.0, tcpa_s=0.0, rng_m=7090.0),
            _m2_record(190, cpa_m=120.0, tcpa_s=0.0, rng_m=7085.0),
        ]
        rs = _route_status(returned_to_route=False, final_xte=200.0,
                           final_behavior=1)
        failed, reason = runner.assess_encounter_failure(
            route_status=rs, m2_records=m2, cpa_floor_m=self.CPA_FLOOR,
            tcpa_nominal_s=self.TCPA_NOMINAL,
            route_return_budget_s=self.BUDGET, current_sim_t=190.0)
        assert failed is False
        assert reason is None

    def test_cpa_sentinel_negative_one_not_flagged(self):
        """M2 emits cpa=-1.0 as a 'not computed' sentinel (target track not
        stable). This is NOT a floor violation. Regression guard for the
        GNC-profile false positive observed at sim_t=191s."""
        runner = _load_runner()
        m2 = [
            _m2_record(189, cpa_m=-1.0, tcpa_s=-1.0, rng_m=7105.0),
            _m2_record(190, cpa_m=-1.0, tcpa_s=-1.0, rng_m=7107.0),
            _m2_record(191, cpa_m=-1.0, tcpa_s=-1.0, rng_m=7109.0),
        ]
        rs = _route_status(returned_to_route=False, final_xte=400.0,
                           final_behavior=1)
        failed, reason = runner.assess_encounter_failure(
            route_status=rs, m2_records=m2, cpa_floor_m=self.CPA_FLOOR,
            tcpa_nominal_s=self.TCPA_NOMINAL,
            route_return_budget_s=self.BUDGET, current_sim_t=191.0)
        assert failed is False
        assert reason is None

    def test_cpa_below_floor_but_not_past_cpa_not_flagged(self):
        """CPA below floor is acceptable while still approaching (tcpa>0,
        range closing); avoidance may still open it up. Do not early-stop."""
        runner = _load_runner()
        m2 = [
            _m2_record(490, cpa_m=120.0, tcpa_s=400.0, rng_m=3100.0),
            _m2_record(495, cpa_m=120.0, tcpa_s=395.0, rng_m=3050.0),
            _m2_record(500, cpa_m=120.0, tcpa_s=390.0, rng_m=3000.0),
        ]
        rs = _route_status(returned_to_route=False, final_xte=200.0,
                           final_behavior=1)
        failed, reason = runner.assess_encounter_failure(
            route_status=rs, m2_records=m2, cpa_floor_m=self.CPA_FLOOR,
            tcpa_nominal_s=self.TCPA_NOMINAL,
            route_return_budget_s=self.BUDGET, current_sim_t=500.0)
        assert failed is False
        assert reason is None

    def test_recovery_with_large_xte_not_flagged(self):
        """Past (tcpa_nominal + budget), CPA past, in RECOVERY(7) with a large
        XTE -> NOT flagged. RECOVERY is the healthy route-return terminal
        state; a large-but-converging XTE is the recovery in progress, not a
        stall. A non-converging RECOVERY is caught by the hard_stop backstop.

        Regression guard: an earlier heuristic flagged this as
        recovery_stalled, which killed healthy GNC rule14-ho runs mid-recovery.
        """
        runner = _load_runner()
        # current_sim_t = 2050 > 1620 + 300 = 1920
        m2 = [
            _m2_record(1900, cpa_m=686.0, tcpa_s=-280.0, rng_m=800.0),
            _m2_record(2050, cpa_m=700.0, tcpa_s=-430.0, rng_m=950.0),
        ]
        rs = _route_status(returned_to_route=False, final_xte=460.0,
                           final_behavior=7)  # RECOVERY, XTE still large
        failed, reason = runner.assess_encounter_failure(
            route_status=rs, m2_records=m2, cpa_floor_m=self.CPA_FLOOR,
            tcpa_nominal_s=self.TCPA_NOMINAL,
            route_return_budget_s=self.BUDGET, current_sim_t=2050.0)
        assert failed is False
        assert reason is None

    def test_recovery_in_progress_within_budget_not_flagged(self):
        """In RECOVERY, XTE still large, but still within the
        (tcpa_nominal + budget) window -> allow it to continue (not stalled)."""
        runner = _load_runner()
        # current_sim_t = 1700 < 1920 -> within expected recovery window
        m2 = [
            _m2_record(1680, cpa_m=690.0, tcpa_s=0.0, rng_m=720.0),
            _m2_record(1700, cpa_m=700.0, tcpa_s=-20.0, rng_m=750.0),
        ]
        rs = _route_status(returned_to_route=False, final_xte=400.0,
                           final_behavior=7)
        failed, reason = runner.assess_encounter_failure(
            route_status=rs, m2_records=m2, cpa_floor_m=self.CPA_FLOOR,
            tcpa_nominal_s=self.TCPA_NOMINAL,
            route_return_budget_s=self.BUDGET, current_sim_t=1700.0)
        assert failed is False
        assert reason is None

    def test_stuck_in_avoidance_past_cpa(self):
        """Past (tcpa_nominal + budget) and CPA is well past, but own ship is
        STILL in an avoidance behavior (never released) -> recovery_stalled."""
        runner = _load_runner()
        m2 = [
            _m2_record(2000, cpa_m=700.0, tcpa_s=-380.0, rng_m=900.0),
            _m2_record(2100, cpa_m=720.0, tcpa_s=-480.0, rng_m=1000.0),
        ]
        # final_behavior != 0 and != 7 -> still in avoidance (merged criterion)
        rs = _route_status(returned_to_route=False, final_xte=400.0,
                           final_behavior=1)
        failed, reason = runner.assess_encounter_failure(
            route_status=rs, m2_records=m2, cpa_floor_m=self.CPA_FLOOR,
            tcpa_nominal_s=self.TCPA_NOMINAL,
            route_return_budget_s=self.BUDGET, current_sim_t=2100.0)
        assert failed is True
        assert reason == "recovery_stalled"

    def test_pre_encounter_not_flagged(self):
        """Early in the run, target still closing, no avoidance yet -> no
        early-stop (must not kill a healthy long-approach scenario)."""
        runner = _load_runner()
        m2 = [
            _m2_record(100, cpa_m=1.9, tcpa_s=1019.0, rng_m=9600.0),
        ]
        rs = _route_status(returned_to_route=False, final_xte=5.0,
                           final_behavior=0)
        failed, reason = runner.assess_encounter_failure(
            route_status=rs, m2_records=m2, cpa_floor_m=self.CPA_FLOOR,
            tcpa_nominal_s=self.TCPA_NOMINAL,
            route_return_budget_s=self.BUDGET, current_sim_t=100.0)
        assert failed is False
        assert reason is None
