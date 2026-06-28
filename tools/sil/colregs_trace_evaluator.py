"""Host-side COLREGs trace evaluator helpers."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from math import cos, hypot, radians, sin
from typing import Any


class CpaProfile(str):
    """CPA acceptance profile name."""


@dataclass(frozen=True)
class CpaThreshold:
    threshold_formula: str
    threshold_m: float
    loa_multiplier: float


_CPA_PROFILE_MULTIPLIERS: dict[CpaProfile, float] = {
    CpaProfile("corridor_close_start_4L"): 4.0,
    CpaProfile("standon_in_extremis_4L"): 4.0,
    CpaProfile("corridor_follow_or_overtake_4L"): 4.0,
    CpaProfile("corridor_boundary_6L"): 6.0,
    CpaProfile("corridor_follow_or_overtake_6L"): 6.0,
    CpaProfile("ideal_corridor_domain_9L"): 9.0,
    CpaProfile("open_water_crossing_20L"): 20.0,
}


def derive_cpa_threshold(profile: CpaProfile, loa_m: float) -> CpaThreshold:
    try:
        multiplier = _CPA_PROFILE_MULTIPLIERS[profile]
    except KeyError as exc:
        raise ValueError(f"unsupported CPA profile: {profile}") from exc

    return CpaThreshold(
        threshold_formula=f"{multiplier:.1f}L",
        threshold_m=multiplier * loa_m,
        loa_multiplier=multiplier,
    )


class EncounterPhase(str, Enum):
    APPROACH_RISK = "approach_risk"
    POST_PASS_CLEARANCE = "post_pass_clearance"
    FREE = "free"


@dataclass(frozen=True)
class TraceSample:
    t_s: float
    range_m: float
    cpa_m: float
    tcpa_s: float
    closing_speed_mps: float
    rel_bearing_deg: float
    colreg_rule: str
    own_duty: str
    past_and_clear: bool = False
    warning: bool = False
    danger: bool = False


@dataclass(frozen=True)
class ExposureMetrics:
    approach_warning_exposure_s: float = 0.0
    approach_danger_exposure_s: float = 0.0
    post_pass_warning_exposure_s: float = 0.0
    post_pass_danger_exposure_s: float = 0.0


def _relative_bearing_180(deg: float) -> float:
    return (float(deg) + 180.0) % 360.0 - 180.0


def _is_abaft(rel_bearing_deg: float) -> bool:
    return abs(_relative_bearing_180(rel_bearing_deg)) > 90.0


def classify_encounter_phase(sample: TraceSample) -> EncounterPhase:
    rule = sample.colreg_rule.lower()
    duty = sample.own_duty.lower()
    if rule == "rule13" and duty == "give_way" and not sample.past_and_clear:
        return EncounterPhase.APPROACH_RISK
    if sample.tcpa_s >= 0.0 or sample.closing_speed_mps > 0.0:
        return EncounterPhase.APPROACH_RISK
    if sample.tcpa_s < 0.0 and sample.closing_speed_mps <= 0.0 and _is_abaft(sample.rel_bearing_deg):
        return EncounterPhase.POST_PASS_CLEARANCE
    return EncounterPhase.FREE


def compute_exposure_metrics(samples: list[TraceSample]) -> ExposureMetrics:
    if len(samples) < 2:
        return ExposureMetrics()
    approach_warning = 0.0
    approach_danger = 0.0
    post_warning = 0.0
    post_danger = 0.0
    ordered = sorted(samples, key=lambda sample: sample.t_s)
    for prev, cur in zip(ordered, ordered[1:]):
        dt_s = max(0.0, cur.t_s - prev.t_s)
        phase = classify_encounter_phase(prev)
        if phase == EncounterPhase.APPROACH_RISK:
            approach_warning += dt_s if prev.warning else 0.0
            approach_danger += dt_s if prev.danger else 0.0
        elif phase == EncounterPhase.POST_PASS_CLEARANCE:
            post_warning += dt_s if prev.warning else 0.0
            post_danger += dt_s if prev.danger else 0.0
    return ExposureMetrics(
        approach_warning_exposure_s=approach_warning,
        approach_danger_exposure_s=approach_danger,
        post_pass_warning_exposure_s=post_warning,
        post_pass_danger_exposure_s=post_danger,
    )


@dataclass(frozen=True)
class NoActionBaseline:
    no_action_dcpa_m: float
    no_action_tcpa_s: float
    scenario_conflict_valid: bool
    expected_rule: str
    expected_duty: str


def _latlon_to_xy_m(lat: float, lon: float, lat0: float, lon0: float) -> tuple[float, float]:
    return (
        (float(lon) - float(lon0)) * 111120.0 * cos(radians(float(lat0))),
        (float(lat) - float(lat0)) * 111120.0,
    )


def _nav_velocity_mps(heading_deg: float, sog_kn: float) -> tuple[float, float]:
    speed = float(sog_kn) * 0.514444
    return speed * sin(radians(float(heading_deg))), speed * cos(radians(float(heading_deg)))


def _cpa_tcpa_m(rel_x: float, rel_y: float, rel_vx: float, rel_vy: float) -> tuple[float, float]:
    rel_speed_sq = rel_vx * rel_vx + rel_vy * rel_vy
    if rel_speed_sq <= 1.0e-9:
        return hypot(rel_x, rel_y), -1.0
    tcpa = -((rel_x * rel_vx + rel_y * rel_vy) / rel_speed_sq)
    cpa_t = max(tcpa, 0.0)
    return hypot(rel_x + rel_vx * cpa_t, rel_y + rel_vy * cpa_t), tcpa


def compute_no_action_baseline(scenario_yaml: dict[str, Any]) -> NoActionBaseline:
    metadata = scenario_yaml.get("metadata") or {}
    sim_settings = metadata.get("simulation_settings") or {}
    origin = sim_settings.get("coordinate_origin") or [0.0, 0.0]
    lat0, lon0 = float(origin[0]), float(origin[1])
    encounter = metadata.get("encounter") or {}
    own_initial = scenario_yaml["ownShip"]["initial"]
    own_pos = own_initial["position"]
    own_x, own_y = _latlon_to_xy_m(own_pos["latitude"], own_pos["longitude"], lat0, lon0)
    own_vx, own_vy = _nav_velocity_mps(own_initial["heading"], own_initial["sog"])

    targets = scenario_yaml.get("targetShips") or []
    if not targets:
        return NoActionBaseline(float("inf"), -1.0, False, str(encounter.get("rule", "")), "")

    target_initial = targets[0]["initial"]
    target_pos = target_initial["position"]
    target_x, target_y = _latlon_to_xy_m(target_pos["latitude"], target_pos["longitude"], lat0, lon0)
    target_vx, target_vy = _nav_velocity_mps(target_initial["cog"], target_initial["sog"])
    dcpa_m, tcpa_s = _cpa_tcpa_m(
        target_x - own_x,
        target_y - own_y,
        target_vx - own_vx,
        target_vy - own_vy,
    )
    expected = metadata.get("expected_outcome") or {}
    cpa_floor = float(expected.get("cpa_min_m_ge", 180.0))
    return NoActionBaseline(
        no_action_dcpa_m=dcpa_m,
        no_action_tcpa_s=tcpa_s,
        scenario_conflict_valid=(tcpa_s >= 0.0 and dcpa_m < cpa_floor),
        expected_rule=str(encounter.get("rule", "")),
        expected_duty=str(encounter.get("give_way_vessel", "")),
    )


@dataclass(frozen=True)
class Rule8ActionVerdict:
    full_pass: bool
    partial_pass: bool
    heading_delta_deg: float


def evaluate_rule8_action(
    heading_delta_deg: float,
    *,
    full_turn_deg: float = 30.0,
    restricted_partial_turn_deg: float = 15.0,
) -> Rule8ActionVerdict:
    delta = abs(float(heading_delta_deg))
    return Rule8ActionVerdict(
        full_pass=delta >= full_turn_deg,
        partial_pass=delta >= restricted_partial_turn_deg,
        heading_delta_deg=delta,
    )


@dataclass(frozen=True)
class ManeuverTimingConfig:
    required_heading_change_deg: float
    max_effective_rot_deg_s: float
    system_delay_s: float
    actuator_delay_s: float
    hydrodynamic_response_s: float
    safety_margin_s: float


def compute_t_last_maneuver_s(config: ManeuverTimingConfig) -> float:
    if config.max_effective_rot_deg_s <= 0.0:
        raise ValueError("max_effective_rot_deg_s must be positive")
    return (
        config.system_delay_s
        + config.actuator_delay_s
        + config.required_heading_change_deg / config.max_effective_rot_deg_s
        + config.hydrodynamic_response_s
        + config.safety_margin_s
    )


@dataclass(frozen=True)
class Rule17Verdict:
    latest_action_pass: bool
    t_last_maneuver_s: float
    timing_inputs: dict[str, float]


def evaluate_rule17_latest_action(action_onset_tcpa_s: float, config: ManeuverTimingConfig) -> Rule17Verdict:
    t_last = compute_t_last_maneuver_s(config)
    return Rule17Verdict(
        latest_action_pass=float(action_onset_tcpa_s) >= t_last,
        t_last_maneuver_s=t_last,
        timing_inputs={
            "required_heading_change_deg": config.required_heading_change_deg,
            "max_effective_rot_deg_s": config.max_effective_rot_deg_s,
            "system_delay_s": config.system_delay_s,
            "actuator_delay_s": config.actuator_delay_s,
            "hydrodynamic_response_s": config.hydrodynamic_response_s,
            "safety_margin_s": config.safety_margin_s,
        },
    )


@dataclass(frozen=True)
class Rule15CrossingVerdict:
    pass_: bool
    crossed_ahead: bool


def evaluate_rule15_crossing(*, crossed_ahead: bool) -> Rule15CrossingVerdict:
    return Rule15CrossingVerdict(pass_=not bool(crossed_ahead), crossed_ahead=bool(crossed_ahead))


@dataclass(frozen=True)
class TraceEvaluationVerdict:
    safety_pass: bool
    mission_pass: bool
    colregs_pass: bool
    stability_pass: bool

    @property
    def overall_pass(self) -> bool:
        return self.safety_pass and self.mission_pass and self.colregs_pass and self.stability_pass

    def to_json_dict(self) -> dict[str, bool]:
        return {
            "safety_pass": self.safety_pass,
            "mission_pass": self.mission_pass,
            "colregs_pass": self.colregs_pass,
            "stability_pass": self.stability_pass,
            "overall_pass": self.overall_pass,
        }


@dataclass(frozen=True)
class TraceEvaluationReport:
    scenario_id: str
    clean_8probe: bool
    verdict: TraceEvaluationVerdict
    threshold_provenance: dict[str, Any]
    layers: dict[str, dict[str, Any]]
    first_failure: str | None
    trace_artifact_path: str | None = None
    no_action_trace_path: str | None = None
    chain_summary: dict[str, Any] | None = None

    def to_json_dict(self) -> dict[str, Any]:
        return {
            "scenario_id": self.scenario_id,
            "clean_8probe": self.clean_8probe,
            "verdict": self.verdict.to_json_dict(),
            "threshold_provenance": self.threshold_provenance,
            "layers": self.layers,
            "first_failure": self.first_failure,
            "trace_artifact_path": self.trace_artifact_path,
            "no_action_trace_path": self.no_action_trace_path,
            "chain_summary": self.chain_summary,
        }


def first_failed_layer(layers: dict[str, dict[str, Any]]) -> str | None:
    for name, layer in layers.items():
        if layer.get("status") == "FAIL":
            return name
    return None


def make_threshold_provenance(cpa_acceptance: dict[str, Any]) -> dict[str, Any]:
    profile = CpaProfile(str(cpa_acceptance["profile"]))
    threshold = derive_cpa_threshold(profile, loa_m=float(cpa_acceptance.get("loa_m", 45.0)))
    return {
        "profile": str(profile),
        "threshold_formula": threshold.threshold_formula,
        "threshold_m": threshold.threshold_m,
        "loa_m": float(cpa_acceptance.get("loa_m", 45.0)),
        "loa_multiplier": threshold.loa_multiplier,
        "source_confidence": cpa_acceptance.get("source_confidence", "unknown"),
    }


def make_minimal_passing_report() -> TraceEvaluationReport:
    layers = {
        "L1_scenario_validity": {"status": "PASS"},
        "L2_safety_floor": {"status": "PASS"},
        "L3_dynamic_risk": {"status": "PASS"},
        "L4_colregs_compliance": {"status": "PASS"},
        "L5_route_recovery": {"status": "PASS"},
        "L6_seamanship": {"status": "PASS"},
        "L7_stability": {"status": "PASS"},
    }
    return TraceEvaluationReport(
        scenario_id="colreg-rule14-ho",
        clean_8probe=True,
        verdict=TraceEvaluationVerdict(True, True, True, True),
        threshold_provenance={
            "profile": "corridor_close_start_4L",
            "threshold_formula": "4.0L",
            "threshold_m": 180.0,
            "loa_m": 45.0,
            "loa_multiplier": 4.0,
            "source_confidence": "project_profile_medium",
        },
        layers=layers,
        first_failure=first_failed_layer(layers),
    )


def evaluate_trace(
    *,
    scenario_id: str,
    cpa_acceptance: dict[str, Any],
    safety_pass: bool,
    mission_pass: bool,
    colregs_pass: bool,
    stability_pass: bool,
    layer_statuses: dict[str, str] | None = None,
    trace_artifact_path: str | None = None,
    no_action_trace_path: str | None = None,
    chain_summary: dict[str, Any] | None = None,
) -> TraceEvaluationReport:
    layer_statuses = layer_statuses or {}
    layers = {
        "L1_scenario_validity": {"status": layer_statuses.get("L1_scenario_validity", "UNKNOWN")},
        "L2_safety_floor": {"status": layer_statuses.get("L2_safety_floor", "PASS" if safety_pass else "FAIL")},
        "L3_dynamic_risk": {"status": layer_statuses.get("L3_dynamic_risk", "PASS" if safety_pass else "FAIL")},
        "L4_colregs_compliance": {"status": layer_statuses.get("L4_colregs_compliance", "PASS" if colregs_pass else "FAIL")},
        "L5_route_recovery": {"status": layer_statuses.get("L5_route_recovery", "PASS" if mission_pass else "FAIL")},
        "L6_seamanship": {"status": layer_statuses.get("L6_seamanship", "PASS" if mission_pass else "FAIL")},
        "L7_stability": {"status": layer_statuses.get("L7_stability", "PASS" if stability_pass else "FAIL")},
    }
    return TraceEvaluationReport(
        scenario_id=scenario_id,
        clean_8probe=True,
        verdict=TraceEvaluationVerdict(safety_pass, mission_pass, colregs_pass, stability_pass),
        threshold_provenance=make_threshold_provenance(cpa_acceptance),
        layers=layers,
        first_failure=first_failed_layer(layers),
        trace_artifact_path=trace_artifact_path,
        no_action_trace_path=no_action_trace_path,
        chain_summary=chain_summary,
    )


def report_from_runner_result(
    *,
    scenario_id: str,
    expected_outcome: dict[str, Any],
    result: dict[str, Any],
    trace_artifact_path: str | None = None,
    no_action_trace_path: str | None = None,
) -> TraceEvaluationReport:
    safety_pass = bool(result.get("cpa_ok") and result.get("domain_gates", {}).get("risk_gate_ok", True))
    route_recovery_pass = bool(
        result.get("route_corridor_ok")
        and ((not result.get("route_return_required")) or result.get("returned_to_route"))
        and ((not result.get("overtake_required")) or result.get("overtake_completed"))
    )
    seamanship_pass = bool(result.get("domain_gates", {}).get("seamanship_gate_ok", True))
    mission_pass = bool(route_recovery_pass and seamanship_pass)
    phase_semantics = result.get("phase_semantics") or {}
    phase_semantics_ok = bool(phase_semantics.get("phase_semantics_ok", True))
    colregs_pass = (
        str(result.get("compliance_verdict")) in {"full", "unknown"}
        and phase_semantics_ok
    )
    stability_pass = bool(result.get("stability_pass"))
    return evaluate_trace(
        scenario_id=scenario_id,
        cpa_acceptance=expected_outcome["cpa_acceptance"],
        safety_pass=safety_pass,
        mission_pass=mission_pass,
        colregs_pass=colregs_pass,
        stability_pass=stability_pass,
        layer_statuses={
            "L1_scenario_validity": "UNKNOWN",
            "L5_route_recovery": "PASS" if route_recovery_pass else "FAIL",
            "L6_seamanship": "PASS" if seamanship_pass else "FAIL",
        },
        trace_artifact_path=trace_artifact_path,
        no_action_trace_path=no_action_trace_path,
        chain_summary=result.get("chain_summary"),
    )
