#!/usr/bin/env python3
"""D1.3.1 MMG fidelity assessment tool — Nomoto reference model vs FCB MMG.

Generates Nomoto 1st-order reference trajectories (deceleration, turning circle,
zigzag) as ground truth for MMG fidelity validation (V&V Plan §2.1 E1.5).

When --all is used, writes reference CSVs and a JSON fidelity report. Compares
simulated MMG data (from D1.3a) against these references once D1.3a is delivered.

Usage:
    python tools/sil/d1_3_1_mmg_fidelity.py --all --output-dir test-results/d1_3_1/
    python tools/sil/d1_3_1_mmg_fidelity.py --reference turning_circle --csv turning.csv
    python tools/sil/d1_3_1_mmg_fidelity.py --compare sim.csv --reference turning_circle
"""
from __future__ import annotations

import argparse
import json
import logging
import sys
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

logger = logging.getLogger(__name__)

FCB_U0_MS = 9.26
FCB_T_U_S = 12.5
FCB_K = 0.085
FCB_T_VAL_S = 2.8
FCB_DELTA_RAD = 0.262
FCB_DELTA_DEG = 10.0

DEFAULT_DT_S = 0.1
DECEL_DURATION_S = 60.0
TURN_DURATION_S = 600.0
ZIGZAG_DURATION_S = 200.0

REPORT_VERSION = "1.0"


def nomoto_deceleration(t: np.ndarray, u0: float, T_u: float) -> np.ndarray:
    """Exponential speed decay: u(t) = u0 * exp(-t / T_u)."""
    if T_u <= 0:
        return np.full_like(t, u0, dtype=float)
    return u0 * np.exp(-t / T_u)


def stopping_distance(t: np.ndarray, u: np.ndarray) -> float:
    """Trapezoidal integral of speed over time: S_stop = ∫ u(t) dt."""
    if len(t) < 2:
        return 0.0
    return float(np.trapezoid(u, t))


