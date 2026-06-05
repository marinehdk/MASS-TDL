#!/usr/bin/env python3
import json
import os
import math
from pathlib import Path
import pyarrow as pa
import pyarrow.ipc as ipc
import yaml

SCENARIOS = [
    "colreg-rule17-cr-so",
    "colreg-rule17-cr-so-2",
    "colreg-rule14-ho-port",
    "colreg-rule15-ms",
    "colreg-rule13-15-ms",
    "colreg-ms-headon-cross"
]

def _enu(lat, lon, lat0, lon0):
    e = (lon - lon0) * 111120.0 * math.cos(math.radians(lat0))
    n = (lat - lat0) * 111120.0
    return e, n

def get_cross_track_error(x, y, x0, y0, hdg_deg):
    theta = math.radians(hdg_deg)
    return (x - x0) * math.cos(theta) - (y - y0) * math.sin(theta)

def main():
    runs_dir = Path("runs")
    if not runs_dir.exists():
        print("Error: runs directory not found.")
        return

    # 1. Group run directories by type
    orch_runs = []
    score_runs = []
    for d in runs_dir.iterdir():
        if d.is_dir() and d.name.startswith("run-"):
            try:
                ts = int(d.name.split("-")[1], 16)
            except Exception:
                continue
            
            if (d / "scenario.yaml").exists():
                orch_runs.append((ts, d))
            elif (d / "scoring.arrow").exists():
                score_runs.append((ts, d))

    print(f"Found {len(orch_runs)} orchestrator runs and {len(score_runs)} scoring runs.")

    # 2. Match orchestrator runs to scoring runs
    matched_scenarios = {}
    for o_ts, o_dir in sorted(orch_runs):
        with open(o_dir / "scenario.yaml") as f:
            scen_data = yaml.safe_load(f)
        scen_id = scen_data.get("metadata", {}).get("scenario_id")
        # Strip version suffix
        scen_base = scen_id.replace("-001-v1.0", "").replace("-002-v1.0", "")
        
        # Only analyze the scenarios we care about
        if scen_base not in SCENARIOS:
            continue

        # Find closest scoring run by timestamp
        best_score_dir = None
        min_diff = float("inf")
        for s_ts, s_dir in score_runs:
            diff = abs(o_ts - s_ts)
            if diff < min_diff:
                min_diff = diff
                best_score_dir = s_dir

        # Match must be within 10 seconds (10000 ms)
        if best_score_dir and min_diff < 10000:
            # If multiple runs for same scenario, keep the latest one
            matched_scenarios[scen_base] = {
                "orch_dir": o_dir,
                "score_dir": best_score_dir,
                "scenario_yaml": scen_data,
                "ts": o_ts
            }

    print(f"Matched {len(matched_scenarios)} scenarios for analysis.")

    results = {}
    for scen_base, info in matched_scenarios.items():
        print(f"\nAnalyzing {scen_base}...")
        scen_data = info["scenario_yaml"]
        score_dir = info["score_dir"]
        orch_dir = info["orch_dir"]

        sim_settings = scen_data.get("metadata", {}).get("simulation_settings", {})
        coordinate_origin = sim_settings.get("coordinate_origin", [63.44, 10.38])
        lat0, lon0 = coordinate_origin[0], coordinate_origin[1]
        
        own_init = scen_data["ownShip"]["initial"]
        init_lat = own_init["position"]["latitude"]
        init_lon = own_init["position"]["longitude"]
        init_hdg = float(own_init["heading"])
        init_sog = float(own_init["sog"])

        # Read scoring.arrow
        cpa_min_nm = float("nan")
        rule_compliance_score = float("nan")
        applicable_rules = []
        arrow_path = score_dir / "scoring.arrow"
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

        # We can read trace from the global trace file since it gets truncated on ACTIVE,
        # but wait: we can also find the trace file inside the run directory if it was copied,
        # or we can read the latest runs/trace_current.jsonl if this scenario was run recently.
        # Actually, let's look at trace_current.jsonl and filter by the timestamps or the hex run_id!
        # Wait, the bridge logs msg to trace writer, does it include the run_id?
        # In main.py, it doesn't log run_id to trace, but the trace file is truncated.
        # If we run analyze_runs right after the batch, trace_current.jsonl might be overwritten by the last scenario.
        # Wait! Is trace_current.jsonl copied to the run directory?
        # Let's check: does main.py or bridge copy the trace file to the run directory on cleanup?
        # Let's check main.py or bridge logic.
        # In bridge node:
        # self._trace_path = run_dir / "trace_current.jsonl"
        # Ah! In bridge node __init__:
        # self._trace_path = run_dir / "trace_current.jsonl" (which is in the run directory!)
        # Wait, let's verify if the run directory has trace_current.jsonl or trace.jsonl!
        # Let's check the files inside score_dir or orch_dir.
        # Earlier, ls -R showed:
        # runs/run-19e91f0ff36 has only: preflight, scenario.sha256, scenario.yaml.
        # Wait, what about the bridge trace?
        # Let's check if the bridge trace file is written to runs/trace_current.jsonl on the host.
        # Yes, self._trace_path = run_dir / "trace_current.jsonl" inside the bridge container!
        # And in docker-compose.yml:
        # - ./runs:/var/sil/runs
        # And in sil_topic_bridge.py:
        # self._trace_writer = DebugTraceWriter(node=self)
        # In DebugTraceWriter:
        # self._trace_path = run_dir / "trace_current.jsonl"
        # So trace_current.jsonl is written directly to runs/trace_current.jsonl!
        # But wait! Does it get saved per run directory?
        # No, it's just runs/trace_current.jsonl.
        # However, the global trace file runs/trace_current.jsonl contains records from ALL runs if it wasn't truncated,
        # but it gets truncated on ACTIVE!
        # So if we run the script, trace_current.jsonl will only contain the trace for the LAST scenario run!
        # Wait, is that true?
        # Let's check: does the bridge close and truncate the trace file on ACTIVE?
        # Yes:
        # "The bridge (sil_topic_bridge.py DebugTraceWriter) owns the trace file handle and truncates on ACTIVE."
        # This means runs/trace_current.jsonl is indeed truncated at the start of each scenario,
        # so when the batch finishes, runs/trace_current.jsonl only contains the trace of the LAST scenario.
        # Wait, then where are the traces of the previous scenarios?
        # Are they lost?
        # Let's check if there are other files in the run directories.
        # What about `runs/run-{run_id}/`?
        # Wait! Let's check if there is a `.jsonl` file in the run directories.
        # Let's check `runs/run-19e95738f7a` (the scoring directory) or the orch directory.
        # Let's run a search for any jsonl files on A4000.
        # `find runs/ -name "*.jsonl"`
        # Let's run this.
