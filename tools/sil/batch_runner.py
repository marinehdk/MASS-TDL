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

from scenario_spec import ScenarioSpec
from simulate import simulate


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

    yaml_files = sorted(f for f in scenarios_dir.glob("*.yaml") if f.name != "schema.yaml")
    print(f"Found {len(yaml_files)} scenario files in {scenarios_dir}")

    for yaml_path in yaml_files:
        spec = ScenarioSpec.model_validate(yaml.safe_load(yaml_path.read_text()))
        sid = spec.scenario_id.replace("-v1.0", "")
        rule_info = SCENARIO_RULE_MAP.get(sid)

        print(f"  Running {spec.scenario_id}...")

        # Pass 1: no-action
        result_na = simulate(spec, apply_avoidance=False)
        no_action_dcpa = result_na.dcpa_m

        # Pass 2: with-action
        result_wa = simulate(spec, apply_avoidance=True)

        geometric_pass = no_action_dcpa < spec.pass_criteria.max_dcpa_no_action_m
        bearing_pass = _check_bearing(spec, rule_info) if rule_info else True

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

    return results


def main() -> None:
    parser = argparse.ArgumentParser(description="D1.3b batch scenario runner")
    parser.add_argument("--scenarios", type=Path, default=Path("scenarios/colregs"))
    parser.add_argument("--output", type=Path, default=Path("reports"))
    args = parser.parse_args()

    import coverage_reporter
    results = run_batch(args.scenarios, args.output)
    ts = datetime.now(tz=timezone.utc).strftime("%Y%m%d")
    html_path = args.output / f"coverage_report_{ts}.html"
    coverage_reporter.generate_report(args.output, html_path)
    print(f"HTML report: {html_path}")


if __name__ == "__main__":
    main()
