# tests/integration/sim_determinism/test_avoidance_chain.py
"""Master acceptance test for DEMO-1 avoidance chain fix.

Encodes all AC-1…AC-7 from the implementation plan (2026-05-30).
Requires the full SIL stack running (`npm run sys:start` from repo root).

Run:
    pytest tests/integration/sim_determinism/test_avoidance_chain.py -m integration -v --timeout=700
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

CAPTURE_SCRIPT = Path(__file__).parent / "capture_rule14_boundary.py"
SCENARIO = "colreg-rule14-ho"
RATE = 1.0
DURATION_SIM_S = 650.0      # covers full avoidance + return-to-route
WALL_TIMEOUT_S = 1400       # conservative: 650 s sim at RTF=0.9 ≈ 722 s wall

# Acceptance thresholds
RTF_LOW  = 0.9
RTF_HIGH = 1.1
MIN_CPA_M = 500.0            # AC-2: real haversine min distance own↔target
XTE_CLOSE_M = 50.0           # AC-4: final XTE < 50 m
XTE_CLOSE_WINDOW_S = 30.0    # last N sim-seconds to evaluate XTE close
TASK_VALID_VALUE = 1          # MissionGoal.task_validity == VALID
AVOIDANCE_STATUS_NORMAL = "NORMAL"  # AC-6

def _haversine_m(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    """Haversine distance in metres between two lat/lon points."""
    R = 6_371_000.0
    dlat = math.radians(lat2 - lat1)
    dlon = math.radians(lon2 - lon1)
    a = (math.sin(dlat / 2) ** 2
         + math.cos(math.radians(lat1)) * math.cos(math.radians(lat2))
         * math.sin(dlon / 2) ** 2)
    return 2.0 * R * math.asin(math.sqrt(a))

def _get_container_name() -> str:
    try:
        res = subprocess.run(
            ["docker", "ps", "--filter", "name=sil-nodes", "--format", "{{.Names}}"],
            capture_output=True, text=True, check=True)
        lines = res.stdout.strip().splitlines()
        if lines and lines[0]:
            return lines[0]
    except Exception:
        pass
    return "mass-l3-sil-sil-nodes-1"

def _run_capture_in_container(output_host: str) -> None:
    container = _get_container_name()
    run_id = uuid.uuid4().hex[:8]
    container_script = f"/tmp/cap_boundary_{run_id}.py"
    container_output = f"/tmp/cap_boundary_{run_id}.csv"
    try:
        subprocess.run(["docker", "cp", str(CAPTURE_SCRIPT),
                        f"{container}:{container_script}"], check=True)
        subprocess.run(
            ["docker", "exec", container, "bash", "-c",
             f"export ROS_DOMAIN_ID=0 && "
             f"export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp && "
             f"source /opt/ros/humble/setup.bash && "
             f"source /opt/ws/install/setup.bash && "
             f"python3 {container_script} "
             f"--scenario {SCENARIO} --rate {RATE} "
             f"--duration {DURATION_SIM_S} --output {container_output}"],
            check=True, timeout=WALL_TIMEOUT_S)
        subprocess.run(["docker", "cp", f"{container}:{container_output}",
                        output_host], check=True)
    finally:
        subprocess.run(["docker", "exec", container, "rm", "-f",
                        container_script, container_output], capture_output=True)

def _load_csv(path: str) -> list[dict]:
    with open(path, newline="") as f:
        return list(csv.DictReader(f))

@pytest.mark.integration
class TestAvoidanceChain:
    """All AC-1…AC-7 must pass simultaneously for DEMO-1 sign-off."""

    @pytest.fixture(scope="class")
    def capture_data(self, tmp_path_factory):
        """Run one capture, reuse across all tests in class."""
        out = str(tmp_path_factory.mktemp("cap") / "boundary.csv")
        _run_capture_in_container(out)
        rows = _load_csv(out)
        assert rows, f"No rows in capture CSV at {out} — is the stack running?"
        return rows

    # ── AC-7: RTF ──────────────────────────────────────────────────────────
    def test_ac7_rtf(self, capture_data):
        """RTF @ rate=1.0 must be in [0.9, 1.1]."""
        rows = capture_data
        sim_start = float(rows[0]["sim_t"])
        sim_end   = float(rows[-1]["sim_t"])
        wall_start = float(rows[0]["wall_t"])
        wall_end   = float(rows[-1]["wall_t"])
        sim_elapsed  = sim_end  - sim_start
        wall_elapsed = wall_end - wall_start
        assert sim_elapsed > 30.0, f"Too little sim time: {sim_elapsed:.1f}s"
        rtf = sim_elapsed / max(0.1, wall_elapsed)
        assert RTF_LOW <= rtf <= RTF_HIGH, (
            f"AC-7 FAIL: RTF={rtf:.3f} not in [{RTF_LOW},{RTF_HIGH}]. "
            f"sim={sim_elapsed:.1f}s wall={wall_elapsed:.1f}s")

    # ── AC-1: current_target_wp != (0,0), task_validity VALID ─────────────
    def test_ac1_task_validity_and_target_wp(self, capture_data):
        """Within 60 sim-seconds of start, task_validity==VALID and ctwp is non-zero."""
        rows = capture_data
        sim_t0 = float(rows[0]["sim_t"])
        valid_rows = [
            r for r in rows
            if int(r["task_validity"]) == TASK_VALID_VALUE
            and (float(r["sim_t"]) - sim_t0) < 60.0
        ]
        assert valid_rows, (
            "AC-1 FAIL: task_validity never reached VALID within 60 sim-s. "
            f"Observed values: {set(r['task_validity'] for r in rows[:50])}")

        wp_nonzero = [
            r for r in valid_rows
            if abs(float(r["ctwp_lat"])) > 1e-4 or abs(float(r["ctwp_lon"])) > 1e-4
        ]
        assert wp_nonzero, (
            "AC-1 FAIL: task_validity==VALID but current_target_wp is always (0,0). "
            f"First VALID row: ctwp_lat={valid_rows[0]['ctwp_lat']} ctwp_lon={valid_rows[0]['ctwp_lon']}")

    # ── AC-2: real haversine min CPA >= 500 m ─────────────────────────────
    def test_ac2_min_cpa(self, capture_data):
        """Minimum haversine distance own↔target during COLREG_AVOID must be >= 500 m.

        Implements the ORCHESTRATOR CORRECTION (2026-05-30): AC-2 uses real
        geometric CPA (haversine own_lat/lon ↔ target_lat/lon), NOT turn_radius
        as proxy. Secondary signal: avoidance_turn_r > 50 (NLP path, not fallback).
        """
        rows = capture_data
        avoid_rows = [r for r in rows if r["behavior"] == "COLREG_AVOID"]
        assert avoid_rows, "AC-2/AC-3 FAIL: behavior never reached COLREG_AVOID"

        # Filter rows where target position was received (non-zero)
        cpa_rows = [
            r for r in avoid_rows
            if abs(float(r["target_lat"])) > 1e-4 or abs(float(r["target_lon"])) > 1e-4
        ]
        assert cpa_rows, (
            "AC-2 FAIL: no avoidance rows have valid target_lat/lon — "
            "/sil/target_vessel_state not received during COLREG_AVOID")

        min_cpa = min(
            _haversine_m(
                float(r["lat"]), float(r["lon"]),
                float(r["target_lat"]), float(r["target_lon"]))
            for r in cpa_rows
        )
        assert min_cpa >= MIN_CPA_M, (
            f"AC-2 FAIL: min CPA = {min_cpa:.1f}m < {MIN_CPA_M}m (haversine own↔target). "
            f"CPA undershoot: need >= 500m for colreg-rule14-ho safety.")

        # Secondary: verify at least some avoidance rows have NLP turn_radius (not 50m fallback)
        nlp_rows = [r for r in avoid_rows if float(r["avoidance_turn_r"]) > 50.0]
        if not nlp_rows:
            import warnings
            warnings.warn(
                "AC-2 secondary: all avoidance_turn_r <= 50m (geometric fallback). "
                "CPA may still be >= 500m but MPC NLP did not converge (see AC-6).")

    # ── AC-3: AVOID → TRANSIT transition ──────────────────────────────────
    def test_ac3_avoid_then_transit(self, capture_data):
        """Behavior sequence must include COLREG_AVOID followed by TRANSIT."""
        rows = capture_data
        behaviors = [r["behavior"] for r in rows if r["behavior"]]
        try:
            avoid_idx = next(i for i, b in enumerate(behaviors) if b == "COLREG_AVOID")
        except StopIteration:
            pytest.fail("AC-3 FAIL: COLREG_AVOID never observed")
        post_avoid = behaviors[avoid_idx:]
        assert "TRANSIT" in post_avoid, (
            "AC-3 FAIL: no TRANSIT observed after COLREG_AVOID. "
            f"Post-avoid behaviors: {set(post_avoid)}")

    # ── AC-4: post-avoidance XTE closes to < 50 m ─────────────────────────
    def test_ac4_xte_closure(self, capture_data):
        """In the final 30 sim-s of capture, |xte_m| < 50 m."""
        rows = capture_data
        sim_end = float(rows[-1]["sim_t"])
        window_rows = [
            r for r in rows
            if float(r["sim_t"]) >= sim_end - XTE_CLOSE_WINDOW_S
        ]
        assert window_rows, "AC-4: no rows in final window"
        max_xte = max(abs(float(r["xte_m"])) for r in window_rows)
        assert max_xte < XTE_CLOSE_M, (
            f"AC-4 FAIL: max |xte_m|={max_xte:.1f}m in final {XTE_CLOSE_WINDOW_S}s "
            f"(threshold={XTE_CLOSE_M}m)")

    # ── AC-5: MissionGoal.xte_nm != -1 ────────────────────────────────────
    def test_ac5_xte_nm_populated(self, capture_data):
        """MissionGoal.xte_nm must be != -1 (L3 geometric XTE computed)."""
        rows = capture_data
        active_rows = [
            r for r in rows
            if int(r["task_validity"]) == TASK_VALID_VALUE
            and r["behavior"] in ("TRANSIT", "COLREG_AVOID")
        ]
        assert active_rows, "AC-5: no active rows to evaluate xte_nm"
        stuck_neg1 = [r for r in active_rows if abs(float(r["xte_nm"]) - (-1.0)) < 0.001]
        assert len(stuck_neg1) < len(active_rows), (
            f"AC-5 FAIL: xte_nm == -1.0 in ALL {len(stuck_neg1)} active rows "
            "(L3-geometric XTE never computed)")

    # ── AC-6: avoidance_plan.status == "NORMAL" (MPC converged) ───────────
    def test_ac6_mpc_converged(self, capture_data):
        """During COLREG_AVOID, avoidance_plan.status must reach NORMAL."""
        rows = capture_data
        avoid_rows = [r for r in rows if r["behavior"] == "COLREG_AVOID"]
        assert avoid_rows, "AC-6: no COLREG_AVOID rows"
        normal_rows = [r for r in avoid_rows
                       if r["avoidance_status"] == AVOIDANCE_STATUS_NORMAL]
        assert normal_rows, (
            f"AC-6 FAIL: avoidance_plan.status never NORMAL during COLREG_AVOID. "
            f"Observed statuses: {set(r['avoidance_status'] for r in avoid_rows)}")
