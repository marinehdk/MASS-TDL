"""D1.3.1 Determinism replay tool.

Phase 1 stub: generates synthetic deterministic trajectories using numpy sin/cos
and verifies cross-run determinism. Real ROS2 integration comes with D1.3b.3.
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np


@dataclass
class DeterminismReport:
    run_count: int
    max_heading_diff_deg: float
    max_position_diff_m: float
    max_time_shift_s: float
    passed: bool


def run_simulation(scenario_yaml: str, seed: int, output_dir: str) -> Path:
    """Generate a fully deterministic synthetic trajectory CSV (Phase 1 stub).

    Uses numpy sin/cos with a fixed RNG seed to produce identical output
    on every invocation with the same seed. No ROS2 dependency.
    """
    print("Phase 1: standalone mode — ROS2 lifecycle not yet integrated.")

    rng = np.random.default_rng(seed)

    amp = 50.0 + rng.random() * 100.0
    freq = 0.02 + rng.random() * 0.04
    phase = rng.random() * 2.0 * math.pi
    base_speed = 5.0 + rng.random() * 10.0
    duration = 90.0
    dt = 0.1

    t = np.arange(0.0, duration, dt)
    x = base_speed * t + amp * np.sin(freq * t + phase)
    y = amp * np.cos(freq * t + phase)
    psi = np.arctan2(
        base_speed + amp * freq * np.cos(freq * t + phase),
        -amp * freq * np.sin(freq * t + phase),
    )

    out_dir = Path(output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    csv_path = out_dir / f"trajectory_seed{seed:03d}.csv"
    np.savetxt(
        csv_path,
        np.column_stack([t, x, y, psi]),
        delimiter=",",
        header="t,x,y,psi",
        comments="",
    )
    return csv_path


def load_trajectory_csv(csv_path: Path) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Load (t, x, y, psi) arrays from a CSV file produced by run_simulation."""
    data = np.loadtxt(csv_path, delimiter=",", skiprows=1)
    return data[:, 0], data[:, 1], data[:, 2], data[:, 3]


