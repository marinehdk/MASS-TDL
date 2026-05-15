#!/usr/bin/env python3
"""D1.3.1 Sensor Calibration Runner — validates sensor models against CG-0264 §6 limits.

Usage:
  python tools/sil/d1_3_1_sensor_calibrate.py --scenario scenario.yaml
  python tools/sil/d1_3_1_sensor_calibrate.py --all-scenarios --output-dir reports/
  python tools/sil/d1_3_1_sensor_calibrate.py --scenario a.yaml --scenario b.yaml --compare
"""

import argparse
import copy
import json
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

import numpy as np

# ─── CG-0264 §6 limits ──────────────────────────────────────────────────────
# CG-0264 §6 limits from public summary — pending full PDF verification (GAP-032)
CG0264_LIMITS = {
    "radar": {
        "P_D_nom_min": 0.95,
        "P_D_deg_min": 0.75,
        "position_std_max_m": 11.0,
    },
    "ais": {
        "position_std_nom_max_m": 5.1,
        "position_std_deg_max_m": 7.1,
    },
    "gnss": {
        "CEP95_nom_max_m": 5.0,
        "reacq_max_s": 30.0,
    },
}


def _parse_simple_yaml(text: str) -> dict[str, Any]:
    """Minimal YAML parser for flat section→key→scalar config format."""
    result: dict[str, Any] = {}
    current_section: str | None = None

    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if ":" not in stripped:
            continue

        key, _, value = stripped.partition(":")
        key = key.strip()
        value = value.strip()

        if not value:
            current_section = key
            result[current_section] = {}
        elif current_section is not None and line.startswith("  "):
            result[current_section][key] = _coerce_value(value)
        else:
            result[key] = _coerce_value(value)
            current_section = None

    return result


def _coerce_value(raw: str) -> float | int | str | bool:
    if raw.lower() in ("true", "yes"):
        return True
    if raw.lower() in ("false", "no"):
        return False
    try:
        return int(raw)
    except ValueError:
        pass
    try:
        return float(raw)
    except ValueError:
        pass
    return raw.strip("\"'").strip()


def load_sensor_config(scenario_path: str) -> dict[str, Any]:
    """Parse sensor configuration from a YAML (or JSON) file.

    YAML is parsed with a minimal stdlib-only parser that handles the flat
    section→key→scalar format used by D1.3.1 sensor configs.
    """
    path = Path(scenario_path)
    raw = path.read_text()
    if raw.strip().startswith("{"):
        return json.loads(raw)
    return _parse_simple_yaml(raw)


def apply_degradation(config: dict[str, Any], condition: str) -> dict[str, Any]:
    """Apply ``"nominal"`` or ``"degraded"`` multipliers to sensor config.

    Degraded multipliers:
      * radar: max_range ×0.5, R_ne ×4, P_D −0.15, clutter ×4
      * AIS:   R_position ×2, update_interval ×3.3, dropout_rate 0.10
      * GNSS:  hdop ×3, position_noise ×3, outage_prob 0.01, outage_duration_s 5.0
    """
    degraded = copy.deepcopy(config)

    if condition == "nominal":
        return degraded

    r = degraded.get("radar", {})
    if r:
        r["max_range"] = r.get("max_range", 0) * 0.5
        r["R_ne"] = r.get("R_ne", 0) * 4.0
        r["P_D"] = max(0.0, r.get("P_D", 0) - 0.15)
        r["clutter"] = r.get("clutter", 0) * 4.0

    a = degraded.get("ais", {})
    if a:
        a["R_position"] = a.get("R_position", 0) * 2.0
        a["update_interval"] = a.get("update_interval", 0) * 3.3
        a["dropout_rate"] = 0.10

    g = degraded.get("gnss", {})
    if g:
        g["hdop"] = g.get("hdop", 1.0) * 3.0
        g["position_noise"] = g.get("position_noise", 0) * 3.0
        g["outage_prob"] = 0.01
        g["outage_duration_s"] = 5.0

    return degraded


def compute_sensor_metrics(config: dict[str, Any]) -> dict[str, Any]:
    """Derive per-sensor validation metrics from raw config values."""
    metrics: dict[str, Any] = {}

    r = config.get("radar", {})
    if r:
        rate = r.get("measurement_rate", 10)
        metrics["radar"] = {
            "expected_pd": r.get("P_D", 0.95),
            "position_std_m": r.get("R_ne", 0.5),
            "update_interval_s": 1.0 / rate if rate > 0 else float("inf"),
            "dropout_rate": 0.0,
        }

    a = config.get("ais", {})
    if a:
        metrics["ais"] = {
            "expected_pd": 1.0 - a.get("dropout_rate", 0.0),
            "position_std_m": a.get("R_position", 5.0),
            "update_interval_s": a.get("update_interval", 6),
            "dropout_rate": a.get("dropout_rate", 0.0),
        }

    g = config.get("gnss", {})
    if g:
        hdop = g.get("hdop", 1.0)
        noise = g.get("position_noise", 1.5)
        std_m = hdop * noise
        metrics["gnss"] = {
            "expected_pd": 1.0 - g.get("outage_prob", 0.0),
            "position_std_m": std_m,
            "update_interval_s": 0.1,
            "dropout_rate": g.get("outage_prob", 0.0),
            "cep95_m": 2.08 * std_m,
            "reacq_s": g.get("outage_duration_s", 0.0),
        }

    return metrics


