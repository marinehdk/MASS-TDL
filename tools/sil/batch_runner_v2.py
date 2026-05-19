"""D1.3b batch scenario runner -- v2.0 (multi-dir, Arrow output, 32 scenarios).

Usage:
    python tools/sil/batch_runner_v2.py --output reports/

TODO(salvaged-from-d1.3b.1): This v2 runner was built against the Cerberus v2.0
flat-snake_case YAML schema (obsolete; main migrated to DNV maritime-schema
v0.2.x / schema_version 3.0 in commit eb053a0). Before activating, re-align with:
  - scenarios at scenarios/IMAZU标准测试/ (not scenarios/imazu22/)
  - ScenarioSpec from current scenario_spec.py (DNV camelCase nested static/initial)
  - simulate.TRAJECTORY_SAMPLE_INTERVAL must still exist
  - encounter.rule field name (DNV may use different path)
Kept verbatim to preserve Arrow output + multi-dir + progress_cb + trajectory
sampling features not present in main batch_runner.py (161 LOC).
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from collections.abc import Callable
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

import pyarrow as pa
import pyarrow.ipc as ipc
import yaml

SCRIPT_DIR = Path(__file__).parent
sys.path.insert(0, str(SCRIPT_DIR))

from scenario_spec import ScenarioSpec
from simulate import simulate, TRAJECTORY_SAMPLE_INTERVAL


SUMMARY_SCHEMA = pa.schema([
    pa.field("scenario_id",        pa.string()),
    pa.field("rule",               pa.string()),
    pa.field("odd_zone",           pa.string()),
    pa.field("dcpa_no_action_m",   pa.float32()),
    pa.field("dcpa_with_action_m", pa.float32()),
    pa.field("min_separation_m",   pa.float32()),
    pa.field("pass",               pa.bool_()),
    pa.field("duration_s",         pa.float32()),
    pa.field("wall_clock_s",       pa.float32()),
    pa.field("scenario_source",    pa.string()),
])

TRAJECTORY_SCHEMA = pa.schema([
    pa.field("scenario_id", pa.string()),
    pa.field("t_s",         pa.float32()),
    pa.field("os_x_m",      pa.float32()),
    pa.field("os_y_m",      pa.float32()),
    pa.field("ts1_x_m",     pa.float32()),
    pa.field("ts1_y_m",     pa.float32()),
    pa.field("ts2_x_m",     pa.float32()),   # null if < 2 targets
    pa.field("ts2_y_m",     pa.float32()),
    pa.field("ts3_x_m",     pa.float32()),   # null if < 3 targets
    pa.field("ts3_y_m",     pa.float32()),
])

# Map scenario_id prefix → (rule label, sector_start, sector_end, from_target_perspective)
SCENARIO_RULE_MAP: dict[str, tuple[str, float, float, bool]] = {
    "colreg-rule14-ho-001": ("Rule 14 Head-on", 345.0, 15.0, False),
    "colreg-rule14-ho-002": ("Rule 14 Head-on", 345.0, 15.0, False),
    "colreg-rule14-ho-003": ("Rule 14 Head-on", 345.0, 15.0, False),
    "colreg-rule15-cs-001": ("Rule 15/16 Stbd", 15.0, 112.5, False),
    "colreg-rule15-cs-002": ("Rule 15/16 Stbd", 15.0, 112.5, False),
    "colreg-rule15-cs-003": ("Rule 15/17 Port", 247.5, 345.0, False),
    "colreg-rule15-cs-004": ("Rule 15/16 Stbd", 15.0, 112.5, False),
    "colreg-rule13-ot-001": ("Rule 13 Overtaking", 112.5, 247.5, True),
    "colreg-rule13-ot-002": ("Rule 13 Overtaking", 112.5, 247.5, True),
    "colreg-rule13-ot-003": ("Rule 13 Overtaking", 112.5, 247.5, True),
    # Imazu single-ship
    "imazu-01-ho": ("Rule 14 Head-on", 345.0, 15.0, False),
    "imazu-02-cr-gw": ("Rule 15/16 Stbd", 15.0, 112.5, False),
    "imazu-03-ot": ("Rule 13 Overtaking", 112.5, 247.5, True),
    "imazu-04-cr-so": ("Rule 15/17 Port", 247.5, 345.0, False),
    # Imazu multi-ship: bearing check not applicable (multiple targets)
}


def discover_scenarios(dirs: list[Path]) -> list[Path]:
    """Glob all *.yaml from dirs, excluding schema.yaml. Returns sorted list."""
    results = []
    for d in dirs:
        if not d.exists():
            continue
        results.extend(sorted(f for f in d.glob("*.yaml") if f.name != "schema.yaml"))
    return results


def _bearing_deg(fx: float, fy: float, tx: float, ty: float) -> float:
    return math.degrees(math.atan2(tx - fx, ty - fy)) % 360.0


def _in_sector(b: float, start: float, end: float) -> bool:
    if start <= end:
        return start <= b <= end
    return b >= start or b <= end


def _check_bearing(spec: ScenarioSpec, rule_info: tuple[str, float, float, bool]) -> bool:
    _, sector_start, sector_end, from_target = rule_info
    own = spec.initial_conditions.own_ship
    if not spec.initial_conditions.targets:
        return True
    tgt = spec.initial_conditions.targets[0]
    if from_target:
        b = _bearing_deg(tgt.x_m, tgt.y_m, own.x_m, own.y_m)
    else:
        b = _bearing_deg(own.x_m, own.y_m, tgt.x_m, tgt.y_m)
    return _in_sector(b, sector_start, sector_end)


def _infer_rule_label(spec: ScenarioSpec) -> str:
    """Infer COLREGs rule label from encounter metadata or scenario_id pattern."""
    if spec.encounter is not None and spec.encounter.rule:
        rule = spec.encounter.rule
        n_targets = len(spec.initial_conditions.targets)
        if rule == "Rule14" and n_targets > 1:
            return "Multi-Ship"
        if rule == "Rule14":
            return "Rule 14 Head-on"
        if rule == "Rule15":
            if n_targets > 1:
                return "Multi-Ship"
            return "Rule 15/16 Stbd"
        if rule == "Rule13":
            return "Rule 13 Overtaking"
        return rule
    sid = spec.scenario_id
    if "ho-" in sid:
        return "Rule 14 Head-on"
    if "ms-" in sid or "-ms-" in sid:
        return "Multi-Ship"
    if "cr-so" in sid:
        return "Rule 15/17 Port"
    if "cs-003" in sid:
        return "Rule 15/17 Port"
    if "cs-" in sid or "cr-" in sid:
        return "Rule 15/16 Stbd"
    if "ot-" in sid:
        return "Rule 13 Overtaking"
    return "Unknown"


def _build_trajectory_rows(
    spec: ScenarioSpec,
    result_wa,
) -> list[dict]:
    """Build Arrow trajectory rows from SimResult sampled trajectories."""
    dt = spec.simulation.dt_s
    rows = []
    for i, (ox, oy) in enumerate(result_wa.own_trajectory_sampled):
        t_sim = i * TRAJECTORY_SAMPLE_INTERVAL * dt
        row: dict = {
            "scenario_id": spec.scenario_id,
            "t_s": float(t_sim),
            "os_x_m": float(ox),
            "os_y_m": float(oy),
        }
        tgt_trajs = result_wa.target_trajectories_sampled
        for j in range(3):
            key_x = f"ts{j+1}_x_m"
            key_y = f"ts{j+1}_y_m"
            if j < len(tgt_trajs) and i < len(tgt_trajs[j]):
                row[key_x] = float(tgt_trajs[j][i][0])
                row[key_y] = float(tgt_trajs[j][i][1])
            else:
                row[key_x] = None
                row[key_y] = None
        rows.append(row)
    return rows


def _write_arrow_summary(rows: list[dict], path: Path) -> None:
    table = pa.table(
        {f.name: [r.get(f.name) for r in rows] for f in SUMMARY_SCHEMA},
        schema=SUMMARY_SCHEMA,
    )
    with pa.OSFile(str(path), "wb") as sink:
        with ipc.new_file(sink, SUMMARY_SCHEMA) as writer:
            writer.write_table(table)


def _write_arrow_trajectories(all_traj_rows: list[dict], path: Path) -> None:
    table = pa.table(
        {f.name: [r.get(f.name) for r in all_traj_rows] for f in TRAJECTORY_SCHEMA},
        schema=TRAJECTORY_SCHEMA,
    )
    with pa.OSFile(str(path), "wb") as sink:
        with ipc.new_file(sink, TRAJECTORY_SCHEMA) as writer:
            writer.write_table(table)


def run_batch(
    scenario_dirs: list[Path],
    output_dir: Path,
    geo_origin: Optional[tuple[float, float]] = None,
    progress_cb: Optional[Callable[[int, int], None]] = None,
) -> dict:
    """Run all scenarios from scenario_dirs. Returns batch summary dict."""
    output_dir.mkdir(parents=True, exist_ok=True)
    scenarios = discover_scenarios(scenario_dirs)
    print(f"Found {len(scenarios)} scenario files")

    results = []
    all_traj_rows: list[dict] = []

    for i, yaml_path in enumerate(scenarios):
        spec = ScenarioSpec.from_file(yaml_path, geo_origin=geo_origin)
        sid = spec.scenario_id
        sid_key = sid.replace("-v1.0", "")
        rule_info = SCENARIO_RULE_MAP.get(sid_key)
        rule_label = _infer_rule_label(spec)

        print(f"  [{i+1}/{len(scenarios)}] Running {sid}...")

        result_na = simulate(spec, apply_avoidance=False)
        no_action_dcpa = result_na.dcpa_m

        result_wa = simulate(spec, apply_avoidance=True)

        geometric_pass = (
            spec.pass_criteria is None
            or no_action_dcpa < spec.pass_criteria.max_dcpa_no_action_m
        )
        bearing_pass = _check_bearing(spec, rule_info) if rule_info is not None else True

        out_json = result_wa.to_json_dict(
            spec,
            no_action_dcpa_m=no_action_dcpa,
            yaml_path=str(yaml_path),
            geometric_pass=geometric_pass,
            bearing_pass=bearing_pass,
        )

        ts = datetime.now(tz=timezone.utc).strftime("%Y%m%dT%H%M%S")
        json_filename = f"{sid_key}-{ts}.json"
        json_path = output_dir / json_filename
        json_path.write_text(json.dumps(out_json, indent=2))

        out_json["_json_filename"] = json_filename
        out_json["_rule_label"] = rule_label

        _summary_row = {
            "scenario_id": sid,
            "rule": rule_label,
            "odd_zone": spec.odd_zone,
            "dcpa_no_action_m": float(no_action_dcpa),
            "dcpa_with_action_m": float(result_wa.dcpa_m),
            "min_separation_m": float(result_wa.dcpa_m),
            "pass": out_json["result"] == "PASS",
            "duration_s": float(spec.simulation.duration_s),
            "wall_clock_s": float(out_json["performance"]["wall_clock_s"]),
            "scenario_source": out_json.get("scenario_source", "fcb_original"),
        }
        out_json["_summary_row"] = _summary_row

        traj_rows = _build_trajectory_rows(spec, result_wa)
        all_traj_rows.extend(traj_rows)
        results.append(out_json)

        wc = out_json["performance"]["wall_clock_s"]
        print(f"    {out_json['result']} | DCPA_na={no_action_dcpa:.1f}m | wall={wc:.3f}s")

        if progress_cb:
            progress_cb(i + 1, len(scenarios))

    summary = {
        "batch_timestamp": datetime.now(tz=timezone.utc).isoformat(),
        "total": len(results),
        "passed": sum(1 for r in results if r["result"] == "PASS"),
        "failed": sum(1 for r in results if r["result"] == "FAIL"),
        "max_wall_clock_s": max((r["performance"]["wall_clock_s"] for r in results), default=0.0),
        "scenarios": results,
    }
    (output_dir / "batch_results.json").write_text(json.dumps(summary, indent=2))

    summary_rows = [r["_summary_row"] for r in results]
    _write_arrow_summary(summary_rows, output_dir / "batch_summary.arrow")
    _write_arrow_trajectories(all_traj_rows, output_dir / "batch_trajectories.arrow")

    print(f"\nPassed: {summary['passed']}/{summary['total']}")
    print(f"batch_summary.arrow: {len(summary_rows)} rows")
    print(f"batch_trajectories.arrow: {len(all_traj_rows)} rows")

    return {"n_scenarios": len(scenarios), "n_pass": summary["passed"]}


def main() -> None:
    parser = argparse.ArgumentParser(description="D1.3b batch scenario runner v2.0")
    parser.add_argument(
        "--scenarios-dirs",
        type=Path,
        nargs="+",
        default=[Path("scenarios/colregs"), Path("scenarios/imazu22")],
    )
    parser.add_argument("--output", type=Path, default=Path("reports"))
    args = parser.parse_args()

    import coverage_reporter
    run_batch(args.scenarios_dirs, args.output)
    ts = datetime.now(tz=timezone.utc).strftime("%Y%m%d")
    html_path = args.output / f"coverage_report_{ts}.html"
    coverage_reporter.generate_report(args.output, html_path)
    print(f"HTML report: {html_path}")


if __name__ == "__main__":
    main()