def _angle_diff(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """Element-wise angular difference wrapped to [-pi, pi]."""
    d = a - b
    return np.arctan2(np.sin(d), np.cos(d))


def compute_time_shift(traj_i: np.ndarray, traj_j: np.ndarray) -> float:
    """Find delta-t that minimizes RMSD between trajectories.

    Brute-force search over [-1.0, +1.0] seconds at 0.01 s resolution.
    traj arrays have shape (N, 4) with columns (t, x, y, psi).

    Returns the time shift to apply to traj_j to align it with traj_i.
    Positive shift means traj_j is delayed relative to traj_i.
    """
    t_i = traj_i[:, 0]
    t_j = traj_j[:, 0]

    dt_values = np.arange(-1.0, 1.0 + 0.005, 0.01)
    best_shift = 0.0
    best_rmsd = float("inf")

    for dt in dt_values:
        t_shifted = t_j + dt
        mask = (t_i >= t_shifted[0]) & (t_i <= t_shifted[-1])
        if not np.any(mask):
            continue

        x_interp = np.interp(t_i[mask], t_shifted, traj_j[:, 1])
        y_interp = np.interp(t_i[mask], t_shifted, traj_j[:, 2])
        psi_interp = np.interp(t_i[mask], t_shifted, traj_j[:, 3])

        diff_xy = np.sqrt(
            (traj_i[mask, 1] - x_interp) ** 2
            + (traj_i[mask, 2] - y_interp) ** 2
        )
        diff_psi = np.abs(_angle_diff(traj_i[mask, 3], psi_interp))
        rmsd = float(np.sqrt(np.mean(diff_xy**2 + diff_psi**2)))

        if rmsd < best_rmsd:
            best_rmsd = rmsd
            best_shift = float(dt)

    return best_shift


def compute_pairwise_diffs(trajectories: list[np.ndarray]) -> dict[str, Any]:
    """Compare all N*(N-1)/2 trajectory pairs.

    Each trajectory is (N, 4) with columns (t, x, y, psi).
    Returns dict with max_heading_diff_deg, max_position_diff_m, max_time_shift_s,
    and run_count.
    """
    n = len(trajectories)
    max_heading_diff = 0.0
    max_position_diff = 0.0
    max_time_shift = 0.0

    for i in range(n):
        for j in range(i + 1, n):
            heading_diffs = np.abs(
                _angle_diff(trajectories[i][:, 3], trajectories[j][:, 3])
            )
            max_hd = float(np.max(heading_diffs))
            max_heading_diff = max(max_heading_diff, max_hd)

            pos_diffs = np.sqrt(
                (trajectories[i][:, 1] - trajectories[j][:, 1]) ** 2
                + (trajectories[i][:, 2] - trajectories[j][:, 2]) ** 2
            )
            max_pd = float(np.max(pos_diffs))
            max_position_diff = max(max_position_diff, max_pd)

            ts = compute_time_shift(trajectories[i], trajectories[j])
            max_time_shift = max(max_time_shift, abs(ts))

    return {
        "run_count": n,
        "max_heading_diff_deg": math.degrees(max_heading_diff),
        "max_position_diff_m": max_position_diff,
        "max_time_shift_s": max_time_shift,
    }


def generate_report(diffs: dict[str, Any], output_dir: Path) -> Path:
    """Save determinism_report.json and return its path.

    Pass thresholds for Phase 1 deterministic runs:
    - heading < 0.01 deg
    - position < 0.001 m
    - time shift < 0.01 s
    """
    thresholds = {
        "heading_deg": 0.01,
        "position_m": 0.001,
        "time_shift_s": 0.01,
    }
    passed = (
        diffs["max_heading_diff_deg"] < thresholds["heading_deg"]
        and diffs["max_position_diff_m"] < thresholds["position_m"]
        and abs(diffs["max_time_shift_s"]) < thresholds["time_shift_s"]
    )
    report = DeterminismReport(
        run_count=diffs["run_count"],
        max_heading_diff_deg=diffs["max_heading_diff_deg"],
        max_position_diff_m=diffs["max_position_diff_m"],
        max_time_shift_s=diffs["max_time_shift_s"],
        passed=passed,
    )
    report_path = output_dir / "determinism_report.json"
    report_path.write_text(json.dumps(report.__dict__, indent=2))
    print(f"Determinism report saved to {report_path}")
    return report_path


def main() -> None:
    parser = argparse.ArgumentParser(
        description="D1.3.1 determinism replay — verify cross-run reproducibility (Phase 1 stub)"
    )
    parser.add_argument(
        "--scenario", required=True, help="Path to scenario YAML (not parsed in Phase 1)"
    )
    parser.add_argument(
        "--runs", type=int, default=10, help="Number of repeated runs (default: 10)"
    )
    parser.add_argument(
        "--seed", type=int, default=1, help="Base RNG seed (default: 1)"
    )
    parser.add_argument(
        "--output-dir", default="build/d1_3_1_replay", help="Output directory"
    )
    parser.add_argument(
        "--compare-run",
        type=int,
        default=None,
        help="Optional: run once with a second seed for cross-seed comparison",
    )
    args = parser.parse_args()

    scenario_path = Path(args.scenario)
    if not scenario_path.exists():
        print(f"Warning: scenario file not found: {args.scenario}", file=sys.stderr)

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    csv_paths: list[Path] = []

    print(f"Running {args.runs} simulations with seed={args.seed}...")
    for run_idx in range(args.runs):
        path = run_simulation(args.scenario, args.seed, str(output_dir))
        indexed = output_dir / f"run{run_idx:02d}_seed{args.seed:03d}.csv"
        path.rename(indexed)
        csv_paths.append(indexed)

    if args.compare_run is not None:
        print(f"Running 1 comparison simulation with seed={args.compare_run}...")
        comp_path = run_simulation(args.scenario, args.compare_run, str(output_dir))
        indexed = output_dir / f"compare_seed{args.compare_run:03d}.csv"
        comp_path.rename(indexed)
        csv_paths.append(indexed)

    trajectories = []
    for p in csv_paths:
        t, x, y, psi = load_trajectory_csv(p)
        trajectories.append(np.column_stack([t, x, y, psi]))

    diffs = compute_pairwise_diffs(trajectories)
    report_path = generate_report(diffs, output_dir)

    print(
        f"\nDeterminism check: {'PASS' if diffs['run_count'] and _det_passed(diffs) else 'FAIL'}"
    )
    print(f"  max_heading_diff: {diffs['max_heading_diff_deg']:.6f} deg")
    print(f"  max_position_diff: {diffs['max_position_diff_m']:.6f} m")
    print(f"  max_time_shift: {diffs['max_time_shift_s']:.6f} s")


def _det_passed(diffs: dict[str, Any]) -> bool:
    thresholds = {"heading_deg": 0.01, "position_m": 0.001, "time_shift_s": 0.01}
    return (
        diffs["max_heading_diff_deg"] < thresholds["heading_deg"]
        and diffs["max_position_diff_m"] < thresholds["position_m"]
        and abs(diffs["max_time_shift_s"]) < thresholds["time_shift_s"]
    )


if __name__ == "__main__":
    main()
