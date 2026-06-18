#!/usr/bin/env python3
import argparse
import json
import os
import shutil
import ssl
import sys
import time
import math
import subprocess
import urllib.request
from pathlib import Path
import pyarrow as pa
import pyarrow.ipc as ipc
import yaml

REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.sil.colregs_trace_evaluator import (
    CpaProfile,
    derive_cpa_threshold,
    report_from_runner_result,
)

# Phase B: behavioral-stability scorer — standalone import (no polars/ROS2) so
# this host-side runner can use the same logic the scoring package ships.
sys.path.insert(0, str(Path(__file__).resolve().parents[1] /
                       "src/sim_workbench/sil_nodes/scoring/scoring"))
import stability_scorer as ss  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parents[1] /
                       "src/l3_tdl_kernel/l3_risk_model/python"))
from l3_risk_model import (  # noqa: E402
    ColregsDuty,
    OwnShipInput,
    RankingState,
    RiskPhase,
    TargetInput,
    evaluate_target,
    select_primary,
)

BASE = os.environ.get("SIL_ORCH_BASE_URL", "https://127.0.0.1:18000/api/v1").rstrip("/")
CTX = ssl.create_default_context()
CTX.check_hostname = False
CTX.verify_mode = ssl.CERT_NONE

SCENARIOS = [
    "colreg-rule14-ho",
    "colreg-rule14-ho-port",
    "colreg-rule13-ot",
    "colreg-rule15-cs",
    "colreg-rule15-cs-2",
    "colreg-rule15-cs-edge",
    "colreg-rule15-ot-boundary",
    "colreg-rule17-cr-so",
]

ROUTE_CORRIDOR_HALF_WIDTH_M = 1000.0
ROUTE_CORRIDOR_PASS_LIMIT_M = 500.0
ROUTE_CORRIDOR_PASS_EPS_M = 5.0
DEFAULT_CPA_FLOOR_M = 500.0
CPA_FLOOR_MEASUREMENT_TOLERANCE_LOA = 0.05
DEFAULT_RESTART_CONTAINER = "mass-l3-sil-sil-nodes-1"
DEFAULT_RESTART_SETTLE_S = 24.0
MAX_WARNING_DOMAIN_EXPOSURE_S = 120.0
MAX_INTEGRATED_ABS_XTE_M_S = 500.0 * 600.0
MAX_ROUTE_CROSSING_OVERSHOOTS = 1
MAX_PATH_LENGTH_RATIO = 1.35
MAX_PRIMARY_THREAT_SWITCHES = 2
DANGER_EXPOSURE_GRACE_S = 5.0
DOMAIN_EPS = 1.0e-9
ROUTE_RETURN_RELEASE_DWELL_S = 10.0
CONFIGURE_RETRY_ATTEMPTS = 5
CONFIGURE_RETRY_DELAY_S = 2.0

