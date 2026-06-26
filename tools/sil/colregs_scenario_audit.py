"""Read-only COLREGs scenario coverage audit.

The audit checks whether YAML scenarios expose the five observable avoidance
stages needed by the probe evidence chain. It does not change gates or
scenario geometry.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any

import yaml

KN_TO_MPS = 0.514444
M_PER_DEG_LAT = 111_120.0

CLEAN8_SCENARIOS = [
    "colreg-rule14-ho",
    "colreg-rule14-ho-port",
    "colreg-rule13-ot",
    "colreg-rule15-cs",
    "colreg-rule15-cs-2",
    "colreg-rule15-cs-edge",
    "colreg-rule15-ot-boundary",
    "colreg-rule17-cr-so",
]

INTELLIGENT_SCENARIOS = [
    "colreg-rule14-ho-intelligent",
    "colreg-rule15-cs-intelligent",
    "colreg-rule13-ot-target-giveway",
    "colreg-rule17-cr-so-target-giveway",
]

CLEAN12_SCENARIOS = CLEAN8_SCENARIOS + INTELLIGENT_SCENARIOS
DEFAULT_THRESHOLDS_PATH = Path("src/l3_tdl_kernel/m6_colregs_reasoner/config/odd_aware_thresholds.yaml")
MIN_REVIEWABLE_FREE_APPROACH_S = 60.0
MIN_REVIEWABLE_WARNING_WINDOW_S = 60.0
MIN_RETURN_WINDOW_S = 300.0
CPA_PROFILE_CONTRACTS = {
    "corridor_close_start_4L": {
        "acceptance_m": 180.0,
        "m6_onset_m": 180.0,
        "m6_release_m": 180.0,
        "m7_emergency_m": 180.0,
    },
    "corridor_follow_or_overtake_4L": {
        "acceptance_m": 180.0,
        "m6_onset_m": 180.0,
        "m6_release_m": 180.0,
        "m7_emergency_m": 180.0,
    },
    "open_water_crossing_20L": {
        "acceptance_m": 900.0,
        "m6_onset_m": 900.0,
        "m6_release_m": 900.0,
        "m7_emergency_m": 180.0,
    },
    "corridor_boundary_6L": {
        "acceptance_m": 270.0,
        "m6_onset_m": 270.0,
        "m6_release_m": 270.0,
        "m7_emergency_m": 180.0,
    },
    "standon_in_extremis_4L": {
        "acceptance_m": 180.0,
        "m6_onset_m": 180.0,
        "m6_release_m": 180.0,
        "m7_emergency_m": 180.0,
    },
    "corridor_follow_or_overtake_6L": {
        "acceptance_m": 270.0,
        "m6_onset_m": 270.0,
        "m6_release_m": 270.0,
        "m7_emergency_m": 180.0,
    },
}


def _enu(lat: float, lon: float, lat0: float, lon0: float) -> tuple[float, float]:
    return (
        (lon - lon0) * M_PER_DEG_LAT * math.cos(math.radians(lat0)),
        (lat - lat0) * M_PER_DEG_LAT,
    )


def _velocity(cog_deg: float, sog_kn: float) -> tuple[float, float]:
    speed = sog_kn * KN_TO_MPS
    rad = math.radians(cog_deg)
    return speed * math.sin(rad), speed * math.cos(rad)


def _straight_line_cpa(doc: dict[str, Any]) -> dict[str, float]:
    sim = doc.get("metadata", {}).get("simulation_settings", {})
    lat0, lon0 = sim.get("coordinate_origin", [63.44, 10.38])
    own = doc["ownShip"]["initial"]
    target = doc["targetShips"][0]["initial"]
    own_e, own_n = _enu(
        float(own["position"]["latitude"]),
        float(own["position"]["longitude"]),
        float(lat0),
        float(lon0),
    )
    tgt_e, tgt_n = _enu(
        float(target["position"]["latitude"]),
        float(target["position"]["longitude"]),
        float(lat0),
        float(lon0),
    )
    own_vx, own_vy = _velocity(float(own.get("cog", own.get("heading", 0.0))), float(own["sog"]))
    tgt_vx, tgt_vy = _velocity(float(target.get("cog", target.get("heading", 0.0))), float(target["sog"]))
    rx, ry = tgt_e - own_e, tgt_n - own_n
    rvx, rvy = tgt_vx - own_vx, tgt_vy - own_vy
    vv = rvx * rvx + rvy * rvy
    tcpa = 0.0 if vv < 1.0e-9 else max(0.0, -((rx * rvx) + (ry * rvy)) / vv)
    dcpa = math.hypot(rx + rvx * tcpa, ry + rvy * tcpa)
    abs_bearing = math.degrees(math.atan2(rx, ry)) % 360.0
    rel_bearing = (abs_bearing - float(own.get("heading", own.get("cog", 0.0)))) % 360.0
    return {
        "range_m": round(math.hypot(rx, ry), 3),
        "range_nm": round(math.hypot(rx, ry) / 1852.0, 3),
        "tcpa_s": round(tcpa, 3),
        "dcpa_m": round(dcpa, 3),
        "rel_bearing_deg": round(rel_bearing, 3),
        "relative_speed_mps": round(math.sqrt(vv), 3),
    }


# Encounter classification sector boundaries (COLREGs).
# Head-on cone: rel bearing ±6° from bow (reciprocal/near-reciprocal).
# Overtaking sector: >22.5° abaft beam = rel bearing in (112.5°, 247.5°) absolute.
# Crossing: everything else forward of the overtaking sector.
HEAD_ON_CONE_DEG = 6.0
OVERTAKE_SECTOR_START_DEG = 112.5
# Rule 13(a) dynamic-overtaking parameters. A static t=0 bearing snapshot cannot
# express "catching up from astern" — when own and target share a course and own
# is faster, a target dead-ahead at t=0 is a legitimate overtake, not head-on.
OVERTAKE_HEADING_DIFF_MAX_DEG = 30.0
OVERTAKE_AHEAD_CONE_DEG = 90.0  # target forward of beam counts as being overtaken


def _heading_diff_deg(h1: float, h2: float) -> float:
    """Smallest signed heading difference, magnitude in [0, 180]."""
    d = (float(h1) - float(h2)) % 360.0
    return d if d <= 180.0 else d - 360.0


def _classify_sector(abs_rel_deg: float) -> str:
    """Classify encounter from absolute relative bearing (0..180).

    abs_rel_deg is the unsigned relative bearing magnitude (port/starboard
    mirrored). Sector boundaries follow COLREGs:
      - [0, 6]: head-on (Rule 14)
      - (6, 112.5): crossing (Rule 15)
      - [112.5, 180]: overtaking (Rule 13)

    Note: this is a static t=0 snapshot. Same-direction overtaking (Rule 13)
    where own is faster but the target is still ahead must be corrected by the
    dynamic-overtake override in _encounter_classification.
    """
    if abs_rel_deg <= HEAD_ON_CONE_DEG:
        return "Rule14_HeadOn"
    if abs_rel_deg < OVERTAKE_SECTOR_START_DEG:
        return "Rule15_Crossing"
    return "Rule13_Overtaking"


def _boundary_margin(abs_rel_deg: float) -> float:
    """Distance (deg) to nearest sector *junction* (Rule14/15 or Rule15/13).

    Bow (0°) and stern (180°) are sector apexes, not junctions — a pure
    head-on / pure overtaking geometry is unambiguous, so it is interior.
    Only the 6° and 112.5° junctions are classification boundaries.
    """
    candidates = [
        abs(abs_rel_deg - HEAD_ON_CONE_DEG),
        abs(abs_rel_deg - OVERTAKE_SECTOR_START_DEG),
    ]
    return min(candidates)


def _encounter_classification(
    geometry: dict[str, Any],
    own_heading_deg: float,
    *,
    own_speed: float | None = None,
    own_course: float | None = None,
    target_speed: float | None = None,
    target_course: float | None = None,
) -> dict[str, Any]:
    """Recompute COLREGs rule classification from geometry, cross-checkable
    against metadata.encounter.rule (design §4.2).

    geometry must contain 'rel_bearing_deg' as computed by _straight_line_cpa
    (0..360, starboard-positive nautical convention). own_heading_deg is the
    own-ship heading used for documentation and dynamic-overtake detection.

    Dynamic-overtake override (Rule 13(a)): when own/target courses align
    (heading diff < OVERTAKE_HEADING_DIFF_MAX_DEG) and own is faster, a target
    that is still ahead of the beam at t=0 is a legitimate overtake, not
    head-on. This corrects the static snapshot's blind spot.
    """
    rel = float(geometry["rel_bearing_deg"]) % 360.0
    # Normalize to signed [-180, 180] for port/starboard mirroring.
    rel_signed = rel if rel <= 180.0 else rel - 360.0
    abs_rel = abs(rel_signed)
    compiled_rule = _classify_sector(abs_rel)

    # Dynamic-overtake override (Rule 13(a)).
    overtake_dynamic = False
    if (
        own_speed is not None and target_speed is not None
        and own_course is not None and target_course is not None
        and float(own_speed) > float(target_speed)
        and abs(_heading_diff_deg(float(own_course), float(target_course))) <= OVERTAKE_HEADING_DIFF_MAX_DEG
        and abs_rel < OVERTAKE_AHEAD_CONE_DEG
    ):
        compiled_rule = "Rule13_Overtaking"
        overtake_dynamic = True

    margin = _boundary_margin(abs_rel)
    return {
        "compiled_rule": compiled_rule,
        "compiled_rel_bearing_deg": round(rel_signed, 3),
        "own_heading_deg": round(float(own_heading_deg), 3),
        "boundary_distance_deg": round(margin, 3),
        "classification": "interior" if margin >= 5.0 else "boundary",
        "overtake_dynamic": overtake_dynamic,
    }


def _normalize_declared_rule(declared: str) -> str:
    """Map metadata.encounter.rule variants to compiled_rule keys."""
    d = declared.lower().replace(" ", "").replace("-", "")
    if "rule14" in d or "headon" in d or d == "r14":
        return "Rule14_HeadOn"
    if "rule13" in d or "overtak" in d or d == "r13":
        return "Rule13_Overtaking"
    if "rule15" in d or "crossing" in d or d == "r15":
        return "Rule15_Crossing"
    if "rule17" in d or "standon" in d or d == "r17":
        # Stand-on is a role, not a sector; defer to crossing/head-on geometry.
        return ""
    return ""


def _load_thresholds(path: Path | None = None, odd_key: str = "odd_a") -> dict[str, float]:
    threshold_path = path or DEFAULT_THRESHOLDS_PATH
    doc = yaml.safe_load(threshold_path.read_text(encoding="utf-8"))
    raw = doc.get(odd_key, {})
    return {
        "t_monitor_s": float(raw.get("t_monitor_s", 1500.0)),
        "t_plan_s": float(raw.get("t_plan_s", 720.0)),
        "t_emergency_s": float(raw.get("t_emergency_s", 60.0)),
        "cpa_soft_m": float(raw.get("cpa_soft_m", 2778.0)),
        "cpa_hard_m": float(raw.get("cpa_hard_m", 1852.0)),
        "cpa_safe_m": float(raw.get("cpa_safe_m", 1852.0)),
    }


def _trigger_time(tcpa_s: float, dcpa_m: float, *, tcpa_limit_s: float, cpa_limit_m: float) -> float | None:
    if dcpa_m >= cpa_limit_m or tcpa_s < 0.0:
        return None
    return round(max(0.0, tcpa_s - tcpa_limit_s), 3)


def _phase_windows(doc: dict[str, Any], geometry: dict[str, float], thresholds: dict[str, float]) -> dict[str, Any]:
    sim = doc.get("metadata", {}).get("simulation_settings", {})
    expected = doc.get("metadata", {}).get("expected_outcome", {})
    environment = doc.get("environment", {})
    total_time = float(sim.get("total_time", 0.0) or 0.0)
    visibility_nm = float(environment.get("visibility_nm", 0.0) or 0.0)
    relative_speed = float(geometry.get("relative_speed_mps", 0.0) or 0.0)
    monitor_t = _trigger_time(
        geometry["tcpa_s"],
        geometry["dcpa_m"],
        tcpa_limit_s=thresholds["t_monitor_s"],
        cpa_limit_m=thresholds["cpa_soft_m"],
    )
    active_t = _trigger_time(
        geometry["tcpa_s"],
        geometry["dcpa_m"],
        tcpa_limit_s=thresholds["t_plan_s"],
        cpa_limit_m=thresholds["cpa_hard_m"],
    )
    if active_t == 0.0:
        bucket = "ACTIVE"
    elif monitor_t == 0.0:
        bucket = "PREPLAN"
    elif monitor_t is not None:
        bucket = "FREE_APPROACH"
    else:
        bucket = "OUTSIDE_COLREG_WINDOW"

    free_approach = monitor_t if monitor_t is not None else total_time
    warning_to_active = None
    if monitor_t is not None and active_t is not None:
        warning_to_active = round(max(0.0, active_t - monitor_t), 3)
    post_cpa = None
    if total_time > 0.0:
        post_cpa = round(total_time - geometry["tcpa_s"], 3)
    required_monitor_range_m = math.hypot(
        geometry["dcpa_m"],
        relative_speed * thresholds["t_monitor_s"],
    )
    required_active_range_m = math.hypot(
        geometry["dcpa_m"],
        relative_speed * thresholds["t_plan_s"],
    )
    return {
        "thresholds": thresholds,
        "simulation_total_time_s": total_time,
        "visibility_nm": round(visibility_nm, 3),
        "initial_fsm_bucket": bucket,
        "monitor_trigger_time_s": monitor_t,
        "active_trigger_time_s": active_t,
        "free_approach_window_s": round(max(0.0, free_approach), 3),
        "warning_to_active_window_s": warning_to_active,
        "post_cpa_return_window_s": post_cpa,
        "return_window_required": bool(expected.get("returned_to_route_required", False)),
        "required_range_for_monitor_m": round(required_monitor_range_m, 3),
        "required_range_for_monitor_nm": round(required_monitor_range_m / 1852.0, 3),
        "required_range_for_active_m": round(required_active_range_m, 3),
        "required_range_for_active_nm": round(required_active_range_m / 1852.0, 3),
    }


def _metadata_id(path: Path, doc: dict[str, Any]) -> str:
    raw = str(doc.get("metadata", {}).get("scenario_id") or path.stem)
    return raw.removesuffix("-v1.0").removesuffix("-v2.0")


def _stage_coverage(doc: dict[str, Any], geometry: dict[str, float]) -> dict[str, bool]:
    metadata = doc.get("metadata", {})
    encounter = metadata.get("encounter", {})
    expected = metadata.get("expected_outcome", {})
    target_behaviors = [
        ts.get("behavior", {}).get("policy")
        for ts in doc.get("targetShips", [])
        if isinstance(ts, dict)
    ]
    expected_action = str(encounter.get("expected_own_action", ""))
    avoidance_delta = abs(float(encounter.get("avoidance_delta_rad", 0.0) or 0.0))
    return {
        "approach": bool(doc.get("ownShip", {}).get("nominalRoute")) and bool(doc.get("targetShips")),
        "trigger": bool(metadata.get("colregs_rules")) and geometry["tcpa_s"] > 0.0,
        "obvious_avoidance": expected_action.startswith("turn_") or avoidance_delta > 0.0 or any(target_behaviors),
        "pass_or_release": bool(expected.get("rule_compliance")) and float(expected.get("cpa_min_m_ge", 0.0) or 0.0) > 0.0,
        "return_to_route": (
            "returned_to_route_required" in expected
            and (
                bool(expected.get("returned_to_route_required"))
                or bool(expected.get("overtake_required"))
                or bool(expected.get("route_corridor_pass_limit_m"))
            )
        ),
    }


def _finding(scenario_id: str, code: str, message: str, severity: str = "review") -> dict[str, str]:
    return {
        "scenario_id": scenario_id,
        "code": code,
        "severity": severity,
        "message": message,
    }


def _recommendation(action: str, reason: str, priority: str = "medium") -> dict[str, str]:
    return {
        "action": action,
        "priority": priority,
        "reason": reason,
    }


def _round_contract_values(raw: dict[str, Any]) -> dict[str, float]:
    return {
        "acceptance_m": float(raw.get("acceptance_m", 0.0) or 0.0),
        "m6_onset_m": float(raw.get("m6_onset_m", 0.0) or 0.0),
        "m6_release_m": float(raw.get("m6_release_m", 0.0) or 0.0),
        "m7_emergency_m": float(raw.get("m7_emergency_m", 0.0) or 0.0),
    }


def _cpa_contract(doc: dict[str, Any]) -> dict[str, Any]:
    expected = doc.get("metadata", {}).get("expected_outcome", {})
    cpa_acceptance = expected.get("cpa_acceptance") or {}
    profile = str(cpa_acceptance.get("profile", ""))
    expected_contract = CPA_PROFILE_CONTRACTS.get(profile)
    declared_raw = cpa_acceptance.get("contract") or {}
    declared = _round_contract_values(declared_raw) if declared_raw else {}
    status = "unknown_profile"
    if expected_contract is not None:
        status = "missing" if not declared else "ok"
        for key, expected_value in expected_contract.items():
            if not declared or abs(declared.get(key, 0.0) - expected_value) > 1.0e-6:
                status = "mismatch" if declared else "missing"
                break
    return {
        "profile": profile,
        "declared": declared,
        "expected": expected_contract or {},
        "status": status,
        "runtime_odd_a_cpa_hard_m": 1852.0,
    }


def _intent_profile(
    scenario_id: str,
    rule: str,
    expected_action: str,
    geometry: dict[str, float],
    phase_windows: dict[str, Any],
    findings: list[dict[str, str]],
) -> dict[str, str]:
    rule_l = rule.lower()
    code_set = {finding["code"] for finding in findings}
    if scenario_id.endswith("-cs-2"):
        name = "short_window"
        objective = "Short reaction Rule15 give-way; keep a visible pre-active lead-in, then force decisive action."
    elif scenario_id.endswith("-cs-edge") or scenario_id.endswith("-ot-boundary"):
        name = "classification_boundary"
        objective = "COLREGs boundary classification/latch probe; stage windows may be shorter but must not start active."
    elif "rule17" in rule_l:
        name = "stand_on_late_action"
        objective = "Stand-on probe; own ship should hold course first, then act only after late-action condition."
    elif "rule13" in rule_l:
        name = "overtake_completion"
        objective = "Overtaking probe; show approach, passing, past-and-clear completion, and stable release."
    else:
        name = "reviewable_long_approach"
        objective = "Full five-stage demonstration: monitor, approach, warning, avoidance, pass/release, return."

    verdict = "reviewable"
    if "ACTIVE_THRESHOLD_BEYOND_VISIBILITY" in code_set and name == "classification_boundary":
        verdict = "threshold_profile_conflict"
    elif "IMMEDIATE_ACTIVE_WINDOW" in code_set:
        verdict = "needs_yaml_or_threshold_review"
    elif name == "short_window" and geometry["tcpa_s"] > 900.0:
        verdict = "needs_yaml_geometry_repair"
    elif name == "stand_on_late_action" and expected_action != "maintain":
        verdict = "needs_yaml_contract_review"

    return {
        "name": name,
        "objective": objective,
        "verdict": verdict,
        "initial_fsm_bucket": str(phase_windows["initial_fsm_bucket"]),
    }


def _recommendations(
    scenario_id: str,
    rule: str,
    expected_action: str,
    geometry: dict[str, float],
    phase_windows: dict[str, Any],
    findings: list[dict[str, str]],
    intent_profile: dict[str, str],
) -> list[dict[str, str]]:
    code_set = {finding["code"] for finding in findings}
    profile = intent_profile["name"]
    items: list[dict[str, str]] = []

    if "INSIDE_1KM_LABEL_GEOMETRY_MISMATCH" in code_set:
        items.append(_recommendation(
            "repair_metadata_geometry_label",
            "Metadata says inside 1km but straight-line initial range is larger; fix label or geometry before using it as threshold evidence.",
            "high" if profile == "overtake_completion" else "medium",
        ))
    if "SHORT_TCPA_LABEL_GEOMETRY_MISMATCH" in code_set or (
        profile == "short_window" and geometry["tcpa_s"] > 900.0
    ):
        items.append(_recommendation(
            "repair_short_window_geometry",
            "Scenario intent is short-window, but TCPA is too close to regular Rule15; move target closer or adjust relative speed while keeping a nonzero pre-active lead-in.",
            "high",
        ))

    if profile == "reviewable_long_approach" and "IMMEDIATE_ACTIVE_WINDOW" in code_set:
        items.append(_recommendation(
            "increase_initial_range_or_reduce_closing_speed",
            "Regular scenarios should show monitoring and approach before active avoidance; current DCPA/TCPA starts inside ACTIVE.",
            "high",
        ))
    if profile == "short_window" and "IMMEDIATE_ACTIVE_WINDOW" in code_set:
        items.append(_recommendation(
            "keep_short_but_nonzero_pre_active_window",
            "Short-window probes may be urgent, but should still show a short monitor/warning lead-in instead of ACTIVE at t=0.",
            "high",
        ))
    if profile == "classification_boundary" and "ACTIVE_THRESHOLD_BEYOND_VISIBILITY" in code_set:
        items.append(_recommendation(
            "review_threshold_profile_for_high_speed_boundary",
            "Keeping current speed and ODD-A t_plan would require an initial range beyond visibility to avoid ACTIVE@t=0; review boundary-specific timing intent before changing geometry.",
            "high",
        ))
    elif profile == "classification_boundary" and "IMMEDIATE_ACTIVE_WINDOW" in code_set:
        items.append(_recommendation(
            "add_boundary_pre_active_lead_in",
            "Boundary cases can stay short, but need enough pre-active samples to prove classification latch rather than starting already active.",
            "medium",
        ))

    if (
        scenario_id.endswith("-ot-boundary")
        and expected_action == "turn_starboard"
        and "ACTIVE_THRESHOLD_BEYOND_VISIBILITY" in code_set
    ):
        items.append(_recommendation(
            "review_slow_down_seamanship_contract",
            "Fast right-aft crossing boundary may be better represented by slow_down or slow_down plus starboard bias; scorer contract must support that before YAML change.",
            "high",
        ))
    if profile == "stand_on_late_action":
        active_t = phase_windows["active_trigger_time_s"]
        if "IMMEDIATE_ACTIVE_WINDOW" in code_set or active_t is None or active_t < 240.0:
            items.append(_recommendation(
                "ensure_pre_action_hold_window",
                "Rule17 needs measurable hold-course phase before late independent action; immediate ACTIVE undermines stand-on semantics.",
                "high" if "IMMEDIATE_ACTIVE_WINDOW" in code_set else "medium",
            ))
    if profile == "overtake_completion" and "IMMEDIATE_ACTIVE_WINDOW" in code_set:
        items.append(_recommendation(
            "add_overtake_approach_before_active",
            "Rule13 should show closing/overtaking approach before active maneuver, then past-and-clear completion.",
            "medium",
        ))
    if "EXPECTED_CPA_FLOOR_BELOW_ODD_ACTIVE_CPA" in code_set:
        items.append(_recommendation(
            "review_cpa_floor_contract",
            "Scenario pass CPA floor is below M6 ODD-A active CPA threshold; document whether test CPA gate and M6 release/onset CPA intentionally differ.",
            "medium",
        ))
    if "CPA_CONTRACT_MISSING" in code_set or "CPA_CONTRACT_MISMATCH" in code_set:
        items.append(_recommendation(
            "declare_profile_cpa_contract",
            "Declare acceptance/onset/release/emergency CPA values under cpa_acceptance.contract so evaluator and module semantics are unambiguous.",
            "high",
        ))
    return items


def _findings(
    scenario_id: str,
    doc: dict[str, Any],
    geometry: dict[str, float],
    phase_windows: dict[str, Any],
    cpa_contract: dict[str, Any],
) -> list[dict[str, str]]:
    expected = doc.get("metadata", {}).get("expected_outcome", {})
    basis = str((expected.get("cpa_acceptance") or {}).get("basis", "")).lower()
    profile = str((expected.get("cpa_acceptance") or {}).get("profile", "")).lower()
    findings: list[dict[str, str]] = []
    if ("short-tcpa" in basis or "short_tcpa" in profile) and geometry["tcpa_s"] > 900.0:
        findings.append(_finding(
            scenario_id,
            "SHORT_TCPA_LABEL_GEOMETRY_MISMATCH",
            f"metadata says short-TCPA but straight-line TCPA is {geometry['tcpa_s']}s",
        ))
    if "inside 1km" in basis and geometry["range_m"] > 1200.0:
        findings.append(_finding(
            scenario_id,
            "INSIDE_1KM_LABEL_GEOMETRY_MISMATCH",
            f"metadata says inside 1km but initial range is {geometry['range_m']}m",
        ))
    if phase_windows["initial_fsm_bucket"] == "ACTIVE":
        thresholds = phase_windows["thresholds"]
        findings.append(_finding(
            scenario_id,
            "IMMEDIATE_ACTIVE_WINDOW",
            "straight-line DCPA/TCPA already satisfy ACTIVE gate at t=0 "
            f"(DCPA={geometry['dcpa_m']}m < {thresholds['cpa_hard_m']}m, "
            f"TCPA={geometry['tcpa_s']}s <= {thresholds['t_plan_s']}s)",
            "high",
        ))
    if (
        phase_windows["initial_fsm_bucket"] == "ACTIVE"
        and phase_windows["free_approach_window_s"] < MIN_REVIEWABLE_FREE_APPROACH_S
    ):
        findings.append(_finding(
            scenario_id,
            "NO_REVIEWABLE_FREE_APPROACH_WINDOW",
            "scenario starts inside the monitor/preplan window; it cannot show a clean "
            "free-monitoring approach before COLREGs warning",
            "high",
        ))
    warning_window = phase_windows["warning_to_active_window_s"]
    if warning_window is not None and warning_window < MIN_REVIEWABLE_WARNING_WINDOW_S:
        findings.append(_finding(
            scenario_id,
            "COMPRESSED_WARNING_TO_ACTIVE_WINDOW",
            f"warning-to-active window is {warning_window}s; stage transition is too compressed for review",
            "high",
        ))
    visibility_nm = float(phase_windows["visibility_nm"])
    if (
        visibility_nm > 0.0
        and phase_windows["initial_fsm_bucket"] != "PREPLAN"
        and phase_windows["required_range_for_monitor_nm"] > visibility_nm
    ):
        findings.append(_finding(
            scenario_id,
            "MONITOR_THRESHOLD_BEYOND_VISIBILITY",
            "current relative speed requires initial range "
            f"{phase_windows['required_range_for_monitor_nm']}NM to show a free approach before "
            f"t_monitor, beyond visibility {visibility_nm}NM",
        ))
    if visibility_nm > 0.0 and phase_windows["required_range_for_active_nm"] > visibility_nm:
        findings.append(_finding(
            scenario_id,
            "ACTIVE_THRESHOLD_BEYOND_VISIBILITY",
            "current relative speed requires initial range "
            f"{phase_windows['required_range_for_active_nm']}NM to avoid immediate ACTIVE, "
            f"beyond visibility {visibility_nm}NM",
            "high",
        ))
    if cpa_contract["status"] == "missing":
        findings.append(_finding(
            scenario_id,
            "CPA_CONTRACT_MISSING",
            "cpa_acceptance.contract must declare acceptance_m, m6_onset_m, m6_release_m, and m7_emergency_m",
            "high",
        ))
    elif cpa_contract["status"] == "mismatch":
        findings.append(_finding(
            scenario_id,
            "CPA_CONTRACT_MISMATCH",
            "cpa_acceptance.contract does not match the profile-derived CPA contract",
            "high",
        ))
    post_cpa = phase_windows["post_cpa_return_window_s"]
    if (
        phase_windows["return_window_required"]
        and post_cpa is not None
        and post_cpa < MIN_RETURN_WINDOW_S
    ):
        findings.append(_finding(
            scenario_id,
            "SHORT_POST_CPA_RETURN_WINDOW",
            f"post-CPA simulation window is {post_cpa}s; route-return review may be under-specified",
            "high",
        ))
    # Encounter classification cross-check (design §4.2).
    encounter = doc.get("metadata", {}).get("encounter", {})
    declared_rule = str(encounter.get("rule", "")).strip()
    own_initial = doc.get("ownShip", {}).get("initial", {})
    target_initial = doc.get("targetShips", [{}])[0].get("initial", {}) if doc.get("targetShips") else {}
    own_heading = float(own_initial.get("heading", 0.0) or 0.0)
    cls = _encounter_classification(
        geometry,
        own_heading,
        own_speed=own_initial.get("sog"),
        own_course=own_initial.get("cog", own_initial.get("heading")),
        target_speed=target_initial.get("sog"),
        target_course=target_initial.get("cog", target_initial.get("heading")),
    )
    compiled = cls["compiled_rule"]
    declared_normalized = _normalize_declared_rule(declared_rule)
    if declared_normalized and declared_normalized != compiled:
        findings.append(_finding(
            scenario_id,
            "SCENARIO_ENCOUNTER_MISMATCH",
            f"metadata.encounter.rule='{declared_rule}' but geometry "
            f"compiles to {compiled} (rel_bearing={cls['compiled_rel_bearing_deg']}°)",
            severity="review",
        ))
    # Boundary scenarios get an advisory note (affects Layer-3 hysteresis tolerance).
    if cls["classification"] == "boundary":
        findings.append(_finding(
            scenario_id,
            "BOUNDARY_SCENARIO",
            f"rel_bearing {cls['compiled_rel_bearing_deg']}° is within 5° of a "
            f"classification boundary ({compiled}); hysteresis gate should allow flip",
            severity="info",
        ))
    return findings


def audit_scenario_file(path: Path, thresholds_path: Path | None = None) -> dict[str, Any]:
    doc = yaml.safe_load(path.read_text(encoding="utf-8"))
    metadata = doc.get("metadata", {})
    encounter = metadata.get("encounter", {})
    expected = metadata.get("expected_outcome", {})
    geometry = _straight_line_cpa(doc)
    scenario_id = _metadata_id(path, doc)
    coverage = _stage_coverage(doc, geometry)
    thresholds = _load_thresholds(thresholds_path)
    phase_windows = _phase_windows(doc, geometry, thresholds)
    cpa_contract = _cpa_contract(doc)
    findings = _findings(scenario_id, doc, geometry, phase_windows, cpa_contract)
    intent_profile = _intent_profile(
        scenario_id,
        str(encounter.get("rule", "")),
        str(encounter.get("expected_own_action", "")),
        geometry,
        phase_windows,
        findings,
    )
    return {
        "scenario_id": scenario_id,
        "path": str(path),
        "rule": encounter.get("rule", ""),
        "give_way_vessel": encounter.get("give_way_vessel", ""),
        "expected_action": encounter.get("expected_own_action", ""),
        "geometry": geometry,
        "phase_windows": phase_windows,
        "cpa_contract": cpa_contract,
        "intent_profile": intent_profile,
        "expected": {
            "cpa_min_m_ge": float(expected.get("cpa_min_m_ge", 0.0) or 0.0),
            "returned_to_route_required": bool(expected.get("returned_to_route_required", False)),
            "overtake_required": bool(expected.get("overtake_required", False)),
        },
        "stage_coverage": coverage,
        "stage_coverage_complete": all(coverage.values()),
        "findings": findings,
        "recommendations": _recommendations(
            scenario_id,
            str(encounter.get("rule", "")),
            str(encounter.get("expected_own_action", "")),
            geometry,
            phase_windows,
            findings,
            intent_profile,
        ),
    }


def _phase_window_summary(scenarios: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "immediate_active_count": sum(
            1 for scenario in scenarios
            if scenario["phase_windows"]["initial_fsm_bucket"] == "ACTIVE"
        ),
        "immediate_monitor_count": sum(
            1 for scenario in scenarios
            if scenario["phase_windows"]["monitor_trigger_time_s"] == 0.0
        ),
        "free_approach_count": sum(
            1 for scenario in scenarios
            if scenario["phase_windows"]["free_approach_window_s"] >= MIN_REVIEWABLE_FREE_APPROACH_S
        ),
        "reviewable_warning_count": sum(
            1 for scenario in scenarios
            if (
                scenario["phase_windows"]["warning_to_active_window_s"] is not None
                and scenario["phase_windows"]["warning_to_active_window_s"] >= MIN_REVIEWABLE_WARNING_WINDOW_S
            )
        ),
    }


def audit_clean12_scenarios(scenarios_dir: Path, thresholds_path: Path | None = None) -> dict[str, Any]:
    scenarios = [
        audit_scenario_file(scenarios_dir / f"{scenario_id}.yaml", thresholds_path)
        for scenario_id in CLEAN12_SCENARIOS
    ]
    findings = [finding for scenario in scenarios for finding in scenario["findings"]]
    profile_summary: dict[str, int] = {}
    for scenario in scenarios:
        profile_name = scenario["intent_profile"]["name"]
        profile_summary[profile_name] = profile_summary.get(profile_name, 0) + 1
    return {
        "suite": "clean12",
        "scenario_count": len(scenarios),
        "all_stage_coverage_complete": all(s["stage_coverage_complete"] for s in scenarios),
        "phase_window_summary": _phase_window_summary(scenarios),
        "intent_profile_summary": profile_summary,
        "scenarios": scenarios,
        "findings": findings,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenarios-dir", type=Path, default=Path("scenarios/COLREGs测试"))
    parser.add_argument("--scenario", help="Audit one scenario id instead of clean12")
    parser.add_argument("--thresholds", type=Path, default=DEFAULT_THRESHOLDS_PATH)
    args = parser.parse_args()

    if args.scenario:
        report = audit_scenario_file(args.scenarios_dir / f"{args.scenario}.yaml", args.thresholds)
    else:
        report = audit_clean12_scenarios(args.scenarios_dir, args.thresholds)
    print(json.dumps(report, indent=2, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