def nomoto_turning_circle(
    t: np.ndarray,
    u: np.ndarray,
    delta: float,
    K: float,
    T_val: float,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Nomoto 1st-order turning circle integration.

    Integrates r_dot = (K*delta - r)/T_val using semi-implicit Euler.
    Returns (x, y, r) arrays.
    """
    n = len(t)
    if n < 2:
        return (
            np.zeros(n, dtype=float),
            np.zeros(n, dtype=float),
            np.zeros(n, dtype=float),
        )
    dt = t[1] - t[0]

    r = np.zeros(n, dtype=float)
    psi = np.zeros(n, dtype=float)
    x = np.zeros(n, dtype=float)
    y = np.zeros(n, dtype=float)

    for i in range(1, n):
        r_dot = (K * delta - r[i - 1]) / T_val
        r[i] = r[i - 1] + r_dot * dt
        psi[i] = psi[i - 1] + r[i] * dt
        speed = u[i] if i < len(u) else u[-1]
        x[i] = x[i - 1] + speed * np.cos(psi[i]) * dt
        y[i] = y[i - 1] + speed * np.sin(psi[i]) * dt

    return x, y, r


def tactical_diameter(t: np.ndarray, x: np.ndarray, y: np.ndarray) -> float:
    """Tactical diameter: 2 * max(|y|) for a turn starting on x-axis heading East."""
    return float(2.0 * np.max(np.abs(y)))


def nomoto_zigzag(
    t: np.ndarray,
    u: np.ndarray,
    delta_deg: float,
    K: float,
    T_val: float,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Nomoto 1st-order zigzag with rudder reversal at ±delta_deg heading crossings.

    Rudder angle is delta_deg (degrees, converted to radians internally).
    Returns (x, y, psi, delta_t) where delta_t is rudder angle time series [rad].
    """
    n = len(t)
    if n < 2:
        return (
            np.zeros(n, dtype=float),
            np.zeros(n, dtype=float),
            np.zeros(n, dtype=float),
            np.zeros(n, dtype=float),
        )
    dt = t[1] - t[0]
    delta_rad = np.radians(delta_deg)
    delta_thresh_rad = np.radians(delta_deg)

    r = np.zeros(n, dtype=float)
    psi = np.zeros(n, dtype=float)
    x = np.zeros(n, dtype=float)
    y = np.zeros(n, dtype=float)
    delta_t = np.zeros(n, dtype=float)

    current_delta = delta_rad
    delta_t[0] = current_delta

    for i in range(1, n):
        prev_psi = psi[i - 1]
        if prev_psi > delta_thresh_rad and current_delta > 0:
            current_delta = -delta_rad
        elif prev_psi < -delta_thresh_rad and current_delta < 0:
            current_delta = delta_rad

        delta_t[i] = current_delta
        r_dot = (K * current_delta - r[i - 1]) / T_val
        r[i] = r[i - 1] + r_dot * dt
        psi[i] = psi[i - 1] + r[i] * dt
        speed = u[i] if i < len(u) else u[-1]
        x[i] = x[i - 1] + speed * np.cos(psi[i]) * dt
        y[i] = y[i - 1] + speed * np.sin(psi[i]) * dt

    return x, y, psi, delta_t


def overshoot_angle(t: np.ndarray, psi: np.ndarray, delta_t: np.ndarray) -> float:
    """First overshoot angle: max(|psi| after first reversal) - delta_deg [deg].

    Identifies the time of first rudder reversal, then computes the peak heading
    beyond the reversal threshold.
    """
    n = len(delta_t)
    if n < 2:
        return 0.0

    first_reversal_idx = None
    for i in range(1, n):
        if delta_t[i] * delta_t[i - 1] < 0:
            first_reversal_idx = i
            break

    if first_reversal_idx is None:
        return 0.0

    psi_after = psi[first_reversal_idx:]
    if len(psi_after) == 0:
        return 0.0

    delta_deg_rad = np.radians(FCB_DELTA_DEG)
    peak_rad = float(np.max(np.abs(psi_after)))
    return float(np.degrees(peak_rad) - FCB_DELTA_DEG)


def compute_rms_error(
    traj_sim: np.ndarray,
    traj_ref: np.ndarray,
    normalize: float | None = None,
) -> float:
    """RMS error as percentage of reference range.

    rms_pct = 100 * sqrt(mean((sim - ref)^2)) / norm
    where norm = normalize if provided, else max(ref) - min(ref).
    """
    if len(traj_sim) < 1 or len(traj_ref) < 1:
        return 0.0
    diff = traj_sim - traj_ref
    rms = float(np.sqrt(np.mean(diff ** 2)))
    if normalize is None:
        ref_range = float(np.max(traj_ref) - np.min(traj_ref))
        if ref_range < 1e-12:
            return 0.0
        return float(100.0 * rms / ref_range)
    if normalize < 1e-12:
        return 0.0
    return float(100.0 * rms / normalize)


def resample_to_common_time(
    t_sim: np.ndarray,
    v_sim: np.ndarray,
    t_ref: np.ndarray,
    v_ref: np.ndarray,
) -> tuple[np.ndarray, np.ndarray]:
    """Interpolate both signals onto a common time grid.

    Returns (v_sim_interp, v_ref_interp) on union of time points.
    """
    t_common = np.union1d(t_sim, t_ref)
    v_sim_interp = np.interp(t_common, t_sim, v_sim)
    v_ref_interp = np.interp(t_common, t_ref, v_ref)
    return v_sim_interp, v_ref_interp


@dataclass
class FidelityResult:
    scenario_id: str
    reference_name: str
    rms_position_pct: float
    rms_heading_deg: float
    rms_speed_pct: float
    passed: bool
    data_path: str = ""


@dataclass
class FidelityReport:
    report_version: str = REPORT_VERSION
    generated_at: str = field(default_factory=lambda: datetime.now(tz=timezone.utc).isoformat())
    results: list[FidelityResult] = field(default_factory=list)

    @property
    def overall_pass(self) -> bool:
        return all(r.passed for r in self.results) if self.results else True

    def to_json_dict(self) -> dict:
        return {
            "report_version": self.report_version,
            "generated_at": self.generated_at,
            "overall_pass": self.overall_pass,
            "results": [
                {
                    "scenario_id": r.scenario_id,
                    "reference_name": r.reference_name,
                    "rms_position_pct": round(r.rms_position_pct, 3),
                    "rms_heading_deg": round(r.rms_heading_deg, 3),
                    "rms_speed_pct": round(r.rms_speed_pct, 3),
                    "passed": r.passed,
                    "data_path": r.data_path,
                }
                for r in self.results
            ],
        }


def _make_time(duration_s: float, dt_s: float = DEFAULT_DT_S) -> np.ndarray:
    return np.arange(0, duration_s, dt_s, dtype=float)


def _speed_constant(t: np.ndarray, u0: float) -> np.ndarray:
    return np.full_like(t, u0, dtype=float)


def _generate_deceleration(output_dir: Path | None = None) -> tuple[str, dict]:
    t = _make_time(DECEL_DURATION_S)
    u = nomoto_deceleration(t, FCB_U0_MS, FCB_T_U_S)
    s_stop = stopping_distance(t, u)

    data_info = {"stopping_distance_m": round(s_stop, 2), "n_points": len(t)}
    if output_dir is not None:
        output_dir.mkdir(parents=True, exist_ok=True)
        path = output_dir / "reference_deceleration.csv"
        np.savetxt(
            path,
            np.column_stack((t, u)),
            delimiter=",",
            header="t_s,u_ms",
            comments="",
        )
        data_info["data_path"] = str(path)
    return "deceleration", data_info


def _generate_turning_circle(output_dir: Path | None = None) -> tuple[str, dict]:
    t = _make_time(TURN_DURATION_S)
    u = _speed_constant(t, FCB_U0_MS)
    x, y, r = nomoto_turning_circle(t, u, FCB_DELTA_RAD, FCB_K, FCB_T_VAL_S)
    td = tactical_diameter(t, x, y)

    data_info = {"tactical_diameter_m": round(td, 1), "n_points": len(t)}
    if output_dir is not None:
        output_dir.mkdir(parents=True, exist_ok=True)
        path = output_dir / "reference_turning_circle.csv"
        np.savetxt(
            path,
            np.column_stack((t, x, y, r, u)),
            delimiter=",",
            header="t_s,x_m,y_m,r_radps,u_ms",
            comments="",
        )
        data_info["data_path"] = str(path)
    return "turning_circle", data_info


def _generate_zigzag(output_dir: Path | None = None) -> tuple[str, dict]:
    t = _make_time(ZIGZAG_DURATION_S)
    u = _speed_constant(t, FCB_U0_MS)
    x, y, psi, delta_t = nomoto_zigzag(t, u, FCB_DELTA_DEG, FCB_K, FCB_T_VAL_S)
    osa = overshoot_angle(t, psi, delta_t)

    n_reversals = int(np.sum(np.diff(np.sign(delta_t)) != 0))
    data_info = {
        "overshoot_angle_deg": round(osa, 3),
        "n_reversals": n_reversals,
        "n_points": len(t),
    }
    if output_dir is not None:
        output_dir.mkdir(parents=True, exist_ok=True)
        path = output_dir / "reference_zigzag.csv"
        np.savetxt(
            path,
            np.column_stack((t, x, y, psi, delta_t)),
            delimiter=",",
            header="t_s,x_m,y_m,psi_rad,delta_rad",
            comments="",
        )
        data_info["data_path"] = str(path)
    return "zigzag", data_info


def _run_all(output_dir: Path) -> FidelityReport:
    report = FidelityReport()
    print("SIMULATION DATA NOT YET AVAILABLE — D1.3a MMG module has not been delivered.\n")

    generators = [_generate_deceleration, _generate_turning_circle, _generate_zigzag]
    for gen_fn in generators:
        ref_name, info = gen_fn(output_dir)
        print(f"  [{ref_name}] generated: {info}")
        result = FidelityResult(
            scenario_id=ref_name,
            reference_name=ref_name,
            rms_position_pct=0.0,
            rms_heading_deg=0.0,
            rms_speed_pct=0.0,
            passed=True,
            data_path=info.get("data_path", ""),
        )
        report.results.append(result)

    report_path = output_dir / "fidelity_report.json"
    report_path.write_text(json.dumps(report.to_json_dict(), indent=2))
    print(f"\nFidelity report written to {report_path}")
    print("All references are Nomoto 1st-order baselines — pending D1.3a MMG comparison.")
    return report


def _generate_single(reference: str, csv_path: Path | None) -> None:
    gen_map = {
        "deceleration": _generate_deceleration,
        "turning_circle": _generate_turning_circle,
        "zigzag": _generate_zigzag,
    }
    if reference not in gen_map:
        print(f"Unknown reference '{reference}'. Options: {list(gen_map.keys())}", file=sys.stderr)
        sys.exit(1)

    output_dir = csv_path.parent if csv_path else None
    ref_name, info = gen_map[reference](output_dir)
    print(f"[{ref_name}] {json.dumps(info, indent=2)}")

    if csv_path:
        print(f"CSV written to {csv_path}")


def _run_sensitivity(output_dir: Path) -> None:
    factors = [0.8, 0.9, 1.0, 1.1, 1.2]
    t = _make_time(TURN_DURATION_S)
    u = _speed_constant(t, FCB_U0_MS)
    _, y_nom, _ = nomoto_turning_circle(t, u, FCB_DELTA_RAD, FCB_K, FCB_T_VAL_S)

    results = []
    for mult in factors:
        x, y, _ = nomoto_turning_circle(t, u, FCB_DELTA_RAD, mult * FCB_K, FCB_T_VAL_S)
        td = tactical_diameter(t, x, y)
        y_ref, _ = resample_to_common_time(t, y, t, y_nom)
        rms = compute_rms_error(y, y_nom)
        results.append({"K_multiplier": mult, "tactical_diameter_m": round(td, 1), "rms_y_pct": round(rms, 3)})

    output_dir.mkdir(parents=True, exist_ok=True)
    path = output_dir / "sensitivity_K.json"
    path.write_text(json.dumps(results, indent=2))
    print(f"Sensitivity (K) → {path}")

    results_T = []
    for mult in factors:
        x, y, _ = nomoto_turning_circle(t, u, FCB_DELTA_RAD, FCB_K, mult * FCB_T_VAL_S)
        td = tactical_diameter(t, x, y)
        rms = compute_rms_error(y, y_nom)
        results_T.append({"T_multiplier": mult, "tactical_diameter_m": round(td, 1), "rms_y_pct": round(rms, 3)})

    path_T = output_dir / "sensitivity_T.json"
    path_T.write_text(json.dumps(results_T, indent=2))
    print(f"Sensitivity (T) → {path_T}")


def _run_stability_check(output_dir: Path) -> None:
    t_ref = _make_time(TURN_DURATION_S, DEFAULT_DT_S)
    u_ref = _speed_constant(t_ref, FCB_U0_MS)
    _, y_ref, _ = nomoto_turning_circle(t_ref, u_ref, FCB_DELTA_RAD, FCB_K, FCB_T_VAL_S)

    dt_fine = DEFAULT_DT_S / 2.0
    t_fine = _make_time(TURN_DURATION_S, dt_fine)
    u_fine = _speed_constant(t_fine, FCB_U0_MS)
    _, y_fine, _ = nomoto_turning_circle(t_fine, u_fine, FCB_DELTA_RAD, FCB_K, FCB_T_VAL_S)

    y_ref_resampled, y_fine_resampled = resample_to_common_time(t_ref, y_ref, t_fine, y_fine)
    rms = compute_rms_error(y_ref_resampled, y_fine_resampled)

    output_dir.mkdir(parents=True, exist_ok=True)
    result = {
        "dt_coarse_s": DEFAULT_DT_S,
        "dt_fine_s": dt_fine,
        "rms_y_pct": round(rms, 3),
        "stable": rms < 1.0,
    }
    path = output_dir / "stability_check.json"
    path.write_text(json.dumps(result, indent=2))
    print(f"Stability check → {path}  (RMS={rms:.3f}%, {'stable' if rms < 1.0 else 'unstable'})")


REFERENCE_FILENAMES = {
    "deceleration": "reference_deceleration.csv",
    "turning_circle": "reference_turning_circle.csv",
    "zigzag": "reference_zigzag.csv",
}


def _run_compare(sim_path: str, reference: str, output_dir: Path) -> FidelityReport:
    sim_data = np.loadtxt(sim_path, delimiter=",", skiprows=1)
    t_sim = sim_data[:, 0]

    if reference not in REFERENCE_FILENAMES:
        options = list(REFERENCE_FILENAMES.keys())
        print(f"Unknown reference '{reference}'. Options: {options}", file=sys.stderr)
        sys.exit(1)

    ref_path = output_dir / REFERENCE_FILENAMES[reference]
    if not ref_path.exists():
        print(f"Reference data not found at {ref_path}. Run --all first.", file=sys.stderr)
        sys.exit(1)

    ref_data = np.loadtxt(str(ref_path), delimiter=",", skiprows=1)
    t_ref = ref_data[:, 0]

    rms_pos = compute_rms_error(sim_data[:, 1], ref_data[:, 1])
    if sim_data.shape[1] >= 4:
        sim_resampled, ref_resampled = resample_to_common_time(
            t_sim, sim_data[:, 3], t_ref, ref_data[:, 3]
        )
        rms_hdg = compute_rms_error(sim_resampled, ref_resampled)
    else:
        rms_hdg = 0.0

    rms_spd = 0.0
    if sim_data.shape[1] >= 5:
        sim_resampled, ref_resampled = resample_to_common_time(
            t_sim, sim_data[:, 4], t_ref, ref_data[:, 4]
        )
        rms_spd = compute_rms_error(sim_resampled, ref_resampled)

    passed = rms_pos < 5.0 and rms_hdg < 5.0 and rms_spd < 5.0

    result = FidelityResult(
        scenario_id=Path(sim_path).stem,
        reference_name=reference,
        rms_position_pct=rms_pos,
        rms_heading_deg=rms_hdg,
        rms_speed_pct=rms_spd,
        passed=passed,
        data_path=sim_path,
    )

    report = FidelityReport(results=[result])
    output_dir.mkdir(parents=True, exist_ok=True)
    report_path = output_dir / "comparison_report.json"
    report_path.write_text(json.dumps(report.to_json_dict(), indent=2))
    print(f"Comparison report → {report_path}")
    print(f"  RMS position: {rms_pos:.3f}%")
    print(f"  RMS heading:  {rms_hdg:.3f}°")
    print(f"  RMS speed:    {rms_spd:.3f}%")
    print(f"  Passed:       {passed}")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(
        description="D1.3.1 MMG fidelity assessment — Nomoto reference model"
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="Generate all 3 reference CSVs and JSON fidelity report",
    )
    parser.add_argument(
        "--reference",
        type=str,
        choices=["deceleration", "turning_circle", "zigzag", "all"],
        help="Generate a single reference trajectory",
    )
    parser.add_argument(
        "--scenario",
        type=str,
        default="default",
        help="Scenario ID for output naming",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("test-results/d1_3_1"),
        help="Output directory for CSVs and reports",
    )
    parser.add_argument(
        "--compare",
        type=str,
        metavar="SIM_CSV",
        help="Compare a simulated trajectory CSV against a reference",
    )
    parser.add_argument(
        "--sensitivity",
        action="store_true",
        help="Run parameter sensitivity (±20%% on K, T_val)",
    )
    parser.add_argument(
        "--stability-check",
        action="store_true",
        help="Verify integration stability by halving dt",
    )
    parser.add_argument(
        "--csv",
        type=Path,
        help="Output path for a single reference CSV (used with --reference)",
    )
    args = parser.parse_args()

    if args.all:
        _run_all(args.output_dir)
        return 0

    if args.sensitivity:
        _run_sensitivity(args.output_dir)
        return 0

    if args.stability_check:
        _run_stability_check(args.output_dir)
        return 0

    if args.compare:
        if not args.reference:
            print("--compare requires --reference", file=sys.stderr)
            return 1
        _run_compare(args.compare, args.reference, args.output_dir)
        return 0

    if args.reference:
        _generate_single(args.reference, args.csv)
        return 0

    parser.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
