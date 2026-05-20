"""D1.3b batch scenario runner.

Usage:
    python tools/sil/batch_runner.py \
        --scenarios scenarios/colregs/ \
        --output reports/

Runs each YAML scenario twice (no-action / with-action), writes per-scenario JSON,
then generates the HTML coverage report.
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from datetime import datetime, timezone
from pathlib import Path

import yaml

SCRIPT_DIR = Path(__file__).parent
sys.path.insert(0, str(SCRIPT_DIR))

import time

import pyarrow as pa

from scenario_spec import ScenarioSpec
from simulate import simulate
from coverage_cube import CoverageCube, seed_index_from_filename


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
}


def _bearing_deg(fx: float, fy: float, tx: float, ty: float) -> float:
    return math.degrees(math.atan2(tx - fx, ty - fy)) % 360.0


def _in_sector(b: float, start: float, end: float) -> bool:
    if start <= end:
        return start <= b <= end
    return b >= start or b <= end


def _check_bearing(spec: ScenarioSpec, rule_info: tuple[str, float, float, bool]) -> bool:
    _, sector_start, sector_end, from_target = rule_info
    own = spec.initial_conditions.own_ship
    tgt = spec.initial_conditions.targets[0]
    if from_target:
        b = _bearing_deg(tgt.x_m, tgt.y_m, own.x_m, own.y_m)
    else:
        b = _bearing_deg(own.x_m, own.y_m, tgt.x_m, tgt.y_m)
    return _in_sector(b, sector_start, sector_end)


def run_batch(scenarios_dir: Path, output_dir: Path) -> list[dict]:
    output_dir.mkdir(parents=True, exist_ok=True)
    results = []
    cube = CoverageCube()

    yaml_files = sorted(f for f in scenarios_dir.glob("*.yaml") if f.name != "schema.yaml")
    print(f"Found {len(yaml_files)} scenario files in {scenarios_dir}")

    for yaml_path in yaml_files:
        spec = ScenarioSpec.model_validate(yaml.safe_load(yaml_path.read_text()))
        sid = spec.scenario_id.replace("-v1.0", "")
        rule_info = SCENARIO_RULE_MAP.get(sid)
        cube.mark(
            rule=spec.encounter.rule,
            odd_zone="open_sea",  # v1.0 colregs scenarios have no ODD metadata
            wind_kn=spec.disturbance_model.wind_kn,
            vis_m=spec.disturbance_model.vis_m,
            seed_index=seed_index_from_filename(yaml_path.stem),
        )

        print(f"  Running {spec.scenario_id}...")

        # Pass 1: no-action
        result_na = simulate(spec, apply_avoidance=False)
        no_action_dcpa = result_na.dcpa_m

        result_wa = simulate(spec, apply_avoidance=True)

        geometric_pass = no_action_dcpa < spec.pass_criteria.max_dcpa_no_action_m
        if rule_info is None:
            print(f"  WARNING: {sid} not in SCENARIO_RULE_MAP — bearing check skipped")
            bearing_pass = False
        else:
            bearing_pass = _check_bearing(spec, rule_info)

        out_json = result_wa.to_json_dict(
            spec,
            no_action_dcpa_m=no_action_dcpa,
            yaml_path=str(yaml_path),
            geometric_pass=geometric_pass,
            bearing_pass=bearing_pass,
        )

        ts = datetime.now(tz=timezone.utc).strftime("%Y%m%dT%H%M%S")
        json_filename = f"{sid}-{ts}.json"
        json_path = output_dir / json_filename
        json_path.write_text(json.dumps(out_json, indent=2))

        out_json["_json_filename"] = json_filename
        out_json["_rule_label"] = rule_info[0] if rule_info else "Unknown"
        results.append(out_json)

        status = out_json["result"]
        wc = out_json["performance"]["wall_clock_s"]
        print(f"    {status} | DCPA_na={no_action_dcpa:.1f}m | DCPA_wa={result_wa.dcpa_m:.1f}m | wall={wc:.3f}s")

    # Write batch summary JSON
    summary = {
        "batch_timestamp": datetime.now(tz=timezone.utc).isoformat(),
        "total": len(results),
        "passed": sum(1 for r in results if r["result"] == "PASS"),
        "failed": sum(1 for r in results if r["result"] == "FAIL"),
        "max_wall_clock_s": max((r["performance"]["wall_clock_s"] for r in results), default=0.0),
        "scenarios": results,
    }
    summary_path = output_dir / "batch_results.json"
    summary_path.write_text(json.dumps(summary, indent=2))
    print(f"\nBatch summary written to {summary_path}")
    print(f"Passed: {summary['passed']}/{summary['total']}, max wall-clock: {summary['max_wall_clock_s']:.3f}s")

    # E1.4 coverage cube
    Path("test-results").mkdir(exist_ok=True)
    cube_path = Path("test-results") / "coverage_cube.json"
    cube_path.write_text(json.dumps(cube.to_json_dict(), indent=2))
    n_lit = cube.cells_lit()
    print(f"Coverage cube: {n_lit}/{cube.to_json_dict()['total_cells']} cells lit → {cube_path}")

    return results


# ---------------------------------------------------------------------------
# v2 — Multi-dir discovery + Arrow dual output (D1.3.2.1)
# ---------------------------------------------------------------------------

SUMMARY_SCHEMA = pa.schema([
    pa.field("scenario_id", pa.string()),
    pa.field("pass", pa.bool_()),
    pa.field("dcpa_m", pa.float32()),
    pa.field("wall_clock_s", pa.float32()),
    pa.field("duration_s", pa.float32()),
])

TRAJECTORY_POINT_SCHEMA = pa.schema([
    pa.field("scenario_id", pa.string()),
    pa.field("t_s", pa.float64()),
    pa.field("x_m", pa.float64()),
    pa.field("y_m", pa.float64()),
    pa.field("psi_rad", pa.float32()),
    pa.field("u_mps", pa.float32()),
    pa.field("v_mps", pa.float32()),
    pa.field("r_radps", pa.float32()),
])


def discover_scenarios(dirs=None):
    """Discover scenario YAML files from multiple directories.

    Default directories: scenarios/IMAZU标准测试, scenarios/COLREGs测试.

    Args:
        dirs: List of directory paths to search. Defaults to the two
              standard scenario directories.

    Returns:
        Sorted list of ``Path`` objects for all ``*.yaml`` files,
        excluding schema files.
    """
    if dirs is None:
        dirs = [
            Path("scenarios/IMAZU标准测试"),
            Path("scenarios/COLREGs测试"),
        ]

    files: list[Path] = []
    for d in dirs:
        dp = Path(d)
        if dp.exists():
            files.extend(
                f for f in sorted(dp.glob("*.yaml")) if f.name != "schema.yaml"
            )

    return sorted(files, key=lambda p: p.stem)


def _extract_trajectory(result, scenario_id, max_points=200):
    """Extract sampled trajectory points from a simulation result.

    Returns a list of dicts matching *TRAJECTORY_POINT_SCHEMA*.
    """
    rows = []
    traj = getattr(result, "trajectory", None)
    if traj is None:
        traj = getattr(result, "waypoints", None)
    if traj is None:
        traj = getattr(result, "path", None)

    if traj:
        step = max(1, len(traj) // max_points)
        for point in traj[::step]:
            rows.append({
                "scenario_id": scenario_id,
                "t_s": float(
                    getattr(point, "t_s", getattr(point, "time", 0.0))
                ),
                "x_m": float(
                    getattr(point, "x_m", getattr(point, "x", 0.0))
                ),
                "y_m": float(
                    getattr(point, "y_m", getattr(point, "y", 0.0))
                ),
                "psi_rad": float(
                    getattr(point, "psi_rad", getattr(point, "heading", 0.0))
                ),
                "u_mps": float(
                    getattr(point, "u_mps", getattr(point, "speed", 0.0))
                ),
                "v_mps": float(getattr(point, "v_mps", 0.0)),
                "r_radps": float(
                    getattr(point, "r_radps", getattr(point, "yaw_rate", 0.0))
                ),
            })
    return rows


def __arrow_table(rows, schema, path):
    """Write *rows* to an Arrow IPC file at *path* using *schema*."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    col_names = [f.name for f in schema]
    data = {col: [r[col] for r in rows] for col in col_names}
    table = pa.table(data, schema=schema)
    with pa.ipc.new_file(str(path), schema) as writer:
        writer.write_table(table)
    return table.num_rows


