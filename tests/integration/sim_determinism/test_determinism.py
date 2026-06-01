# tests/integration/sim_determinism/test_determinism.py
"""Cross-speed determinism regression test.

Requires the docker SIL stack running (network_mode: host).
Run with:
    pytest tests/integration/sim_determinism/ -m integration -v

PASS criteria (post-fix):
  - RTF @ rate=1.0 in [0.95, 1.05]
  - Trajectory at same sim_t grid: position < 1 m, heading < 0.1 deg,
    behavior/conflict sequence identical between 1x and 10x runs.
"""
from __future__ import annotations
import csv
import math
import os
import subprocess
import sys
import time
import uuid
from pathlib import Path

import pytest

# Path to capture script (relative to repo root)
CAPTURE_SCRIPT = Path(__file__).parent / "capture_imazu.py"

# Capture duration in wall seconds
CAPTURE_WALL_S_1X = 130   # sim 60 s @ rate=1 -> need ~65 s wall (pre-fix RTF~1.74 -> ~35 s wall)
CAPTURE_WALL_S_10X = 20   # sim 60 s @ rate=10 -> ~6 s wall

# Alignment grid step [sim seconds]
GRID_STEP_S = 2.0

# Tolerances (post-fix)
POS_TOL_M = 1.0
HDG_TOL_DEG = 0.1

# RTF tolerance
RTF_LOW = 0.95
RTF_HIGH = 1.05

# Minimum sim-time coverage for alignment [s]
MIN_SIM_COVERAGE_S = 30.0


def _get_container_name() -> str:
    try:
        res = subprocess.run(
            ["docker", "ps", "--filter", "name=sil-nodes", "--format", "{{.Names}}"],
            capture_output=True, text=True, check=True
        )
        name = res.stdout.strip().splitlines()[0]
        if name:
            return name
    except Exception:
        pass
    return "mass-l3-sil-sil-nodes-1"


def _run_capture_rule14_in_container(rate: float, duration: float, output: str) -> None:
    """Run capture_rule14.py inside the sil-nodes container."""
    container = _get_container_name()
    run_id = uuid.uuid4().hex[:8]
    container_script = f"/tmp/capture_rule14_{run_id}.py"
    container_output = f"/tmp/capture_{run_id}.csv"
    rule14_script = Path(__file__).parent / "capture_rule14.py"

    try:
        # Copy script into container
        subprocess.run(
            ["docker", "cp", str(rule14_script), f"{container}:{container_script}"],
            check=True
        )
        subprocess.run(
            ["docker", "exec", container,
             "bash", "-c",
             f"source /opt/ros/humble/setup.bash && "
             f"source /opt/ws/install/setup.bash && "
             f"python3 {container_script} "
             f"--rate {rate} --duration {duration} --output {container_output}"],
            check=True, timeout=int(duration) + 60
        )
        # Copy CSV back to host
        subprocess.run(
            ["docker", "cp", f"{container}:{container_output}", output],
            check=True
        )
    finally:
        # Clean up temporary files inside container
        subprocess.run(
            ["docker", "exec", container, "rm", "-f", container_script, container_output],
            capture_output=True
        )

def _run_capture_in_container(rate: float, duration: float, output: str) -> None:
    """Run capture_imazu.py inside the sil-nodes container."""
    container = _get_container_name()
    run_id = uuid.uuid4().hex[:8]
    container_script = f"/tmp/capture_imazu_{run_id}.py"
    container_output = f"/tmp/capture_{run_id}.csv"

    try:
        # Copy script into container
        subprocess.run(
            ["docker", "cp", str(CAPTURE_SCRIPT), f"{container}:{container_script}"],
            check=True
        )
        subprocess.run(
            ["docker", "exec", container,
             "bash", "-c",
             f"source /opt/ros/humble/setup.bash && "
             f"source /opt/ws/install/setup.bash && "
             f"python3 {container_script} "
             f"--rate {rate} --duration {duration} --output {container_output}"],
            check=True, timeout=int(duration) + 60
        )
        # Copy CSV back to host
        subprocess.run(
            ["docker", "cp", f"{container}:{container_output}", output],
            check=True
        )
    finally:
        # Clean up temporary files inside container
        subprocess.run(
            ["docker", "exec", container, "rm", "-f", container_script, container_output],
            capture_output=True
        )


def _load_csv(path: str) -> list[dict]:
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def _rows_by_sim_t(rows: list[dict]) -> dict[float, dict]:
    """Index rows by sim_t (float)."""
    return {float(r["sim_t"]): r for r in rows}