def validate_against_cg0264(
    metrics: dict[str, Any], sensor: str, condition: str
) -> dict[str, Any]:
    """Validate a single sensor's metrics against CG-0264 §6 limits."""
    violations: list[str] = []
    m = metrics.get(sensor, {})
    limits = CG0264_LIMITS.get(sensor, {})

    if sensor == "radar":
        pd = m.get("expected_pd", 0)
        pd_min = (
            limits["P_D_nom_min"] if condition == "nominal" else limits["P_D_deg_min"]
        )
        if pd < pd_min:
            violations.append(
                f"P_D={pd:.3f} < {pd_min} ({condition})"
            )
        if m.get("position_std_m", 999) > limits["position_std_max_m"]:
            violations.append(
                f"position_std={m['position_std_m']:.1f}m > {limits['position_std_max_m']}m"
            )

    elif sensor == "ais":
        std_max = (
            limits["position_std_nom_max_m"]
            if condition == "nominal"
            else limits["position_std_deg_max_m"]
        )
        if m.get("position_std_m", 999) > std_max:
            violations.append(
                f"position_std={m['position_std_m']:.1f}m > {std_max}m ({condition})"
            )

    elif sensor == "gnss":
        if condition == "nominal" and m.get("cep95_m", 999) > limits["CEP95_nom_max_m"]:
            violations.append(
                f"CEP95={m['cep95_m']:.1f}m > {limits['CEP95_nom_max_m']}m"
            )
        if m.get("reacq_s", 999) > limits["reacq_max_s"]:
            violations.append(
                f"reacq={m['reacq_s']:.1f}s > {limits['reacq_max_s']}s"
            )

    return {
        "passed": len(violations) == 0,
        "violations": violations,
    }


@dataclass
class SensorCalibrationReport:
    sensor: str
    condition: str
    metrics: dict[str, Any]
    validation: dict[str, Any]
    passed: bool


def _report_for_sensor(
    config: dict[str, Any], sensor: str, condition: str
) -> SensorCalibrationReport:
    metrics = compute_sensor_metrics(config)
    validation = validate_against_cg0264(metrics, sensor, condition)
    return SensorCalibrationReport(
        sensor=sensor,
        condition=condition,
        metrics=metrics.get(sensor, {}),
        validation=validation,
        passed=validation["passed"],
    )


def _print_report(report: SensorCalibrationReport) -> None:
    status = "PASS" if report.passed else "FAIL"
    print(f"\n  [{report.sensor}] {report.condition:>8}  {status}")
    for k, v in report.metrics.items():
        if isinstance(v, float):
            print(f"    {k:20s} = {v:.3f}")
    if report.validation["violations"]:
        for v in report.validation["violations"]:
            print(f"    VIOLATION: {v}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="D1.3.1 Sensor Calibration Runner"
    )
    parser.add_argument(
        "--scenario", action="append", default=[],
        help="Path to scenario YAML (repeatable)",
    )
    parser.add_argument("--all-scenarios", action="store_true")
    parser.add_argument("--compare", action="store_true")
    parser.add_argument(
        "--output-dir", type=Path, default=Path("reports/sensor_calibration"),
    )
    parser.add_argument(
        "--conditions", nargs="+", default=["nominal", "degraded"],
    )
    args = parser.parse_args()

    if not args.scenario:
        parser.error("at least one --scenario is required")

    sensors = ["radar", "ais", "gnss"]
    all_passed = True

    for scenario_path in args.scenario:
        print(f"Scenario: {scenario_path}")
        config = load_sensor_config(scenario_path)

        for sensor in sensors:
            if sensor not in config:
                continue
            for condition in args.conditions:
                degraded_config = apply_degradation(config, condition)
                report = _report_for_sensor(degraded_config, sensor, condition)
                _print_report(report)
                if not report.passed:
                    all_passed = False

        if args.compare:
            print("\n  --- Comparison (nominal vs degraded) ---")
            nominal = apply_degradation(config, "nominal")
            degraded = apply_degradation(config, "degraded")
            for sensor in sensors:
                if sensor not in config:
                    continue
                nm = compute_sensor_metrics(nominal).get(sensor, {})
                dm = compute_sensor_metrics(degraded).get(sensor, {})
                print(f"  [{sensor}]")
                for key in nm:
                    nv = nm[key]
                    dv = dm.get(key, nv)
                    if isinstance(nv, float) and isinstance(dv, float):
                        delta = dv - nv
                        print(f"    {key:20s}  nom={nv:.3f}  deg={dv:.3f}  Δ={delta:+.3f}")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    summary = {
        "all_passed": all_passed,
        "scenarios": args.scenario,
        "conditions": args.conditions,
    }
    (args.output_dir / "summary.json").write_text(json.dumps(summary, indent=2))
    print(f"\nReport saved to {args.output_dir / 'summary.json'}")

    sys.exit(0 if all_passed else 1)


if __name__ == "__main__":
    main()
