#!/usr/bin/env python3
import json
import os
import ssl
import sys
import time
import math
import urllib.request
from pathlib import Path
import pyarrow as pa
import pyarrow.ipc as ipc
import yaml
import matplotlib.pyplot as plt

# Phase B: behavioral-stability scorer — standalone import (no polars/ROS2) so
# this host-side runner can use the same logic the scoring package ships.
sys.path.insert(0, str(Path(__file__).resolve().parents[1] /
                       "src/sim_workbench/sil_nodes/scoring/scoring"))
import stability_scorer as ss  # noqa: E402

BASE = "https://127.0.0.1:18000/api/v1"
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
    "safe_route-left-encounter",
]

ROUTE_CORRIDOR_HALF_WIDTH_M = 1000.0
ROUTE_CORRIDOR_PASS_LIMIT_M = 900.0

def req(method, path, body=None, timeout=30):
    data = json.dumps(body).encode() if body is not None else None
    r = urllib.request.Request(
        BASE + path, data=data, method=method,
        headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(r, context=CTX, timeout=timeout) as resp:
        return json.loads(resp.read().decode())

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
):
    osh = [r for r in run_records if r.get("topic") == "/sil/own_ship_state"]
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
    released_after_avoidance = False
    seen_avoidance = False
    for r in bp:
        if _is_avoidance_behavior(r):
            seen_avoidance = True
        elif seen_avoidance and r.get("behavior") == 0:
            released_after_avoidance = True

    returned_to_route = (
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
        abs(max_route_xte) < route_corridor_pass_limit_m
    )

    return {
        "returned_to_route": bool(returned_to_route),
        "released_after_avoidance": bool(released_after_avoidance),
        "had_avoidance": bool(had_avoidance),
        "final_behavior": final_behavior,
        "final_xte": final_xte,
        "final_heading_dev": final_dev,
        "latest_sim_t": latest_sim_t,
        "max_route_xte_m": max_route_xte,
        "route_corridor_half_width_m": route_corridor_half_width_m,
        "route_corridor_pass_limit_m": route_corridor_pass_limit_m,
        "route_corridor_violation": bool(route_corridor_violation),
        "route_corridor_ok": bool(route_corridor_ok),
    }

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
                         overtake_completed=False):
    return bool(cpa_ok and stability_pass and
                ((not route_return_required) or returned_to_route) and
                route_corridor_ok and
                ((not overtake_required) or overtake_completed))

def run_scenario(scenario_id):
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
    total_time = float(sim_settings.get("total_time", 600.0))
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
            "lat0": t_lat, "lon0": t_lon,
            "cog": t_cog, "sog_kn": t_sog
        })
    
    # 2. Cleanup and Configure
    req("POST", "/lifecycle/cleanup")
    time.sleep(2.0)
    cfg = req("POST", "/lifecycle/configure", {"scenario_id": scenario_id})
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
    print(f"Set rate to 10.0x. Simulation total time: {total_time}s")
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
    encounter = scen_data.get("metadata", {}).get("encounter", {})
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
    min_dcpa_m = cpa_min_nm * 1852.0 if not math.isnan(cpa_min_nm) else float("nan")
    cpa_floor_m = float(expected.get("cpa_min_m_ge", 0.0))
    cpa_ok = (not math.isnan(min_dcpa_m)) and (min_dcpa_m >= cpa_floor_m)
    overall_pass = compute_overall_pass(
        cpa_ok=cpa_ok,
        stability_pass=stability["stability_pass"],
        returned_to_route=is_back_to_route,
        route_return_required=route_return_required,
        route_corridor_ok=route_status["route_corridor_ok"],
        overtake_required=overtake_required,
        overtake_completed=overtake_status["overtake_completed"],
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
    print(f"  Role: {role} | CPA floor: {cpa_floor_m:.0f} m | CPA pass: {cpa_ok}")
    print(ss.format_report(stability))
    print(f"  ===> OVERALL: {'PASS' if overall_pass else 'RED'} "
          f"(cpa_ok={cpa_ok} AND stability={stability['stability_pass']} "
          f"AND route_return={is_back_to_route if route_return_required else 'n/a'} "
          f"AND corridor={route_status['route_corridor_ok']} "
          f"AND overtake={overtake_status['overtake_completed'] if overtake_required else 'n/a'})")
    
    # 9. Plot trajectories and save to run directory
    try:
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
        "cpa_ok": cpa_ok,
        "stability_pass": stability["stability_pass"],
        "stability_kpis": stability["kpis"],
        "stability_checks": stability["checks"],
        "overall_pass": overall_pass,
        "plot_path": str(plot_path) if 'plot_path' in locals() else None
    }

def main():
    results = {}
    for scen in SCENARIOS:
        try:
            res = run_scenario(scen)
            if res:
                results[scen] = res
        except Exception as e:
            print(f"Failed to run {scen}: {e}")
            
    # Save results to a json file
    with open("runs/batch_colregs_results.json", "w") as f:
        json.dump(results, f, indent=2)
        
    print("\n\n==================================================")
    print("ALL SCENARIOS COMPLETED. SUMMARY OF RESULTS:")
    print("==================================================")
    n_pass = sum(1 for r in results.values() if r.get("overall_pass"))
    print(f"OVERALL: {n_pass}/{len(results)} PASS "
          f"(CPA floor AND behavioral stability AND required route return AND corridor)\n")
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
        
if __name__ == "__main__":
    main()