def _haversine_m(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    R = 6_371_000.0
    dlat = math.radians(lat2 - lat1)
    dlon = math.radians(lon2 - lon1)
    a = (math.sin(dlat / 2) ** 2 +
         math.cos(math.radians(lat1)) * math.cos(math.radians(lat2)) *
         math.sin(dlon / 2) ** 2)
    return 2 * R * math.asin(math.sqrt(a))


def _heading_diff(h1: float, h2: float) -> float:
    diff = abs(h1 - h2) % 360.0
    return diff if diff <= 180.0 else 360.0 - diff


def _align_on_grid(
    rows_a: dict[float, dict],
    rows_b: dict[float, dict],
    grid_step: float,
) -> list[tuple[dict, dict]]:
    """Return paired (row_a, row_b) at each grid sim_t step."""
    if not rows_a or not rows_b:
        return []
    t_min = max(min(rows_a), min(rows_b))
    t_max = min(max(rows_a), max(rows_b))
    grid_t = t_min
    pairs: list[tuple[dict, dict]] = []
    while grid_t <= t_max:
        closest_a = min(rows_a, key=lambda t: abs(t - grid_t))
        closest_b = min(rows_b, key=lambda t: abs(t - grid_t))
        if abs(closest_a - grid_t) < grid_step and abs(closest_b - grid_t) < grid_step:
            pairs.append((rows_a[closest_a], rows_b[closest_b]))
        grid_t += grid_step
    return pairs


@pytest.mark.integration
class TestSimDeterminism:

    def test_rtf_at_rate_1x(self, tmp_path):
        """RTF @ rate=1.0 must be in [0.95, 1.05]."""
        out = str(tmp_path / "rtf_1x.csv")
        _run_capture_in_container(rate=1.0, duration=30.0, output=out)

        rows = _load_csv(out)
        assert rows, "No rows captured -- is the stack running?"

        sim_elapsed = float(rows[-1]["sim_t"]) - float(rows[0]["sim_t"])
        wall_elapsed = float(rows[-1]["wall_t"]) - float(rows[0]["wall_t"])
        assert sim_elapsed > 5.0, f"Too little sim time captured: {sim_elapsed:.1f}s"

        # RTF strictly of the active simulation loop
        rtf = sim_elapsed / max(0.1, wall_elapsed)
        assert RTF_LOW <= rtf <= RTF_HIGH, (
            f"RTF={rtf:.3f} out of [{RTF_LOW}, {RTF_HIGH}]. "
            f"sim={sim_elapsed:.1f}s wall={wall_elapsed:.1f}s"
        )

    def test_rule14_determinism_1x_vs_10x(self, tmp_path):
        """1x and 10x runs of colreg-rule14-ho must produce identical trajectories."""
        out_1x = str(tmp_path / "det_rule14_1x.csv")
        out_10x = str(tmp_path / "det_rule14_10x.csv")

        _run_capture_rule14_in_container(rate=1.0, duration=120.0, output=out_1x)
        time.sleep(3.0)  # Let stack settle between runs
        _run_capture_rule14_in_container(rate=10.0, duration=120.0, output=out_10x)

        rows_1x = _load_csv(out_1x)
        rows_10x = _load_csv(out_10x)
        assert rows_1x, "No rows in 1x rule14 capture"
        assert rows_10x, "No rows in 10x rule14 capture"

        by_t_1x = _rows_by_sim_t(rows_1x)
        by_t_10x = _rows_by_sim_t(rows_10x)

        pairs = _align_on_grid(by_t_1x, by_t_10x, GRID_STEP_S)
        assert len(pairs) >= int(MIN_SIM_COVERAGE_S / GRID_STEP_S), (
            f"Too few aligned pairs: {len(pairs)} (need >={MIN_SIM_COVERAGE_S/GRID_STEP_S:.0f})"
        )

        max_pos_err = 0.0
        max_hdg_err = 0.0
        max_act_rudder_err = 0.0
        max_latch_offset_err = 0.0
        behavior_mismatches = 0
        conflict_mismatches = 0

        for r1, r10 in pairs:
            pos_err = _haversine_m(
                float(r1["lat"]), float(r1["lon"]),
                float(r10["lat"]), float(r10["lon"])
            )
            hdg_err = _heading_diff(float(r1["heading_deg"]), float(r10["heading_deg"]))
            act_rudder_err = abs(float(r1["act_rudder_deg"]) - float(r10["act_rudder_deg"]))
            latch_offset_err = abs(float(r1["latch_offset"]) - float(r10["latch_offset"]))
            
            max_pos_err = max(max_pos_err, pos_err)
            max_hdg_err = max(max_hdg_err, hdg_err)
            max_act_rudder_err = max(max_act_rudder_err, act_rudder_err)
            max_latch_offset_err = max(max_latch_offset_err, latch_offset_err)
            
            if r1["behavior"] != r10["behavior"]:
                behavior_mismatches += 1
            if r1["conflict"] != r10["conflict"]:
                conflict_mismatches += 1

        print(f"[rule14] Max pos err: {max_pos_err:.2f} m, Max hdg err: {max_hdg_err:.3f} deg, "
              f"Max act rudder err: {max_act_rudder_err:.3f} deg, Max latch offset err: {max_latch_offset_err:.3f} deg")

        assert max_pos_err < POS_TOL_M, (
            f"Max position error {max_pos_err:.2f} m exceeds {POS_TOL_M} m"
        )
        assert max_hdg_err < HDG_TOL_DEG, (
            f"Max heading error {max_hdg_err:.3f}deg exceeds {HDG_TOL_DEG}deg"
        )
        assert max_act_rudder_err < 0.1, (
            f"Max act rudder error {max_act_rudder_err:.3f}deg exceeds 0.1 deg"
        )
        assert max_latch_offset_err < 0.1, (
            f"Max latch offset error {max_latch_offset_err:.3f}deg exceeds 0.1 deg"
        )
        assert behavior_mismatches == 0, (
            f"{behavior_mismatches} behavior mismatches at aligned sim_t"
        )
        assert conflict_mismatches == 0, (
            f"{conflict_mismatches} conflict mismatches at aligned sim_t"
        )

    def test_cross_speed_determinism_1x_vs_10x(self, tmp_path):
        """1x and 10x runs of imazu-01-ho must produce identical trajectories."""
        out_1x = str(tmp_path / "det_1x.csv")
        out_10x = str(tmp_path / "det_10x.csv")

        _run_capture_in_container(rate=1.0, duration=CAPTURE_WALL_S_1X, output=out_1x)
        time.sleep(3.0)  # Let stack settle between runs
        _run_capture_in_container(rate=10.0, duration=CAPTURE_WALL_S_10X, output=out_10x)

        rows_1x = _load_csv(out_1x)
        rows_10x = _load_csv(out_10x)
        assert rows_1x, "No rows in 1x capture"
        assert rows_10x, "No rows in 10x capture"

        by_t_1x = _rows_by_sim_t(rows_1x)
        by_t_10x = _rows_by_sim_t(rows_10x)

        pairs = _align_on_grid(by_t_1x, by_t_10x, GRID_STEP_S)
        assert len(pairs) >= int(MIN_SIM_COVERAGE_S / GRID_STEP_S), (
            f"Too few aligned pairs: {len(pairs)} (need >={MIN_SIM_COVERAGE_S/GRID_STEP_S:.0f})"
        )

        max_pos_err = 0.0
        max_hdg_err = 0.0
        behavior_mismatches = 0
        conflict_mismatches = 0

        for r1, r10 in pairs:
            pos_err = _haversine_m(
                float(r1["lat"]), float(r1["lon"]),
                float(r10["lat"]), float(r10["lon"])
            )
            hdg_err = _heading_diff(float(r1["heading_deg"]), float(r10["heading_deg"]))
            max_pos_err = max(max_pos_err, pos_err)
            max_hdg_err = max(max_hdg_err, hdg_err)
            if r1["behavior"] != r10["behavior"]:
                behavior_mismatches += 1
            if r1["conflict"] != r10["conflict"]:
                conflict_mismatches += 1

        assert max_pos_err < POS_TOL_M, (
            f"Max position error {max_pos_err:.2f} m exceeds {POS_TOL_M} m"
        )
        assert max_hdg_err < HDG_TOL_DEG, (
            f"Max heading error {max_hdg_err:.3f}deg exceeds {HDG_TOL_DEG}deg"
        )
        assert behavior_mismatches == 0, (
            f"{behavior_mismatches} behavior mismatches at aligned sim_t"
        )
        assert conflict_mismatches == 0, (
            f"{conflict_mismatches} conflict mismatches at aligned sim_t"
        )