def req(method, path, body=None, timeout=30):
    data = json.dumps(body).encode() if body is not None else None
    r = urllib.request.Request(
        BASE + path, data=data, method=method,
        headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(r, context=CTX, timeout=timeout) as resp:
        return json.loads(resp.read().decode())

def configure_scenario(scenario_id, *, attempts=CONFIGURE_RETRY_ATTEMPTS,
                       retry_delay_s=CONFIGURE_RETRY_DELAY_S):
    last = {"success": False, "error": "configure not attempted"}
    for attempt in range(max(1, attempts)):
        last = req("POST", "/lifecycle/configure", {"scenario_id": scenario_id})
        if last.get("success"):
            return last
        if attempt + 1 < attempts:
            time.sleep(retry_delay_s)
    return last

def get_sim_time():
    try:
        st = req("GET", "/lifecycle/status")
        snap = req("GET", "/debug/snapshot")
        return float(snap.get("sim_t", 0.0))
    except Exception:
        return 0.0

def _enu(lat, lon, lat0, lon0):
    e = (lon - lon0) * 111120.0 * math.cos(math.radians(lat0))
    n = (lat - lat0) * 111120.0
    return e, n

def get_cross_track_error(x, y, x0, y0, hdg_deg):
    theta = math.radians(hdg_deg)
    # cross track error = (x - x0) * cos(theta) - (y - y0) * sin(theta)
    return (x - x0) * math.cos(theta) - (y - y0) * math.sin(theta)

def _ownship_record_near_origin(record, lat0, lon0, max_distance_m=20000.0):
    try:
        x_m, y_m = _enu(float(record["lat"]), float(record["lon"]), lat0, lon0)
    except (KeyError, TypeError, ValueError):
        return False
    return math.hypot(x_m, y_m) <= max_distance_m

def _knots_to_mps(knots: float) -> float:
    return float(knots) * 0.514444

def _nav_deg_to_math_rad(nav_deg: float) -> float:
    return math.radians(90.0 - float(nav_deg))

def _target_id(meta: dict, index: int) -> str:
    static = meta.get("static") or {}
    for source in (static, meta):
        for key in ("mmsi", "id"):
            value = source.get(key)
            if value is not None and value != "":
                return str(value)
    return f"TS{index + 1:03d}"

def _velocity_components_mps(sog_kn: float, nav_deg: float) -> tuple[float, float]:
    speed_mps = _knots_to_mps(sog_kn)
    heading_rad = _nav_deg_to_math_rad(nav_deg)
    return speed_mps * math.cos(heading_rad), speed_mps * math.sin(heading_rad)

def _target_state_at(meta: dict, sim_t: float, lat0: float, lon0: float) -> dict:
    initial = meta.get("initial") or {}
    position = initial.get("position") or {}
    tgt_lat0 = float(meta.get("lat0", position.get("latitude", 0.0)))
    tgt_lon0 = float(meta.get("lon0", position.get("longitude", 0.0)))
    cog = float(meta.get("cog", initial.get("cog", 0.0)))
    sog_kn = float(meta.get("sog_kn", initial.get("sog", 0.0)))
    e0, n0 = _enu(tgt_lat0, tgt_lon0, lat0, lon0)
    vx_mps, vy_mps = _velocity_components_mps(sog_kn, cog)
    return {
        "x_m": e0 + vx_mps * float(sim_t),
        "y_m": n0 + vy_mps * float(sim_t),
        "cog": cog,
        "sog_kn": sog_kn,
        "sog_mps": _knots_to_mps(sog_kn),
        "vx_mps": vx_mps,
        "vy_mps": vy_mps,
    }

def _cpa_tcpa_m(px, py, rvx, rvy) -> tuple[float, float]:
    rel_speed_sq = rvx * rvx + rvy * rvy
    if rel_speed_sq <= 1.0e-9:
        return math.hypot(px, py), -1.0
    tcpa = -((px * rvx + py * rvy) / rel_speed_sq)
    cpa_t = max(tcpa, 0.0)
    cpa_m = math.hypot(px + rvx * cpa_t, py + rvy * cpa_t)
    return cpa_m, tcpa

def _infer_colregs_duty(encounter: dict, sim_t: float,
                        avoidance_onset_s: float | None) -> ColregsDuty:
    encounter = encounter or {}
    give_way_vessel = str(encounter.get("give_way_vessel", "")).lower()
    if give_way_vessel == "own":
        return ColregsDuty.GIVE_WAY

    rule = str(encounter.get("rule", "")).lower()
    if give_way_vessel == "target":
        if "rule17" in rule and avoidance_onset_s is not None and sim_t >= avoidance_onset_s:
            return ColregsDuty.RULE17_ACTION
        return ColregsDuty.STAND_ON_HOLD

    return ColregsDuty.FREE

def _avoidance_onset_s(run_records) -> float | None:
    behavior_records = sorted(
        (r for r in run_records if r.get("topic") == "/l3/m4/behavior_plan"),
        key=lambda r: float(r.get("sim_t", 0.0)),
    )
    for record in behavior_records:
        if _is_avoidance_behavior(record):
            return float(record.get("sim_t", 0.0))
    return None

def _risk_metrics_defaults() -> dict:
    return {
        "primary_threat_id": "",
        "primary_threat_switches": 0,
        "max_risk_score": 0.0,
        "worst_warning_margin_m": 0.0,
        "worst_danger_margin_m": 0.0,
        "max_warning_ddv": 0.0,
        "max_danger_ddv": 0.0,
        "warning_domain_exposure_s": 0.0,
        "danger_domain_exposure_s": 0.0,
        "risk_recovery_ok": True,
        "risk_trace": [],
    }

def _is_warning_or_worse(phase: RiskPhase) -> bool:
    return phase in (RiskPhase.WARNING, RiskPhase.DANGER, RiskPhase.CRITICAL)

def _is_danger_or_worse(phase: RiskPhase) -> bool:
    return phase in (RiskPhase.DANGER, RiskPhase.CRITICAL)

def _interval_after_grace(start_s: float, end_s: float, first_sample_s: float) -> float:
    grace_end_s = first_sample_s + DANGER_EXPOSURE_GRACE_S
    return max(0.0, end_s - max(start_s, grace_end_s))

def _first_risk_row_at_or_after(risk_trace, t_s):
    for row in risk_trace:
        if row["t_s"] >= t_s:
            return row
    return None

def _risk_row_outside_warning(row) -> bool:
    return (
        row.get("warning_ddv", 0.0) <= DOMAIN_EPS and
        row.get("danger_ddv", 0.0) <= DOMAIN_EPS and
        row.get("risk_phase") not in ("Warning", "Danger", "Critical")
    )

def _risk_recovery_ok(risk_trace, avoidance_onset_s, max_risk_score):
    if avoidance_onset_s is None:
        return all(_risk_row_outside_warning(row) for row in risk_trace)

    onset_row = _first_risk_row_at_or_after(risk_trace, avoidance_onset_s)
    if onset_row is None:
        return max_risk_score == 0.0

    post_onset_rows = [
        row for row in risk_trace
        if row["t_s"] >= avoidance_onset_s
    ]
    warning_or_worse_rows = [
        row for row in post_onset_rows
        if not _risk_row_outside_warning(row)
    ]
    if not warning_or_worse_rows:
        return True

    peak_row = max(
        warning_or_worse_rows,
        key=lambda row: float(row.get("risk_score", 0.0)),
    )
    recovery_row = _first_risk_row_at_or_after(
        risk_trace, peak_row["t_s"] + 60.0)
    if recovery_row is None:
        return False

    return (recovery_row["risk_score"] - peak_row["risk_score"]) < -0.02

def compute_risk_metrics(run_records, targets_meta, *, lat0, lon0, encounter=None) -> dict:
    ownship_records = sorted(
        (
            r for r in run_records
            if r.get("topic") == "/sil/own_ship_state" and
            _ownship_record_near_origin(r, lat0, lon0)
        ),
        key=lambda r: float(r.get("sim_t", 0.0)),
    )
    if not ownship_records or not targets_meta:
        return _risk_metrics_defaults()

    onset_s = _avoidance_onset_s(run_records)
    ranking_state = RankingState()
    previous_primary_id = None
    primary_switches = 0
    max_risk_score = 0.0
    worst_warning_margin = float("inf")
    worst_danger_margin = float("inf")
    max_warning_ddv = 0.0
    max_danger_ddv = 0.0
    warning_exposure_s = 0.0
    danger_exposure_s = 0.0
    risk_trace = []
    prev_t = None
    prev_warning_active = False
    prev_danger_active = False
    first_sample_s = float(ownship_records[0].get("sim_t", 0.0))

    for record in ownship_records:
        sim_t = float(record.get("sim_t", 0.0))
        if prev_t is not None:
            dt = max(0.0, sim_t - prev_t)
            if prev_warning_active:
                warning_exposure_s += dt
            if prev_danger_active:
                danger_exposure_s += _interval_after_grace(prev_t, sim_t, first_sample_s)

        own_x, own_y = _enu(float(record["lat"]), float(record["lon"]), lat0, lon0)
        own_hdg = float(record.get("heading_deg", record.get("cog_deg", 0.0)))
        own_sog_kn = float(record.get("sog_kn", record.get("sog", 0.0)))
        own_vx, own_vy = _velocity_components_mps(own_sog_kn, own_hdg)
        own = OwnShipInput(
            x_m=own_x,
            y_m=own_y,
            heading_rad=_nav_deg_to_math_rad(own_hdg),
            sog_mps=_knots_to_mps(own_sog_kn),
        )

        risks = []
        for index, target_meta in enumerate(targets_meta):
            target_state = _target_state_at(target_meta, sim_t, lat0, lon0)
            px = target_state["x_m"] - own_x
            py = target_state["y_m"] - own_y
            rvx = target_state["vx_mps"] - own_vx
            rvy = target_state["vy_mps"] - own_vy
            cpa_m, tcpa_s = _cpa_tcpa_m(px, py, rvx, rvy)
            target = TargetInput(
                id=_target_id(target_meta, index),
                x_m=target_state["x_m"],
                y_m=target_state["y_m"],
                cog_rad=_nav_deg_to_math_rad(target_state["cog"]),
                sog_mps=target_state["sog_mps"],
                cpa_m=cpa_m,
                tcpa_s=tcpa_s,
                confidence=0.9,
            )
            duty = _infer_colregs_duty(encounter or {}, sim_t, onset_s)
            risks.append(evaluate_target(own, target, duty))

        primary = select_primary(risks, ranking_state)
        if primary.target_id:
            if previous_primary_id is not None and primary.target_id != previous_primary_id:
                primary_switches += 1
            previous_primary_id = primary.target_id

        warning_active = any(_is_warning_or_worse(r.risk_phase) for r in risks)
        danger_active = any(_is_danger_or_worse(r.risk_phase) for r in risks)
        for risk in risks:
            max_risk_score = max(max_risk_score, risk.risk_score)
            worst_warning_margin = min(worst_warning_margin, risk.warning_margin_m)
            worst_danger_margin = min(worst_danger_margin, risk.danger_margin_m)
            max_warning_ddv = max(max_warning_ddv, risk.warning_ddv)
            max_danger_ddv = max(max_danger_ddv, risk.danger_ddv)

        risk_trace.append({
            "t_s": sim_t,
            "primary_threat_id": primary.target_id,
            "risk_phase": primary.risk_phase.value,
            "risk_score": primary.risk_score,
            "warning_margin_m": primary.warning_margin_m,
            "danger_margin_m": primary.danger_margin_m,
            "warning_ddv": primary.warning_ddv,
            "danger_ddv": primary.danger_ddv,
            "closing_speed_mps": primary.closing_speed_mps,
        })

        prev_t = sim_t
        prev_warning_active = warning_active
        prev_danger_active = danger_active

    if worst_warning_margin == float("inf"):
        worst_warning_margin = 0.0
    if worst_danger_margin == float("inf"):
        worst_danger_margin = 0.0

    return {
        "primary_threat_id": previous_primary_id or "",
        "primary_threat_switches": primary_switches,
        "max_risk_score": max_risk_score,
        "worst_warning_margin_m": worst_warning_margin,
        "worst_danger_margin_m": worst_danger_margin,
        "max_warning_ddv": max_warning_ddv,
        "max_danger_ddv": max_danger_ddv,
        "warning_domain_exposure_s": warning_exposure_s,
        "danger_domain_exposure_s": danger_exposure_s,
        "risk_recovery_ok": _risk_recovery_ok(risk_trace, onset_s, max_risk_score),
        "risk_trace": risk_trace,
    }

def compute_seamanship_metrics(run_records, *, lat0, lon0, init_lat, init_lon,
                               init_hdg, route_distance_m=None) -> dict:
    ownship_records = sorted(
        (
            r for r in run_records
            if r.get("topic") == "/sil/own_ship_state" and
            _ownship_record_near_origin(r, lat0, lon0)
        ),
        key=lambda r: float(r.get("sim_t", 0.0)),
    )
    if not ownship_records:
        return {
            "integrated_abs_xte_m_s": 0.0,
            "route_crossing_overshoot_count": 0,
            "path_length_m": 0.0,
            "path_length_ratio": 1.0,
        }

    init_x_m, init_y_m = _enu(init_lat, init_lon, lat0, lon0)
    samples = []
    for record in ownship_records:
        x_m, y_m = _enu(float(record["lat"]), float(record["lon"]), lat0, lon0)
        xte_m = get_cross_track_error(x_m, y_m, init_x_m, init_y_m, init_hdg)
        samples.append((float(record.get("sim_t", 0.0)), x_m, y_m, xte_m))

    integrated_abs_xte_m_s = 0.0
    path_length_m = 0.0
    for prev, cur in zip(samples, samples[1:]):
        dt = max(0.0, cur[0] - prev[0])
        integrated_abs_xte_m_s += ((abs(prev[3]) + abs(cur[3])) * 0.5) * dt
        path_length_m += math.hypot(cur[1] - prev[1], cur[2] - prev[2])

    deadband_m = 25.0
    route_crossing_overshoots = 0
    prev_sign = None
    for _, _, _, xte_m in samples:
        if xte_m > deadband_m:
            sign = 1
        elif xte_m < -deadband_m:
            sign = -1
        else:
            continue
        if prev_sign is not None and sign != prev_sign:
            route_crossing_overshoots += 1
        prev_sign = sign

    if route_distance_m is not None:
        denominator_m = max(float(route_distance_m), 1.0)
    else:
        first = samples[0]
        last = samples[-1]
        denominator_m = max(math.hypot(last[1] - first[1], last[2] - first[2]), 1.0)

    return {
        "integrated_abs_xte_m_s": integrated_abs_xte_m_s,
        "route_crossing_overshoot_count": route_crossing_overshoots,
        "path_length_m": path_length_m,
        "path_length_ratio": path_length_m / denominator_m,
    }

def compute_domain_gate_status(risk_metrics, seamanship_metrics, *,
                               close_start_emergency_allowed=False,
                               single_target=True,
                               integrated_abs_xte_limit_m_s=MAX_INTEGRATED_ABS_XTE_M_S) -> dict:
    danger_domain_ok = (
        True if close_start_emergency_allowed else
        risk_metrics.get("danger_domain_exposure_s", 0.0) <= DOMAIN_EPS
    )
    warning_domain_ok = (
        True if close_start_emergency_allowed else
        risk_metrics.get("warning_domain_exposure_s", 0.0) <= MAX_WARNING_DOMAIN_EXPOSURE_S
    )
    danger_ddv_ok = (
        close_start_emergency_allowed or
        risk_metrics.get("max_danger_ddv", 0.0) <= DOMAIN_EPS
    )
    risk_recovery_ok = bool(risk_metrics.get("risk_recovery_ok", False))
    integrated_xte_ok = (
        seamanship_metrics.get("integrated_abs_xte_m_s", 0.0) <=
        integrated_abs_xte_limit_m_s
    )
    route_crossing_ok = (
        seamanship_metrics.get("route_crossing_overshoot_count", 0) <=
        MAX_ROUTE_CROSSING_OVERSHOOTS
    )
    path_length_ok = (
        seamanship_metrics.get("path_length_ratio", 0.0) <=
        MAX_PATH_LENGTH_RATIO
    )
    threat_switch_ok = (
        True if not single_target else
        risk_metrics.get("primary_threat_switches", 0) <= MAX_PRIMARY_THREAT_SWITCHES
    )
    risk_gate_ok = (
        danger_domain_ok and warning_domain_ok and danger_ddv_ok and
        risk_recovery_ok and threat_switch_ok
    )
    seamanship_gate_ok = integrated_xte_ok and route_crossing_ok and path_length_ok
    return {
        "danger_domain_ok": bool(danger_domain_ok),
        "warning_domain_ok": bool(warning_domain_ok),
        "danger_ddv_ok": bool(danger_ddv_ok),
        "risk_recovery_ok": bool(risk_recovery_ok),
        "integrated_xte_ok": bool(integrated_xte_ok),
        "route_crossing_ok": bool(route_crossing_ok),
        "path_length_ok": bool(path_length_ok),
        "threat_switch_ok": bool(threat_switch_ok),
        "integrated_abs_xte_limit_m_s": float(integrated_abs_xte_limit_m_s),
        "risk_gate_ok": bool(risk_gate_ok),
        "seamanship_gate_ok": bool(seamanship_gate_ok),
        "domain_gate_ok": bool(risk_gate_ok and seamanship_gate_ok),
    }

def _close_start_emergency_allowed(expected: dict) -> bool:
    cpa_acceptance = expected.get("cpa_acceptance") or {}
    profile = str(cpa_acceptance.get("profile", "")).lower()
    return (
        bool(expected.get("allow_danger_domain_start", False)) or
        "close_start" in profile or
        "in_extremis" in profile or
        "follow_or_overtake_4l" in profile
    )

def _route_distance_m(scen_data, lat0, lon0) -> float:
    nominal_route = (scen_data.get("ownShip") or {}).get("nominalRoute") or []
    if len(nominal_route) < 2:
        return 0.0
    first = nominal_route[0]
    last = nominal_route[-1]
    first_e, first_n = _enu(
        float(first["latitude"]), float(first["longitude"]), lat0, lon0)
    last_e, last_n = _enu(
        float(last["latitude"]), float(last["longitude"]), lat0, lon0)
    return math.hypot(last_e - first_e, last_n - first_n)

def read_trace_run_records(trace_path=Path("runs/trace_current.jsonl")):
    records = []
    if trace_path.exists():
        with open(trace_path) as f:
            for line in f:
                try:
                    records.append(json.loads(line))
                except Exception:
                    pass

    start_idx = 0
    for i in range(1, len(records)):
        prev = records[i - 1].get("sim_t", 0.0)
        cur = records[i].get("sim_t", 0.0)
        if cur + 1.0 < prev:
            start_idx = i
    return records[start_idx:]

def _is_avoidance_behavior(record):
    if "behavior" in record:
        return record.get("behavior", 0) != 0
    active = record.get("avoidance_active")
    if active is not None:
        return bool(active)
    return False

def compute_route_return_status(
    run_records,
    *,
    lat0,
    lon0,
    init_lat,
    init_lon,
    init_hdg,
    xte_threshold_m=150.0,
    heading_threshold_deg=10.0,
    route_corridor_half_width_m=ROUTE_CORRIDOR_HALF_WIDTH_M,
    route_corridor_pass_limit_m=ROUTE_CORRIDOR_PASS_LIMIT_M,
    route_return_release_dwell_s=0.0,
):
    osh = [
        r for r in run_records
        if r.get("topic") == "/sil/own_ship_state" and
        _ownship_record_near_origin(r, lat0, lon0)
    ]
    bp = [r for r in run_records if r.get("topic") == "/l3/m4/behavior_plan"]

    final_behavior = bp[-1].get("behavior") if bp else None
    final_xte = float("nan")
    final_dev = float("nan")
    latest_sim_t = float("nan")
    max_route_xte = float("nan")

    init_x_m, init_y_m = _enu(init_lat, init_lon, lat0, lon0)
    if osh:
        route_xtes = []
        for r in osh:
            x_m, y_m = _enu(r["lat"], r["lon"], lat0, lon0)
            route_xtes.append(get_cross_track_error(x_m, y_m, init_x_m, init_y_m, init_hdg))
        if route_xtes:
            max_route_xte = max(route_xtes, key=lambda v: abs(v))

        latest = osh[-1]
        latest_sim_t = float(latest.get("sim_t", 0.0))
        final_x_m, final_y_m = _enu(latest["lat"], latest["lon"], lat0, lon0)
        final_xte = get_cross_track_error(final_x_m, final_y_m, init_x_m, init_y_m, init_hdg)
        final_dev = (latest["heading_deg"] - init_hdg + 180.0) % 360.0 - 180.0

    had_avoidance = any(_is_avoidance_behavior(r) for r in bp)
    last_release_time_s = None
    seen_avoidance = False
    for r in sorted(bp, key=lambda item: float(item.get("sim_t", 0.0))):
        if _is_avoidance_behavior(r):
            seen_avoidance = True
            last_release_time_s = None
        elif seen_avoidance and r.get("behavior") == 0 and last_release_time_s is None:
            last_release_time_s = float(r.get("sim_t", 0.0))

    transit_after_avoidance_s = 0.0
    if final_behavior == 0 and last_release_time_s is not None and not math.isnan(latest_sim_t):
        transit_after_avoidance_s = max(0.0, latest_sim_t - last_release_time_s)
    released_after_avoidance = (
        transit_after_avoidance_s >= float(route_return_release_dwell_s)
    )

    returned_to_route = (
        released_after_avoidance and
        not math.isnan(final_xte) and
        not math.isnan(final_dev) and
        abs(final_xte) < xte_threshold_m and
        abs(final_dev) < heading_threshold_deg and
        final_behavior == 0
    )
    route_corridor_violation = (
        not math.isnan(max_route_xte) and
        abs(max_route_xte) >= route_corridor_half_width_m
    )
    route_corridor_ok = (
        not math.isnan(max_route_xte) and
        abs(max_route_xte) <= route_corridor_pass_limit_m + ROUTE_CORRIDOR_PASS_EPS_M
    )

    return {
        "returned_to_route": bool(returned_to_route),
        "released_after_avoidance": bool(released_after_avoidance),
        "transit_after_avoidance_s": transit_after_avoidance_s,
        "had_avoidance": bool(had_avoidance),
        "final_behavior": final_behavior,
        "final_xte": final_xte,
        "final_heading_dev": final_dev,
        "latest_sim_t": latest_sim_t,
        "max_route_xte_m": max_route_xte,
        "route_corridor_half_width_m": route_corridor_half_width_m,
        "route_corridor_pass_limit_m": route_corridor_pass_limit_m,
        "route_corridor_pass_tolerance_m": ROUTE_CORRIDOR_PASS_EPS_M,
        "route_corridor_violation": bool(route_corridor_violation),
        "route_corridor_ok": bool(route_corridor_ok),
    }


# ── Phase-semantics gate (COLREGs 2018 Rules 8/13/14/15/16/17) ───────────
# Spec: docs/superpowers/specs/2026-06-17-colregs-phase-semantics-gate.md
# Each check maps to an A-level COLREG clause. Returns a dict of booleans +
# diagnostic metrics so failures are explainable (CCS auditability). The
# previous gate used only geometric KPIs (min CPA, heading_change) and could
# not detect "mechanical right turn then route return" which violates the
# phase semantics of Rule 8(d)/14(a)/15.

# Ample-time gate (Rule 8(a)/16). C-12 case law = action by 12 min before
# collision; FCB 18 kn service speed -> T_plan = 720 s (A-level).
PHASE_GATE_T_PLAN_S = 720.0
PHASE_GATE_T_EMERGENCY_S = 60.0
# Past-and-clear bearing threshold (Rule 13(b)/16: abaft the beam = >22.5°
# abaft beam = relative bearing > 112.5° from the bow).
PHASE_GATE_PAST_CLEAR_BEARING_DEG = 112.5
# Readily-apparent alteration (Rule 8(b); case law >= 30°).
PHASE_GATE_APPARENT_HEADING_DEG = 30.0
PHASE_GATE_SMALL_ALTER_DEG = 10.0


def _nav_heading_to_math_rad(nav_deg: float) -> float:
    return _nav_deg_to_math_rad(nav_deg)


def _relative_bearing_deg(own_hdg_deg: float, target_x: float, target_y: float,
                          own_x: float, own_y: float) -> float:
    """Relative bearing of target from own-ship bow, [-180, 180], starboard +.

    Both own heading and target line-of-sight bearing are nautical bearings
    (clockwise from north = atan2(E, N)), so the relative bearing is a plain
    difference. A mixed math/nav convention previously flipped the sign (a
    target on the port bow reported starboard).
    """
    dx = target_x - own_x
    dy = target_y - own_y
    target_bearing_deg = math.degrees(math.atan2(dx, dy))  # clockwise from N
    rel = target_bearing_deg - own_hdg_deg
    while rel > 180.0:
        rel -= 360.0
    while rel < -180.0:
        rel += 360.0
    return rel


def compute_phase_semantics(
    run_records,
    targets_meta,
    *,
    lat0,
    lon0,
    role,                 # "give_way" | "stand_on"
    rule,                 # e.g. "Rule14", "Rule15", "Rule17"...
    cpa_safe_m=1852.0,
    t_plan_s=PHASE_GATE_T_PLAN_S,
    t_act_s=240.0,
    t_emergency_s=PHASE_GATE_T_EMERGENCY_S,
) -> dict:
    """C1-C7 phase-semantics checks grounded in COLREGs 2018 clauses.

    Reconstructs own-ship + primary-target trajectories from run_records /
    targets_meta and evaluates whether the avoidance maneuver respected the
    per-phase rule requirements. Returns a dict with per-check booleans and
    diagnostic metrics. 'phase_semantics_ok' is the aggregate gate.
    """
    defaults = {
        "c1_past_clear_ok": True,
        "c2_apparent_action_ok": True,
        "c3_ample_time_ok": True,
        "c4_port_side_pass_ok": True,
        "c5_no_cross_ahead_ok": True,
        "c6_stand_on_hold_ok": True,
        "c7_overtake_past_clear_ok": True,
        "c8_give_way_avoided_ok": True,
        "phase_semantics_ok": True,
        "release_sim_t": float("nan"),
        "release_target_rel_bearing_deg": float("nan"),
        "onset_sim_t": float("nan"),
        "onset_tcpa_s": float("nan"),
        "max_single_heading_change_deg": 0.0,
        "small_alteration_run_count": 0,
        "cpa_moment_rel_bearing_deg": float("nan"),
        "cpa_moment_along_m": float("nan"),
        "evaluated": False,
        "note": "",
    }

    ownship = sorted(
        (r for r in run_records
         if r.get("topic") == "/sil/own_ship_state" and
         _ownship_record_near_origin(r, lat0, lon0)),
        key=lambda r: float(r.get("sim_t", 0.0)),
    )
    behavior = sorted(
        (r for r in run_records if r.get("topic") == "/l3/m4/behavior_plan"),
        key=lambda r: float(r.get("sim_t", 0.0)),
    )
    if not ownship or not behavior or not targets_meta:
        defaults["note"] = "insufficient data (ownship/behavior/targets)"
        return defaults
    defaults["evaluated"] = True

    target_meta = targets_meta[0]

    # Build synchronized trajectory: ownship (x,y,hdg) + target (x,y) + CPA/TCPA.
    traj = []
    own_by_t = {round(float(r.get("sim_t", 0.0)), 2): r for r in ownship}
    for r in ownship:
        sim_t = float(r.get("sim_t", 0.0))
        ox, oy = _enu(float(r["lat"]), float(r["lon"]), lat0, lon0)
        ohdg = float(r.get("heading_deg", 0.0))
        osog = float(r.get("sog_kn", 0.0))
        ovx, ovy = _velocity_components_mps(osog, ohdg)
        ts = _target_state_at(target_meta, sim_t, lat0, lon0)
        px = ts["x_m"] - ox
        py = ts["y_m"] - oy
        rvx = ts["vx_mps"] - ovx
        rvy = ts["vy_mps"] - ovy
        cpa_m, tcpa_s = _cpa_tcpa_m(px, py, rvx, rvy)
        rel_brg = _relative_bearing_deg(ohdg, ts["x_m"], ts["y_m"], ox, oy)
        traj.append({
            "sim_t": sim_t, "ox": ox, "oy": oy, "ohdg": ohdg,
            "tx": ts["x_m"], "ty": ts["y_m"], "tcog": ts["cog"],
            "cpa_m": cpa_m, "tcpa_s": tcpa_s, "rel_brg_deg": rel_brg,
            "range_m": math.hypot(px, py),
        })

    # Avoidance onset: first behavior != 0 after a TRANSIT run-in.
    onset_s = _avoidance_onset_s(run_records)
    defaults["onset_sim_t"] = onset_s if onset_s is not None else float("nan")

    # Avoidance release: the first sustained return to behavior==0 that is NOT
    # followed by any further avoidance. A momentary 0->avoid->0 blip (behavior
    # flapping near onset, seen as M4/M5 warm-state chatter in batch runs) must
    # not be mistaken for the encounter release -- it collapses C1/C2 to the
    # onset run-in geometry. So locate the LAST avoidance sample and take the
    # first behavior==0 strictly after it.
    release_s = None
    last_avoid_idx = -1
    for i, r in enumerate(behavior):
        if _is_avoidance_behavior(r):
            last_avoid_idx = i
    if last_avoid_idx >= 0:
        for r in behavior[last_avoid_idx + 1:]:
            if r.get("behavior") == 0:
                release_s = float(r.get("sim_t", 0.0))
                break
    defaults["release_sim_t"] = release_s if release_s is not None else float("nan")

    rule_l = str(rule).lower().replace(" ", "")

    # ── C1: Rule 8(d) finally past and clear before route return ─────────
    # At release time, target must be abaft own-ship's beam (relative bearing
    # magnitude > 112.5°) AND range opening AND CPA >= cpa_safe.
    c1_ok = True
    # C1 (abaft-beam past-and-clear) applies to head-on/crossing give-way; an
    # overtaking give-way is governed by C7 (own-ship ahead along target axis),
    # not by the 112.5-degree abaft-beam criterion which is meaningless when
    # both vessels are on near-parallel courses.
    if (release_s is not None and role == "give_way"
            and "rule13" not in rule_l):
        # Find trajectory sample closest to release time.
        rel_sample = min(traj, key=lambda p: abs(p["sim_t"] - release_s))
        rel_brg_abs = abs(rel_sample["rel_brg_deg"])
        defaults["release_target_rel_bearing_deg"] = rel_brg_abs
        # Range opening: compare to a sample ~5s before release.
        before = [p for p in traj if p["sim_t"] <= release_s - 3.0]
        range_opening = True
        if before and rel_sample["range_m"] > 0:
            range_opening = rel_sample["range_m"] >= before[-1]["range_m"] - 1.0
        c1_ok = (rel_brg_abs > PHASE_GATE_PAST_CLEAR_BEARING_DEG and
                 range_opening and rel_sample["cpa_m"] >= cpa_safe_m)
    defaults["c1_past_clear_ok"] = c1_ok

    # ── C2: Rule 8(b) readily apparent, no succession of small alterations
    # Max single-monotonic heading change >= 30°, and no run of >=3 small
    # (<10°) sign-flipping alterations.
    c2_ok = True
    if onset_s is not None:
        avoid_hdg = [p["ohdg"] for p in traj if p["sim_t"] >= onset_s and
                     (release_s is None or p["sim_t"] <= release_s + 5.0)]
        if len(avoid_hdg) >= 3:
            # Max monotonic excursion from onset heading.
            base = avoid_hdg[0]
            devs = [((h - base + 180.0) % 360.0) - 180.0 for h in avoid_hdg]
            max_dev = max(abs(d) for d in devs)
            defaults["max_single_heading_change_deg"] = max_dev
            # Count small sign-flipping runs.
            small_runs = 0
            run_len = 0
            prev_sign = 0
            for d in devs:
                if abs(d) < PHASE_GATE_SMALL_ALTER_DEG:
                    sign = 1 if d >= 0 else -1
                    if sign != prev_sign and prev_sign != 0:
                        run_len += 1
                    else:
                        run_len = max(run_len, 1)
                    prev_sign = sign
                else:
                    if run_len >= 3:
                        small_runs += 1
                    run_len = 0
                    prev_sign = 0
            if run_len >= 3:
                small_runs += 1
            defaults["small_alteration_run_count"] = small_runs
            # Rule 8(b) succession of small alterations: require a genuine
            # repeated turn/counter-turn pattern (>=3 small-alteration runs).
            # A single turn-recovery overshoot produces ~1 run from heading
            # noise around the target; requiring >=3 excludes that while still
            # catching real rudder chattering.
            c2_ok = max_dev >= PHASE_GATE_APPARENT_HEADING_DEG and small_runs < 3
    defaults["c2_apparent_action_ok"] = c2_ok

    # ── C3: Rule 8(a)/16 ample time — onset TCPA within (emergency, T_plan]
    c3_ok = True
    if onset_s is not None and role == "give_way":
        onset_sample = min(traj, key=lambda p: abs(p["sim_t"] - onset_s))
        onset_tcpa = onset_sample["tcpa_s"]
        defaults["onset_tcpa_s"] = onset_tcpa
        # Ample: TCPA at onset should be <= T_plan (acted early enough) but
        # > emergency (not last-second).
        c3_ok = (onset_tcpa <= t_plan_s and onset_tcpa > t_emergency_s)
    defaults["c3_ample_time_ok"] = c3_ok

    # ── C4: Rule 14(a) pass on the port side (head-on give-way) ──────────
    # COLREG 14: "each shall alter to starboard so that each shall pass on the
    # port side of the other." For a reciprocal head-on where both alter
    # starboard, at closest approach the target is on own-ship's PORT side
    # (rel_brg < 0, starboard-positive convention) -- own-ship has moved to the
    # target's port side and the target to own-ship's port side. The prior
    # check (>0) inverted this and flagged a correct port-to-port pass as RED.
    c4_ok = True
    if "rule14" in rule_l and role == "give_way":
        # Use the closest point of approach actually reached (min range), not
        # the predicted CPA: a reciprocal head-on predicts CPA~0 at every
        # pre-maneuver sample, so min(cpa_m) collapses to the onset run-in and
        # never sees the post-avoidance geometry.
        cpa_sample = min(traj, key=lambda p: p["range_m"])
        defaults["cpa_moment_rel_bearing_deg"] = cpa_sample["rel_brg_deg"]
        # Port-to-port: target ends up on own-ship's port side (rel_brg < 0).
        c4_ok = cpa_sample["rel_brg_deg"] < 0.0
    defaults["c4_port_side_pass_ok"] = c4_ok

    # ── C5: Rule 15 avoid crossing ahead (crossing give-way) ─────────────
    # At CPA moment, own-ship is abaft target (along target's course axis < 0)
    # i.e. own passed astern of target.
    c5_ok = True
    if "rule15" in rule_l and role == "give_way":
        # Closest approach actually reached (min range); see C4 note on why
        # predicted min(cpa_m) is wrong for an actively avoided encounter.
        cpa_sample = min(traj, key=lambda p: p["range_m"])
        # Project own-target vector onto target course axis.
        axis_e = math.sin(math.radians(cpa_sample["tcog"]))
        axis_n = math.cos(math.radians(cpa_sample["tcog"]))
        along = (cpa_sample["ox"] - cpa_sample["tx"]) * axis_e + \
                (cpa_sample["oy"] - cpa_sample["ty"]) * axis_n
        defaults["cpa_moment_along_m"] = along
        # along < 0 -> own-ship abaft target (passed astern, did not cross ahead).
        c5_ok = along < 0.0
    defaults["c5_no_cross_ahead_ok"] = c5_ok

    # ── C6: Rule 17 stand-on hold course/speed in stage 1/2 ──────────────
    # Stand-on must keep heading change < 5° before avoidance onset (stage 3).
    c6_ok = True
    if role == "stand_on" and onset_s is not None:
        pre = [p for p in traj if p["sim_t"] < onset_s]
        if pre:
            base = pre[0]["ohdg"]
            max_pre_dev = max(
                abs(((p["ohdg"] - base + 180.0) % 360.0) - 180.0) for p in pre)
            c6_ok = max_pre_dev < 5.0
    defaults["c6_stand_on_hold_ok"] = c6_ok

    # ── C7: Rule 13(d) overtaking finally past and clear ─────────────────
    # Overtaking give-way: at release, own-ship ahead of target along target
    # course axis (along > 0 + margin) AND range opening.
    c7_ok = True
    if "rule13" in rule_l and role == "give_way" and release_s is not None:
        rel_sample = min(traj, key=lambda p: abs(p["sim_t"] - release_s))
        axis_e = math.sin(math.radians(rel_sample["tcog"]))
        axis_n = math.cos(math.radians(rel_sample["tcog"]))
        along = (rel_sample["ox"] - rel_sample["tx"]) * axis_e + \
                (rel_sample["oy"] - rel_sample["ty"]) * axis_n
        before = [p for p in traj if p["sim_t"] <= release_s - 3.0]
        range_opening = True
        if before:
            range_opening = rel_sample["range_m"] >= before[-1]["range_m"] - 1.0
        # Past-and-clear for overtaking: own ahead of target (along > margin)
        # and opening.
        c7_ok = along > 0.0 and range_opening
    defaults["c7_overtake_past_clear_ok"] = c7_ok

    # ── C8: give-way must actually avoid (no silent no-avoidance pass) ──────
    # A give-way scenario that never onset an avoidance maneuver is a hard
    # RED regardless of the per-rule checks above: C2/C3/C4/C7 all gate on
    # onset_s/release_s and would otherwise stay at their default True and
    # mask a total decision failure. Stand-on legitimately may not act, so
    # the guard only applies to give_way.
    c8_ok = True
    if role == "give_way" and onset_s is None:
        c8_ok = False
    defaults["c8_give_way_avoided_ok"] = c8_ok

    defaults["phase_semantics_ok"] = all([
        defaults["c1_past_clear_ok"],
        defaults["c2_apparent_action_ok"],
        defaults["c3_ample_time_ok"],
        defaults["c4_port_side_pass_ok"],
        defaults["c5_no_cross_ahead_ok"],
        defaults["c6_stand_on_hold_ok"],
        defaults["c7_overtake_past_clear_ok"],
        defaults["c8_give_way_avoided_ok"],
    ])
    return defaults


def compute_overtake_status(
    run_records,
    targets_meta,
    *,
    lat0,
    lon0,
    required=False,
    along_margin_m=0.0,
):
    osh = [r for r in run_records if r.get("topic") == "/sil/own_ship_state"]
    base = {
        "overtake_required": bool(required),
        "overtake_completed": not required,
        "overtake_first_time_s": None,
        "final_own_minus_target_along_m": float("nan"),
        "max_own_minus_target_along_m": float("nan"),
    }
    if not required:
        return base
    if not osh or not targets_meta:
        base["overtake_completed"] = False
        return base

    target = targets_meta[0]
    tgt_e0, tgt_n0 = _enu(target["lat0"], target["lon0"], lat0, lon0)
    axis_e = math.sin(math.radians(target["cog"]))
    axis_n = math.cos(math.radians(target["cog"]))
    tgt_v_e = target["sog_kn"] * 0.514444 * axis_e
    tgt_v_n = target["sog_kn"] * 0.514444 * axis_n

    first_time = None
    final_along = float("nan")
    max_along = float("-inf")
    for r in osh:
        sim_t = float(r.get("sim_t", 0.0))
        own_e, own_n = _enu(r["lat"], r["lon"], lat0, lon0)
        tgt_e = tgt_e0 + tgt_v_e * sim_t
        tgt_n = tgt_n0 + tgt_v_n * sim_t
        along = (own_e - tgt_e) * axis_e + (own_n - tgt_n) * axis_n
        max_along = max(max_along, along)
        final_along = along
        if first_time is None and along >= along_margin_m:
            first_time = sim_t

    base["overtake_completed"] = first_time is not None
    base["overtake_first_time_s"] = first_time
    base["final_own_minus_target_along_m"] = final_along
    base["max_own_minus_target_along_m"] = max_along
    return base

def compute_overall_pass(*, cpa_ok, stability_pass, returned_to_route,
                         route_return_required=True,
                         route_corridor_ok=True,
                         overtake_required=False,
                         overtake_completed=False,
                         risk_gate_ok=True,
                         seamanship_gate_ok=True,
                         phase_semantics_ok=True):
    return bool(cpa_ok and stability_pass and
                ((not route_return_required) or returned_to_route) and
                route_corridor_ok and
                ((not overtake_required) or overtake_completed) and
                risk_gate_ok and seamanship_gate_ok and
                phase_semantics_ok)

def expected_cpa_floor_m(expected):
    cpa_acceptance = expected.get("cpa_acceptance") or {}
    profile = cpa_acceptance.get("profile")
    profile_floor = cpa_acceptance.get("threshold_m")
    legacy_floor = expected.get("cpa_min_m_ge")
    if profile:
        loa_m = float(cpa_acceptance.get("loa_m", 45.0))
        derived = derive_cpa_threshold(CpaProfile(str(profile)), loa_m=loa_m)
        if profile_floor is None:
            raise ValueError(
                "metadata.expected_outcome.cpa_acceptance.threshold_m "
                "is required when profile is set")
        profile_floor = float(profile_floor)
        if abs(profile_floor - derived.threshold_m) > 1.0:
            raise ValueError(
                "metadata.expected_outcome.cpa_acceptance.threshold_m "
                "must match length-scaled profile")
        if cpa_acceptance.get("threshold_formula") != derived.threshold_formula:
            raise ValueError(
                "metadata.expected_outcome.cpa_acceptance.threshold_formula "
                "must match length-scaled profile")
        if legacy_floor is not None and abs(float(legacy_floor) - derived.threshold_m) > 1.0:
            raise ValueError(
                "metadata.expected_outcome.cpa_min_m_ge "
                "must match cpa_acceptance threshold")
        return derived.threshold_m
    if profile_floor is not None:
        profile_floor = float(profile_floor)
        if legacy_floor is not None and abs(float(legacy_floor) - profile_floor) > 1e-6:
            raise ValueError(
                "metadata.expected_outcome.cpa_acceptance.threshold_m "
                "must match cpa_min_m_ge")
        return profile_floor
    if legacy_floor is not None:
        return float(legacy_floor)
    return DEFAULT_CPA_FLOOR_M


def cpa_floor_measurement_tolerance_m(expected):
    cpa_acceptance = expected.get("cpa_acceptance") or {}
    if not cpa_acceptance.get("profile"):
        return 0.0
    loa_m = float(cpa_acceptance.get("loa_m", 45.0))
    return loa_m * CPA_FLOOR_MEASUREMENT_TOLERANCE_LOA


def _restart_sil_nodes(container, settle_s):
    print(f"Restarting {container} before scenario; settle={settle_s:.1f}s")
    cp = subprocess.run(
        ["docker", "restart", container],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if cp.returncode != 0:
        print(cp.stdout)
        raise RuntimeError(f"failed to restart {container}")
    time.sleep(settle_s)

def run_scenario(scenario_id, total_time_override=None):
    print(f"\n==================================================")
    print(f"RUNNING SCENARIO: {scenario_id}")
    print(f"==================================================")
    
    # 1. Load scenario YAML to get total_time and metadata
    yaml_path = Path(f"scenarios/COLREGs测试/{scenario_id}.yaml")
    if not yaml_path.exists():
        print(f"Error: scenario file {yaml_path} does not exist.")
        return None
    
    with open(yaml_path) as f:
        scen_data = yaml.safe_load(f)
    
    sim_settings = scen_data.get("metadata", {}).get("simulation_settings", {})
    expected = scen_data.get("metadata", {}).get("expected_outcome", {})
    encounter = scen_data.get("metadata", {}).get("encounter", {})
    total_time = float(sim_settings.get("total_time", 600.0))
    if total_time_override is not None:
        total_time = float(total_time_override)
    coordinate_origin = sim_settings.get("coordinate_origin", [63.44, 10.38])
    lat0, lon0 = coordinate_origin[0], coordinate_origin[1]
    route_return_required = bool(expected.get("returned_to_route_required", False))
    route_return_xte_m = float(expected.get("route_return_xte_m_lt", 150.0))
    route_return_heading_deg = float(expected.get("route_return_heading_deg_lt", 10.0))
    route_corridor_half_width_m = float(
        expected.get("route_corridor_half_width_m", ROUTE_CORRIDOR_HALF_WIDTH_M))
    route_corridor_pass_limit_m = float(
        expected.get("route_corridor_pass_limit_m", ROUTE_CORRIDOR_PASS_LIMIT_M))
    overtake_required = bool(expected.get("overtake_required", False))
    overtake_along_margin_m = float(expected.get("overtake_along_margin_m", 0.0))
    
    own_init = scen_data["ownShip"]["initial"]
    init_lat = own_init["position"]["latitude"]
    init_lon = own_init["position"]["longitude"]
    init_hdg = float(own_init["heading"])
    init_sog = float(own_init["sog"])
    
    targets_meta = []
    for ts in scen_data.get("targetShips", []):
        ti = ts["initial"]
        t_lat = ti["position"]["latitude"]
        t_lon = ti["position"]["longitude"]
        t_cog = float(ti["cog"])
        t_sog = float(ti["sog"])
        targets_meta.append({
            "id": ts.get("id"),
            "static": ts.get("static", {}),
            "lat0": t_lat, "lon0": t_lon,
            "cog": t_cog, "sog_kn": t_sog
        })
    
    # 2. Cleanup and Configure
    req("POST", "/lifecycle/cleanup")
    time.sleep(2.0)
    cfg = configure_scenario(scenario_id)
    if not cfg.get("success"):
        print(f"Configure failed: {cfg.get('error')}")
        return None
    time.sleep(1.0)
    
    # 3. Activate and get Run ID
    act = req("POST", "/lifecycle/activate")
    if not act.get("success"):
        print(f"Activate failed: {act.get('error')}")
        return None
    run_id = act.get("run_id")
    print(f"Activated run_id: {run_id}")
    
    # Wait for active status
    for _ in range(15):
        if req("GET", "/lifecycle/status").get("current_state") == "active":
            break
        time.sleep(0.5)
        
    # 4. Set simulation rate
    req("POST", "/lifecycle/rate", {"rate": 10.0})
    override_tag = f" [override {total_time_override}]" if total_time_override is not None else ""
    print(f"Set rate to 10.0x. Simulation total time: {total_time}s{override_tag}")
    time.sleep(3.0) # Allow bridge to receive transition and truncate trace
    
    # 5. Poll until sim_t reaches total_time
    start_wall = time.time()
    last_sim_t = -1.0
    stuck_counter = 0
    
    while True:
        sim_t = get_sim_time()
        elapsed_wall = time.time() - start_wall
        print(f"  Wall time: {elapsed_wall:.1f}s | Sim time: {sim_t:.1f}s / {total_time:.1f}s", end="\r")
        
        if route_return_required:
            trace_records = read_trace_run_records()
            route_status = compute_route_return_status(
                trace_records,
                lat0=lat0, lon0=lon0,
                init_lat=init_lat, init_lon=init_lon,
                init_hdg=init_hdg,
                xte_threshold_m=route_return_xte_m,
                heading_threshold_deg=route_return_heading_deg,
                route_corridor_half_width_m=route_corridor_half_width_m,
                route_corridor_pass_limit_m=route_corridor_pass_limit_m,
                route_return_release_dwell_s=ROUTE_RETURN_RELEASE_DWELL_S,
            )
            overtake_status = compute_overtake_status(
                trace_records,
                targets_meta,
                lat0=lat0,
                lon0=lon0,
                required=overtake_required,
                along_margin_m=overtake_along_margin_m,
            )
            if (route_status["released_after_avoidance"] and
                    route_status["returned_to_route"] and
                    route_status["route_corridor_ok"] and
                    overtake_status["overtake_completed"]):
                print(
                    f"\n  Route return reached at sim_t={sim_t:.1f}s "
                    f"(XTE={route_status['final_xte']:.1f} m, "
                    f"heading_dev={route_status['final_heading_dev']:.1f}°, "
                    f"max_XTE={route_status['max_route_xte_m']:.1f} m)"
                )
                break

        if sim_t >= total_time - 2.0:
            print(f"\n  Simulation reached target time: {sim_t:.1f}s")
            break
            
        if elapsed_wall > (total_time / 10.0) + 60.0:
            print(f"\n  Timeout: simulation exceeded wall time limit.")
            break
            
        if abs(sim_t - last_sim_t) < 0.1:
            stuck_counter += 1
            if stuck_counter > 40: # ~20 seconds wall time with no sim progress
                print(f"\n  Warning: simulation appears to be stuck at sim_t = {sim_t:.1f}s")
                break
        else:
            stuck_counter = 0
            
        last_sim_t = sim_t
        time.sleep(0.5)
        
    # 6. Cleanup
    req("POST", "/lifecycle/cleanup")
    time.sleep(2.0)
    
    # Find the scoring.arrow file (which might have been created with a slightly different run_id)
    arrow_path = None
    arrows = sorted(Path("runs").glob("run-*/scoring.arrow"), key=lambda p: p.stat().st_mtime)
    if arrows and (time.time() - arrows[-1].stat().st_mtime < 120.0):
        arrow_path = arrows[-1]
        print(f"  Found scoring.arrow at: {arrow_path}")
        
    cpa_min_nm = float("nan")
    rule_compliance_score = float("nan")
    applicable_rules = []
    
    if arrow_path and arrow_path.exists():
        try:
            with pa.memory_map(str(arrow_path), 'r') as source:
                reader = ipc.open_file(source)
                table = reader.read_all()
            arrow_data = table.to_pylist()
            if arrow_data:
                cpas = [r["cpa_nm"] for r in arrow_data if r["cpa_nm"] is not None]
                if cpas:
                    cpa_min_nm = min(cpas)
                compliances = [r["rule_compliance"] for r in arrow_data if r["rule_compliance"] is not None]
                if compliances:
                    rule_compliance_score = sum(compliances) / len(compliances)
                rules = set(r["applicable_rule"] for r in arrow_data if r["applicable_rule"])
                applicable_rules = list(rules)
        except Exception as e:
            print(f"  Failed to read scoring.arrow: {e}")
            
    run_records = read_trace_run_records()
    
    # 8. Analyze Own Ship State
    osh = [r for r in run_records if r.get("topic") == "/sil/own_ship_state"]
    bp = [r for r in run_records if r.get("topic") == "/l3/m4/behavior_plan"]
    ap = [r for r in run_records if r.get("topic") == "/l3/m5/avoidance_plan"]
    veto = [r for r in run_records if r.get("topic") == "/l3/checker/veto"]
    
    print(f"  Telemetry records: own_ship={len(osh)}, behavior_plan={len(bp)}, avoidance_plan={len(ap)}, veto={len(veto)}")
    
    if not osh:
        print("  No own ship telemetry records found.")
        return None
        
    # Steering direction & magnitude
    deviations = []
    for r in osh:
        h = r["heading_deg"]
        dev = (h - init_hdg + 180.0) % 360.0 - 180.0
        deviations.append(dev)
        
    max_starboard = max([d for d in deviations if d >= 0], default=0.0)
    max_port = min([d for d in deviations if d <= 0], default=0.0)
    
    if abs(max_starboard) >= abs(max_port):
        steer_dir = "Starboard"
        steer_mag = max_starboard
    else:
        steer_dir = "Port"
        steer_mag = abs(max_port)
        
    # Rot smoothness
    rots = [abs(r["rot_deg_s"]) for r in osh if "rot_deg_s" in r]
    avg_rot_dpm = (sum(rots) / len(rots)) * 60.0 if rots else 0.0
    
    # Avoidance plan solver status counter
    solver_stats = {}
    for r in ap:
        status = r.get("solver_status", "UNKNOWN")
        solver_stats[status] = solver_stats.get(status, 0) + 1
        
    # Behavior active transitions
    bp_transitions = []
    prev_beh = None
    for r in bp:
        beh = r.get("behavior")
        if beh != prev_beh:
            bp_transitions.append((round(r["sim_t"], 1), beh))
            prev_beh = beh
            
    final_behavior = bp[-1].get("behavior") if bp else None
    
    route_status = compute_route_return_status(
        run_records,
        lat0=lat0, lon0=lon0,
        init_lat=init_lat, init_lon=init_lon,
        init_hdg=init_hdg,
        xte_threshold_m=route_return_xte_m,
        heading_threshold_deg=route_return_heading_deg,
        route_corridor_half_width_m=route_corridor_half_width_m,
        route_corridor_pass_limit_m=route_corridor_pass_limit_m,
        route_return_release_dwell_s=ROUTE_RETURN_RELEASE_DWELL_S,
    )
    final_xte = route_status["final_xte"]
    final_dev = route_status["final_heading_dev"]
    is_back_to_route = route_status["returned_to_route"]
    final_behavior = route_status["final_behavior"]
    overtake_status = compute_overtake_status(
        run_records,
        targets_meta,
        lat0=lat0,
        lon0=lon0,
        required=overtake_required,
        along_margin_m=overtake_along_margin_m,
    )
    
    # Determine rule_compliance status (full, partial, violated)
    # Using rule_compliance_evaluator logic
    compliance_verdict = "violated"
    if math.isnan(rule_compliance_score):
        compliance_verdict = "unknown"
    elif rule_compliance_score >= 0.95:
        compliance_verdict = "full"
    elif rule_compliance_score >= 0.45:
        compliance_verdict = "partial"
    else:
        compliance_verdict = "violated"
        
    # ── Phase B: behavioral-stability KPIs (fishtail / flap detector) ─────
    role = "give_way" if encounter.get("give_way_vessel") == "own" else "stand_on"
    stability_thresholds = expected.get("stability_thresholds")  # optional override
    stability = ss.analyze_stability(
        run_records, role=role, init_heading_deg=init_hdg,
        thresholds=stability_thresholds)

    avoidance_starboard = float(stability["kpis"].get("max_starboard_dev_deg") or 0.0)
    avoidance_port = float(stability["kpis"].get("max_port_dev_deg") or 0.0)
    if avoidance_starboard >= avoidance_port:
        steer_dir = "Starboard"
        steer_mag = avoidance_starboard
    else:
        steer_dir = "Port"
        steer_mag = avoidance_port

    # ── Overall verdict: CPA floor AND behavioral stability ───────────────
    route_distance_m = _route_distance_m(scen_data, lat0, lon0)
    risk_metrics = compute_risk_metrics(
        run_records,
        targets_meta,
        lat0=lat0,
        lon0=lon0,
        encounter=encounter,
    )
    seamanship_metrics = compute_seamanship_metrics(
        run_records,
        lat0=lat0,
        lon0=lon0,
        init_lat=init_lat,
        init_lon=init_lon,
        init_hdg=init_hdg,
        route_distance_m=route_distance_m,
    )
    domain_gates = compute_domain_gate_status(
        risk_metrics,
        seamanship_metrics,
        close_start_emergency_allowed=_close_start_emergency_allowed(expected),
        single_target=(len(targets_meta) <= 1),
        integrated_abs_xte_limit_m_s=route_corridor_pass_limit_m * 600.0,
    )
    min_dcpa_m = cpa_min_nm * 1852.0 if not math.isnan(cpa_min_nm) else float("nan")
    cpa_floor_m = expected_cpa_floor_m(expected)
    cpa_floor_tolerance_m = cpa_floor_measurement_tolerance_m(expected)
    cpa_ok = (
        (not math.isnan(min_dcpa_m))
        and (min_dcpa_m + cpa_floor_tolerance_m >= cpa_floor_m)
    )
    # ── Phase-semantics gate (COLREGs 2018 Rules 8/13/14/15/16/17) ──────
    # Detects phase violations the geometric KPIs miss: premature route
    # return before past-and-clear (Rule 8(d)), mechanical small-alteration
    # turns (Rule 8(b)), late action (Rule 16), wrong passing side (Rule 14),
    # crossing ahead (Rule 15), stand-on fidgeting (Rule 17).
    rule_str = str(encounter.get("rule", ""))
    phase_sem = compute_phase_semantics(
        run_records,
        targets_meta,
        lat0=lat0, lon0=lon0,
        role=role,
        rule=rule_str,
        cpa_safe_m=cpa_floor_m,
    )
    overall_pass = compute_overall_pass(
        cpa_ok=cpa_ok,
        stability_pass=stability["stability_pass"],
        returned_to_route=is_back_to_route,
        route_return_required=route_return_required,
        route_corridor_ok=route_status["route_corridor_ok"],
        overtake_required=overtake_required,
        overtake_completed=overtake_status["overtake_completed"],
        risk_gate_ok=domain_gates["risk_gate_ok"],
        seamanship_gate_ok=domain_gates["seamanship_gate_ok"],
        phase_semantics_ok=phase_sem["phase_semantics_ok"],
    )

    print(f"  Min DCPA: {min_dcpa_m:.1f} m ({cpa_min_nm:.3f} NM)")
    print(f"  Avoidance Steer Direction: {steer_dir} | Magnitude: {steer_mag:.1f}°")
    print(f"  Rule Compliance Score: {rule_compliance_score:.2f} ({compliance_verdict})")
    print(f"  Avg ROT: {avg_rot_dpm:.2f} dpm")
    print(f"  Final Behavior: {final_behavior} | Final XTE: {final_xte:.1f} m | Final Heading Dev: {final_dev:.1f}°")
    print(f"  Returned to Route: {is_back_to_route} "
          f"(required={route_return_required}, XTE<{route_return_xte_m:.0f} m, "
          f"heading<{route_return_heading_deg:.0f}°)")
    print(f"  Max XTE: {route_status['max_route_xte_m']:.1f} m / "
          f"limit {route_corridor_pass_limit_m:.0f} m / "
          f"hard {route_corridor_half_width_m:.0f} m")
    if overtake_required:
        print(f"  Overtake Completed: {overtake_status['overtake_completed']} "
              f"(final along={overtake_status['final_own_minus_target_along_m']:.1f} m)")
    print(f"  Veto events count: {len(veto)}")
    print(f"  Behavior transitions: {bp_transitions}")
    print(f"  M5 Solver states: {solver_stats}")
    # Phase-semantics diagnostics (COLREGs clause-mapped)
    print(f"  Phase Gate: {phase_sem['phase_semantics_ok']} "
          f"(C1 past-clear={phase_sem['c1_past_clear_ok']} "
          f"rel_brg={phase_sem['release_target_rel_bearing_deg']:.0f}°, "
          f"C2 apparent={phase_sem['c2_apparent_action_ok']} "
          f"max_dev={phase_sem['max_single_heading_change_deg']:.0f}° "
          f"small_runs={phase_sem['small_alteration_run_count']}, "
          f"C3 ample={phase_sem['c3_ample_time_ok']} "
          f"onset_tcpa={phase_sem['onset_tcpa_s']:.0f}s, "
          f"C4 port-side={phase_sem['c4_port_side_pass_ok']} "
          f"cpa_rel_brg={phase_sem['cpa_moment_rel_bearing_deg']:.0f}°, "
          f"C5 no-cross={phase_sem['c5_no_cross_ahead_ok']} "
          f"along={phase_sem['cpa_moment_along_m']:.0f}m, "
          f"C6 standon-hold={phase_sem['c6_stand_on_hold_ok']}, "
          f"C7 overtake-past={phase_sem['c7_overtake_past_clear_ok']}, "
          f"C8 give-way-avoided={phase_sem['c8_give_way_avoided_ok']})")
    print(f"  Role: {role} | CPA floor: {cpa_floor_m:.0f} m | CPA pass: {cpa_ok}")
    print(f"  Risk Gate: {domain_gates['risk_gate_ok']} "
          f"(primary={risk_metrics['primary_threat_id'] or 'none'}, "
          f"max_score={risk_metrics['max_risk_score']:.3f}, "
          f"warning_exp={risk_metrics['warning_domain_exposure_s']:.1f}s, "
          f"danger_exp={risk_metrics['danger_domain_exposure_s']:.1f}s, "
          f"recovery={risk_metrics['risk_recovery_ok']})")
    print(f"  Seamanship Gate: {domain_gates['seamanship_gate_ok']} "
          f"(int_abs_xte={seamanship_metrics['integrated_abs_xte_m_s']:.1f} m*s, "
          f"crossings={seamanship_metrics['route_crossing_overshoot_count']}, "
          f"path_ratio={seamanship_metrics['path_length_ratio']:.2f})")
    print(ss.format_report(stability))
    print(f"  ===> OVERALL: {'PASS' if overall_pass else 'RED'} "
          f"(cpa_ok={cpa_ok} AND stability={stability['stability_pass']} "
          f"AND route_return={is_back_to_route if route_return_required else 'n/a'} "
          f"AND corridor={route_status['route_corridor_ok']} "
          f"AND overtake={overtake_status['overtake_completed'] if overtake_required else 'n/a'} "
          f"AND risk_gate={domain_gates['risk_gate_ok']} "
          f"AND seamanship_gate={domain_gates['seamanship_gate_ok']})")
    
    # 9. Plot trajectories and save to run directory
    try:
        import matplotlib.pyplot as plt

        plt.figure(figsize=(10, 8))
        # Plot own ship
        os_e = []
        os_n = []
        for r in osh:
            e, n = _enu(r["lat"], r["lon"], lat0, lon0)
            os_e.append(e)
            os_n.append(n)
        plt.plot(os_e, os_n, "b-", label="Own Ship (OS)")
        plt.plot(os_e[0], os_n[0], "go", label="OS Start")
        plt.plot(os_e[-1], os_n[-1], "bo", label="OS End")
        
        # Plot target ships analytically
        sim_times = [r["sim_t"] for r in osh]
        for t_idx, tgt in enumerate(targets_meta):
            t_e0, t_n0 = _enu(tgt["lat0"], tgt["lon0"], lat0, lon0)
            t_vx = tgt["sog_kn"] * 0.514444 * math.sin(math.radians(tgt["cog"]))
            t_vy = tgt["sog_kn"] * 0.514444 * math.cos(math.radians(tgt["cog"]))
            
            tgt_e = [t_e0 + t_vx * t for t in sim_times]
            tgt_n = [t_n0 + t_vy * t for t in sim_times]
            
            plt.plot(tgt_e, tgt_n, "r--", label=f"Target {t_idx+1} (TS{t_idx+1})")
            plt.plot(tgt_e[0], tgt_n[0], "ro")
            plt.plot(tgt_e[-1], tgt_n[-1], "rx")
            
        plt.grid(True)
        plt.axis("equal")
        plt.title(f"Trajectory for {scenario_id} ({run_id})")
        plt.xlabel("East (m)")
        plt.ylabel("North (m)")
        plt.legend()
        plot_path = Path("runs") / f"{scenario_id}_trajectory.png"
        plt.savefig(plot_path)
        plt.close()
        print(f"  Saved trajectory plot: {plot_path}")
    except Exception as e:
        print(f"  Failed to generate trajectory plot: {e}")
        
    return {
        "scenario_id": scenario_id,
        "run_id": run_id,
        "min_cpa_m": min_dcpa_m,
        "min_cpa_nm": cpa_min_nm,
        "steer_dir": steer_dir,
        "steer_mag": steer_mag,
        "compliance_score": rule_compliance_score,
        "compliance_verdict": compliance_verdict,
        "applicable_rules": applicable_rules,
        "avg_rot_dpm": avg_rot_dpm,
        "final_xte": final_xte,
        "final_heading_dev": final_dev,
        "returned_to_route": is_back_to_route,
        "route_return_required": route_return_required,
        "route_return_xte_m_lt": route_return_xte_m,
        "route_return_heading_deg_lt": route_return_heading_deg,
        "route_return_release_dwell_s": ROUTE_RETURN_RELEASE_DWELL_S,
        "transit_after_avoidance_s": route_status["transit_after_avoidance_s"],
        "max_route_xte_m": route_status["max_route_xte_m"],
        "route_corridor_half_width_m": route_corridor_half_width_m,
        "route_corridor_pass_limit_m": route_corridor_pass_limit_m,
        "route_corridor_violation": route_status["route_corridor_violation"],
        "route_corridor_ok": route_status["route_corridor_ok"],
        "overtake_required": overtake_required,
        "overtake_completed": overtake_status["overtake_completed"],
        "overtake_first_time_s": overtake_status["overtake_first_time_s"],
        "final_own_minus_target_along_m": overtake_status["final_own_minus_target_along_m"],
        "bp_transitions": bp_transitions,
        "solver_stats": solver_stats,
        "veto_count": len(veto),
        "role": role,
        "cpa_floor_m": cpa_floor_m,
        "cpa_floor_measurement_tolerance_m": cpa_floor_tolerance_m,
        "cpa_ok": cpa_ok,
        "stability_pass": stability["stability_pass"],
        "stability_kpis": stability["kpis"],
        "stability_checks": stability["checks"],
        "primary_threat_id": risk_metrics["primary_threat_id"],
        "primary_threat_switches": risk_metrics["primary_threat_switches"],
        "max_risk_score": risk_metrics["max_risk_score"],
        "worst_warning_margin_m": risk_metrics["worst_warning_margin_m"],
        "worst_danger_margin_m": risk_metrics["worst_danger_margin_m"],
        "max_warning_ddv": risk_metrics["max_warning_ddv"],
        "max_danger_ddv": risk_metrics["max_danger_ddv"],
        "warning_domain_exposure_s": risk_metrics["warning_domain_exposure_s"],
        "danger_domain_exposure_s": risk_metrics["danger_domain_exposure_s"],
        "risk_recovery_ok": risk_metrics["risk_recovery_ok"],
        "risk_trace": risk_metrics["risk_trace"],
        "integrated_abs_xte_m_s": seamanship_metrics["integrated_abs_xte_m_s"],
        "route_crossing_overshoot_count": seamanship_metrics["route_crossing_overshoot_count"],
        "path_length_ratio": seamanship_metrics["path_length_ratio"],
        "domain_gates": domain_gates,
        "overall_pass": overall_pass,
        "phase_semantics": phase_sem,
        "plot_path": str(plot_path) if 'plot_path' in locals() else None
    }

def _load_expected_outcome(scenario_id):
    yaml_path = Path(f"scenarios/COLREGs测试/{scenario_id}.yaml")
    with open(yaml_path) as f:
        return yaml.safe_load(f).get("metadata", {}).get("expected_outcome", {})


def _archive_trace_artifact(
    scenario_id,
    trace_report_dir,
    *,
    trace_path=Path("runs/trace_current.jsonl"),
):
    if not trace_report_dir:
        return None
    source = Path(trace_path)
    if not source.exists():
        return None
    report_dir = Path(trace_report_dir)
    report_dir.mkdir(parents=True, exist_ok=True)
    artifact_path = report_dir / f"{scenario_id}.trace_current.jsonl"
    shutil.copyfile(source, artifact_path)
    return str(artifact_path)


def _write_trace_evaluation_report(scenario_id, result, trace_report_dir):
    if not trace_report_dir:
        return None
    report_dir = Path(trace_report_dir)
    report_dir.mkdir(parents=True, exist_ok=True)
    trace_artifact_path = (
        _archive_trace_artifact(scenario_id, report_dir)
        or "runs/trace_current.jsonl"
    )
    report = report_from_runner_result(
        scenario_id=scenario_id,
        expected_outcome=_load_expected_outcome(scenario_id),
        result=result,
        trace_artifact_path=trace_artifact_path,
        no_action_trace_path=None,
    )
    report_path = report_dir / f"{scenario_id}.json"
    with open(report_path, "w") as f:
        json.dump(report.to_json_dict(), f, indent=2)
    return str(report_path)


def _parse_args(argv=None):
    parser = argparse.ArgumentParser(description="Run COLREGs clean 8-probe scenarios.")
    parser.add_argument("--scenario", choices=SCENARIOS, action="append",
                        help="Run one scenario. Repeat to run multiple scenarios.")
    parser.add_argument("--list", action="store_true", help="List clean 8 scenarios and exit.")
    parser.add_argument("--summary-out", default="runs/batch_colregs_results.json",
                        help="Path for batch summary JSON.")
    parser.add_argument("--trace-report-dir", default=None,
                        help="Directory for per-scenario TraceEvaluationReport JSON.")
    parser.add_argument("--restart-between-runs", action="store_true",
                        help="Restart sil-nodes before every scenario to prevent warm-state leakage.")
    parser.add_argument("--restart-container", default=DEFAULT_RESTART_CONTAINER,
                        help="Container restarted when --restart-between-runs is set.")
    parser.add_argument("--restart-settle", type=float, default=DEFAULT_RESTART_SETTLE_S,
                        help="Seconds to wait after each sil-nodes restart.")
    parser.add_argument("--total-time-override", type=float, default=None,
                        help="Override the YAML simulation_settings.total_time horizon for "
                             "all selected scenarios (seconds). Diagnostic only; does not "
                             "modify scenario YAML.")
    parser.add_argument("--deprecated-wrapper", action="store_true", help=argparse.SUPPRESS)
    return parser.parse_args(argv)


def main(argv=None):
    args = _parse_args(argv)
    if args.deprecated_wrapper:
        print(
            "run_6_scenarios.py is deprecated; use "
            "scripts/run_colregs_clean_8probe.py for clean 8-probe.",
            file=sys.stderr,
        )
    if args.list:
        for scen in SCENARIOS:
            print(scen)
        return 0

    results = {}
    scenarios = args.scenario or SCENARIOS
    for scen in scenarios:
        try:
            if args.restart_between_runs:
                _restart_sil_nodes(args.restart_container, args.restart_settle)
            res = run_scenario(scen, total_time_override=args.total_time_override)
            if res:
                report_path = _write_trace_evaluation_report(
                    scen, res, args.trace_report_dir)
                if report_path:
                    res["trace_evaluation_report_path"] = report_path
                    report_data = json.loads(Path(report_path).read_text())
                    verdict = report_data["verdict"]
                    res["safety_pass"] = verdict["safety_pass"]
                    res["mission_pass"] = verdict["mission_pass"]
                    res["colregs_pass"] = verdict["colregs_pass"]
                    res["traceeval_stability_pass"] = verdict["stability_pass"]
                    res["traceeval_overall_pass"] = verdict["overall_pass"]
                results[scen] = res
        except Exception as e:
            print(f"Failed to run {scen}: {e}")
            
    # Save results to a json file
    summary_path = Path(args.summary_out)
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    with open(summary_path, "w") as f:
        json.dump(results, f, indent=2)
        
    print("\n\n==================================================")
    print("ALL SCENARIOS COMPLETED. SUMMARY OF RESULTS:")
    print("==================================================")
    n_pass = sum(1 for r in results.values() if r.get("overall_pass"))
    print(f"OVERALL: {n_pass}/{len(results)} PASS "
          f"(CPA floor AND behavioral stability AND required route return "
          f"AND corridor AND risk/seamanship gates)\n")
    for scen, res in results.items():
        verdict = "PASS" if res.get("overall_pass") else "RED"
        print(f"\n[{verdict}] {scen} ({res['run_id']}) — role={res.get('role')}")
        print(f"  CPA min: {res['min_cpa_m']:.1f} m (floor {res.get('cpa_floor_m', 0):.0f}, ok={res.get('cpa_ok')}) | Steering: {res['steer_dir']} ({res['steer_mag']:.1f}°)")
        print(f"  Stability: {res.get('stability_pass')} | KPIs: {res.get('stability_kpis')}")
        red = [k for k, c in res.get("stability_checks", {}).items()
               if c["applicable"] and not c["pass"]]
        if red:
            print(f"  Stability RED checks: {red}")
        print(f"  Returned to Route: {res['returned_to_route']} "
              f"(required={res.get('route_return_required')}, "
              f"Final XTE: {res['final_xte']:.1f} m)")
        print(f"  Max XTE: {res.get('max_route_xte_m', float('nan')):.1f} m / "
              f"limit {res.get('route_corridor_pass_limit_m', ROUTE_CORRIDOR_PASS_LIMIT_M):.0f} m / "
              f"hard {res.get('route_corridor_half_width_m', ROUTE_CORRIDOR_HALF_WIDTH_M):.0f} m")
        if res.get("overtake_required"):
            print(f"  Overtake Completed: {res.get('overtake_completed')} "
                  f"(final along={res.get('final_own_minus_target_along_m', float('nan')):.1f} m)")
        print(f"  Transitions: {res['bp_transitions']}")
    return 0
        
if __name__ == "__main__":
    raise SystemExit(main(["--deprecated-wrapper", *sys.argv[1:]]))
