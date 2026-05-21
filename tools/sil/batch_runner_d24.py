"""D2.4 50-scenario batch runner — geometric simulation + HagenScorer verdict."""
from __future__ import annotations
import argparse, csv, math, sys, time
from dataclasses import dataclass
from pathlib import Path
import yaml

REPO_ROOT = Path(__file__).parents[2]
sys.path.insert(0, str(REPO_ROOT / "src/sim_workbench/sil_nodes/scoring"))
from scoring.hagen_scorer import HagenScorer

@dataclass
class ScenarioBatchResult:
    scenario_id: str; source: str; rule: str; odd_domain: str
    pass_fail: bool; quality_score: float; min_cpa_nm: float
    wall_clock_s: float; notes: str = ""

def _haversine_nm(lat1, lon1, lat2, lon2):
    R = 3440.065
    dlat = math.radians(lat2 - lat1); dlon = math.radians(lon2 - lon1)
    a = math.sin(dlat/2)**2 + math.cos(math.radians(lat1))*math.cos(math.radians(lat2))*math.sin(dlon/2)**2
    return 2*R*math.atan2(math.sqrt(a), math.sqrt(1-a))

def _simulate_geometric(os_lat, os_lon, os_cog, os_sog, ts_lat, ts_lon, ts_cog, ts_sog,
                        total_time_s=600.0, dt=1.0, rudder_deg=45.0, av_time=300.0):
    """Simplified geometric CPA simulation. Returns (min_cpa_nm, frames)."""
    def _to_xy(lat, lon, lat0, lon0):
        x = (lon - lon0) * math.cos(math.radians(lat0)) * 111120.0
        y = (lat - lat0) * 111120.0
        return x, y
    lat0, lon0 = os_lat, os_lon
    ox, oy = 0.0, 0.0
    tx, ty = _to_xy(ts_lat, ts_lon, lat0, lon0)
    osog = os_sog * 0.514444; tsog = ts_sog * 0.514444
    ocog = math.radians(os_cog); tcog = math.radians(ts_cog)
    min_dist_m = float("inf"); frames = []; action_taken = False
    for step in range(int(total_time_s / dt)):
        t = step * dt
        dist = math.sqrt((tx-ox)**2 + (ty-oy)**2)
        if dist < min_dist_m: min_dist_m = dist
        cpa_nm = dist / 1852.0
        rule_state = "full" if dist > 500.0 else "partial" if dist > 200.0 else "violated"
        frames.append({"stamp": t, "cpa_nm": cpa_nm, "rudder_deg": rudder_deg if action_taken else 0.0,
                        "behavior_phase": "give_way" if action_taken else "transit"})
        if rudder_deg > 0 and t >= av_time and not action_taken:
            ocog = math.radians(os_cog + rudder_deg); action_taken = True
        ox += osog * math.sin(ocog) * dt; oy += osog * math.cos(ocog) * dt
        tx += tsog * math.sin(tcog) * dt; ty += tsog * math.cos(tcog) * dt
    return min_dist_m / 1852.0, frames

def run_scenario_yaml(yaml_path: Path, rule: str, source: str) -> ScenarioBatchResult:
    data = yaml.safe_load(yaml_path.read_text())
    meta = data.get("metadata", {})
    sid = meta.get("scenario_id", yaml_path.stem)
    odd_domain = meta.get("odd_cell", {}).get("domain", "open_sea_offshore_wind_farm")
    sim_settings = meta.get("simulation_settings", {})
    total_time = float(sim_settings.get("total_time", 600.0))
    encounter = meta.get("encounter", {})
    av_time = float(encounter.get("avoidance_time_s", 300.0))
    av_delta_rad = float(encounter.get("avoidance_delta_rad", 0.6109))
    rudder = math.degrees(av_delta_rad)  # 0 stays 0 for maintain-course scenarios
    os_init = data["ownShip"]["initial"]; ts_list = data.get("targetShips", [])
    if not ts_list:
        return ScenarioBatchResult(sid, source, rule, odd_domain, False, 0.0, 0.0, 0.0, "no target")
    ts_init = ts_list[0]["initial"]
    os_lat = os_init["position"]["latitude"]; os_lon = os_init["position"]["longitude"]
    os_cog = float(os_init.get("cog", 0.0)); os_sog = float(os_init.get("sog", 10.0))
    ts_lat = ts_init["position"]["latitude"]; ts_lon = ts_init["position"]["longitude"]
    ts_cog = float(ts_init.get("cog", 180.0)); ts_sog = float(ts_init.get("sog", 10.0))
    t0 = time.perf_counter()
    min_cpa_nm, frames = _simulate_geometric(os_lat, os_lon, os_cog, os_sog,
        ts_lat, ts_lon, ts_cog, ts_sog, total_time, rudder_deg=rudder, av_time=av_time)
    wall_clock = time.perf_counter() - t0
    scorer = HagenScorer(cpa_target_nm=0.27)
    for f in frames:
        try:
            scorer.score_frame(own_lat=os_lat, own_lon=os_lon, own_heading=0.0, own_sog=os_sog,
                targets=[(ts_lat, ts_lon, ts_cog, ts_sog)], rule_states={"Rule14": f.get("rule_state","full")},
                t_action_s=f["stamp"], t_target_action_s=300.0, rudder_deg=f["rudder_deg"],
                turning_rate_dps=2.0, behavior_phase=f["behavior_phase"],
                trajectory_curvature=0.0, trajectory_accel_ms2=0.0, applicable_rule=rule)
        except Exception:
            pass
    verdict = scorer.compute_verdict(min_cpa_nm, {rule: "full" if min_cpa_nm >= 0.27 else "violated"})
    _, quality = scorer.get_quality_score()
    return ScenarioBatchResult(sid, source, rule, odd_domain, verdict, quality, min_cpa_nm, wall_clock)