def run_batch_arrow(scenario_dirs, output_dir, progress_cb=None):
    """Run a batch of scenarios and produce Arrow dual output.

    Discovers all YAML scenario files under *scenario_dirs*, runs each
    scenario through the simulator, and writes:

    - ``batch_summary.arrow``    — one row per scenario (pass/fail + metrics)
    - ``trajectories.arrow``     — sampled own-ship trajectory points
    - ``batch_results.json``     — backward-compatible JSON summary

    Args:
        scenario_dirs: One or more directory paths (``str``, ``Path``,
                       or iterable thereof).
        output_dir:   Directory for output artifacts.
        progress_cb:  Optional ``progress_cb(i, total)`` called after each
                      scenario completes.

    Returns:
        Dict with summary metadata (total, passed, failed, output paths).
    """
    if isinstance(scenario_dirs, (str, Path)):
        scenario_dirs = [scenario_dirs]
    scenario_dirs = [Path(d) for d in scenario_dirs]

    yaml_files = discover_scenarios(scenario_dirs)
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    summary_rows: list[dict] = []
    trajectory_rows: list[dict] = []
    total = len(yaml_files)

    for i, yaml_path in enumerate(yaml_files):
        spec = ScenarioSpec.model_validate(yaml.safe_load(yaml_path.read_text()))
        sid = spec.scenario_id.replace("-v1.0", "")

        t0 = time.perf_counter()
        result_na = simulate(spec, apply_avoidance=False)
        result_wa = simulate(spec, apply_avoidance=True)
        wall_clock = time.perf_counter() - t0

        dcpa_na = result_na.dcpa_m
        dcpa_wa = result_wa.dcpa_m
        passed = dcpa_na < spec.pass_criteria.max_dcpa_no_action_m

        sim_duration = getattr(result_wa, "duration_s", wall_clock)
        summary_rows.append({
            "scenario_id": sid,
            "pass": bool(passed),
            "dcpa_m": float(dcpa_wa),
            "wall_clock_s": float(wall_clock),
            "duration_s": float(sim_duration),
        })
        trajectory_rows.extend(
            _extract_trajectory(result_wa, sid)
        )

        if progress_cb:
            progress_cb(i + 1, total)

    summary_path = output_dir / "batch_summary.arrow"
    n_summary = __arrow_table(summary_rows, SUMMARY_SCHEMA, summary_path)

    traj_path = output_dir / "trajectories.arrow"
    n_traj = __arrow_table(trajectory_rows, TRAJECTORY_POINT_SCHEMA, traj_path)

    summary_json = {
        "batch_timestamp": datetime.now(tz=timezone.utc).isoformat(),
        "total": total,
        "passed": sum(1 for r in summary_rows if r["pass"]),
        "failed": sum(1 for r in summary_rows if not r["pass"]),
        "arrow_summary": str(summary_path),
        "arrow_trajectories": str(traj_path),
    }
    json_path = output_dir / "batch_results.json"
    json_path.write_text(json.dumps(summary_json, indent=2))

    print(f"Arrow summary:  {summary_path} ({n_summary} rows)")
    print(f"Arrow traj:     {traj_path} ({n_traj} rows)")

    return summary_json


def main() -> None:
    parser = argparse.ArgumentParser(description="D1.3b batch scenario runner")
    parser.add_argument("--scenarios", type=Path, default=Path("scenarios/colregs"))
    parser.add_argument("--output", type=Path, default=Path("reports"))
    args = parser.parse_args()

    import coverage_reporter
    run_batch(args.scenarios, args.output)
    ts = datetime.now(tz=timezone.utc).strftime("%Y%m%d")
    html_path = args.output / f"coverage_report_{ts}.html"
    coverage_reporter.generate_report(args.output, html_path)
    print(f"HTML report: {html_path}")


if __name__ == "__main__":
    main()