def run_batch(output_dir: Path) -> list[ScenarioBatchResult]:
    output_dir.mkdir(parents=True, exist_ok=True)
    results: list[ScenarioBatchResult] = []

    # 22 Imazu from IMAZU标准测试/
    imazu_dir = REPO_ROOT / "scenarios/IMAZU标准测试"
    imazu_rules = {"ho": "Rule14", "cr-gw": "Rule15", "cr-so": "Rule15", "ot": "Rule13", "ms": "Rule14"}
    for yf in sorted(imazu_dir.glob("imazu-*.yaml")):
        stem = yf.stem
        parts = stem.split("-")
        if len(parts) >= 3:
            enc_type = parts[2]
            rule = imazu_rules.get(enc_type, "Rule14")
        else:
            rule = "Rule14"
        r = run_scenario_yaml(yf, rule, "imazu22")
        print(f"  {'PASS' if r.pass_fail else 'FAIL'} {r.scenario_id:<40} CPA={r.min_cpa_nm:.3f}NM")
        results.append(r)

    # 5 OU
    ou_dir = REPO_ROOT / "scenarios/ou_mode"
    ou_rules = {"head_on": "Rule14", "crossing_give_way": "Rule15", "crossing_stand_on": "Rule15",
                "overtaking": "Rule13", "restricted_vis": "Rule19"}
    for yf in sorted(ou_dir.glob("ou_*.yaml")):
        stem = yf.stem
        rule = "Rule14"
        for key, r in ou_rules.items():
            if key in stem: rule = r; break
        r = run_scenario_yaml(yf, rule, "ou_mode_d2.4")
        print(f"  {'PASS' if r.pass_fail else 'FAIL'} {r.scenario_id:<40} CPA={r.min_cpa_nm:.3f}NM")
        results.append(r)

    # 5 AIS
    ais_dir = REPO_ROOT / "scenarios/ais_derived"
    for yf in sorted(ais_dir.glob("ais-*.yaml")):
        meta = yaml.safe_load(yf.read_text()).get("metadata", {})
        rule = meta.get("encounter", {}).get("rule", "Rule14").replace("_Stbd","").replace("_Port","")
        r = run_scenario_yaml(yf, rule, "ais_derived")
        print(f"  {'PASS' if r.pass_fail else 'FAIL'} {r.scenario_id:<40} CPA={r.min_cpa_nm:.3f}NM")
        results.append(r)

    # 18 synthetic
    synth_dir = REPO_ROOT / "scenarios/synthetic"
    for yf in sorted(synth_dir.glob("synth_*.yaml")):
        meta = yaml.safe_load(yf.read_text()).get("metadata", {})
        rule = meta.get("encounter", {}).get("rule", "Rule5")
        r = run_scenario_yaml(yf, rule, "synthetic_d2.4")
        print(f"  {'PASS' if r.pass_fail else 'FAIL'} {r.scenario_id:<40} CPA={r.min_cpa_nm:.3f}NM")
        results.append(r)

    # Write CSV
    csv_path = output_dir / "pass_fail_report.csv"
    with open(csv_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=[
            "scenario_id","source","rule","odd_domain","pass_fail","quality_score","min_cpa_nm","wall_clock_s","notes"])
        writer.writeheader()
        for r in results:
            writer.writerow({"scenario_id":r.scenario_id,"source":r.source,"rule":r.rule,
                "odd_domain":r.odd_domain,"pass_fail":r.pass_fail,
                "quality_score":round(r.quality_score,4),"min_cpa_nm":round(r.min_cpa_nm,4),
                "wall_clock_s":round(r.wall_clock_s,4),"notes":r.notes})

    # Write Arrow
    import pyarrow as pa, pyarrow.ipc as ipc
    arrow_path = output_dir / "batch_scoring_summary.arrow"
    schema = pa.schema([pa.field("scenario_id",pa.string()),pa.field("source",pa.string()),
        pa.field("rule",pa.string()),pa.field("odd_domain",pa.string()),
        pa.field("pass_fail",pa.bool_()),pa.field("quality_score",pa.float32()),
        pa.field("min_cpa_nm",pa.float32()),pa.field("wall_clock_s",pa.float32())])
    rows = [{"scenario_id":r.scenario_id,"source":r.source,"rule":r.rule,"odd_domain":r.odd_domain,
        "pass_fail":r.pass_fail,"quality_score":r.quality_score,"min_cpa_nm":r.min_cpa_nm,
        "wall_clock_s":r.wall_clock_s} for r in results]
    batch = pa.RecordBatch.from_pylist(rows, schema=schema)
    with pa.OSFile(str(arrow_path), "wb") as sink:
        with ipc.new_file(sink, schema) as writer: writer.write_batch(batch)

    total = len(results); passed = sum(1 for r in results if r.pass_fail)
    pct = 100.0*passed/total if total else 0.0
    print(f"\n{'='*60}")
    print(f"D2.4 Batch Results: {passed}/{total} PASS ({pct:.1f}%)")
    print(f"CSV:   {csv_path}")
    print(f"Arrow: {arrow_path}")
    if pct < 90.0: print(f"  ⚠ BELOW 90% THRESHOLD — analyse failures before DoD")
    else: print(f"  ✓ Passes first-run ≥90% gate")
    return results

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path,
        default=Path("docs/Design/Phase 2/D2.4-m6-colregs-6d-scoring/evidence"))
    args = parser.parse_args()
    run_batch(args.output)
