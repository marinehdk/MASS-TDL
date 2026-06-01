"""Scenario trace test — behavioral assertions via REST debug probe.

Requires: npm run sys:start (stack must be running).

Usage:
    pytest tools/sil/test_scenario_trace.py -v -s
    pytest tools/sil/test_scenario_trace.py -v -s --scenario imazu-01-ho
"""
from __future__ import annotations

import os
import sys

import pytest

# Allow imports from project root (tools/sil/_e2e_helpers, etc.)
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from tools.sil._e2e_helpers import _get, _post, _wait_until_sim_t  # noqa: E402

SIM_END_T = 700        # sim-seconds to wait before asserting
WAIT_TIMEOUT = 1200    # wall-clock timeout (seconds)


@pytest.fixture(scope="module")
def scenario_id(request):
    return request.config.getoption("--scenario")


@pytest.fixture(scope="module")
def summary(scenario_id):
    """Run scenario end-to-end and return /debug/summary.

    Cleanup runs even if assertions fail (yield fixture).
    """
    _post("/lifecycle/cleanup")

    # Truncate the trace file on the host if it exists to prevent stale data reading
    trace_path = os.path.join(os.path.dirname(__file__), "..", "..", "runs", "trace_current.jsonl")
    if os.path.exists(trace_path):
        try:
            with open(trace_path, "w") as f:
                f.truncate(0)
        except Exception:
            pass

    cfg = _post("/lifecycle/configure", {"scenario_id": scenario_id})
    assert cfg.get("success"), f"configure failed: {cfg}"
    
    # Run simulation at normal rate (1.0x) for closed-loop control stability
    try:
        _post("/lifecycle/rate", {"rate": 1.0})
    except Exception:
        pass

    act = _post("/lifecycle/activate")
    assert act.get("success"), f"activate failed: {act}"

    _wait_until_sim_t(SIM_END_T, timeout_wall_s=WAIT_TIMEOUT)
    result = _get("/debug/summary")
    yield result
    _post("/lifecycle/cleanup")


class TestScenarioTrace:
    """Behavioral gate tests for a full scenario run.

    Each test assertion includes diagnostic context so failures are
    immediately actionable without opening Foxglove or running docker exec.
    """

    def test_m3_task_validity_ever_valid(self, summary):
        """M3 must publish task_validity=1 (VALID) at some point."""
        timeline = summary.get("m3_task_validity_timeline", [])
        valid = [e for e in timeline if e.get("task_validity") == 1]
        assert valid, (
            "M3 task_validity never reached VALID (1) — likely K1 root cause "
            "(stuck target WP at (0,0)). "
            f"Full M3 timeline: {timeline}"
        )

    def test_m4_entered_non_transit(self, summary):
        """M4 must leave TRANSIT at least once (avoidance behavior triggered)."""
        phases = summary.get("m4_phase_timeline", [])
        non_transit = [p for p in phases if p["phase"] != "TRANSIT"]
        assert non_transit, (
            "M4 never left TRANSIT — avoidance behavior never triggered. "
            f"Full M4 phase timeline: {phases}"
        )

    def test_m4_non_transit_duration_min_10s(self, summary):
        """Non-TRANSIT avoidance phase must last ≥ 10 sim-seconds."""
        phases = summary.get("m4_phase_timeline", [])
        total = sum(
            (p.get("to_sim_t") or 0.0) - p["from_sim_t"]
            for p in phases
            if p["phase"] != "TRANSIT" and p.get("to_sim_t") is not None
        )
        assert total >= 10.0, (
            f"Non-TRANSIT avoidance lasted only {total:.1f}s — too brief for COLREGs compliance. "
            f"Phase timeline: {phases}"
        )

    def test_starboard_turn_magnitude(self, summary):
        """Own-ship max heading must be in [20, 55] deg (COLREGs Rule 14 starboard)."""
        max_hdg = summary.get("max_heading_deg", 0.0)
        traj = summary.get("own_ship_trajectory_sampled", [])
        assert 20.0 <= max_hdg <= 55.0, (
            f"Max heading {max_hdg:.1f}° outside Rule-14 starboard range [20, 55]. "
            f"Trajectory sample: {traj[:5]}"
        )

    def test_m5_delivers_valid_plans(self, summary):
        """M5 must deliver at least one VALID avoidance plan."""
        stats = summary.get("m5_solver_stats", {})
        valid_count = stats.get("VALID", 0)
        total = stats.get("total", 0)
        assert valid_count > 0, (
            f"M5 delivered 0 VALID plans out of {total} total. "
            f"All statuses: {stats} — MPC NLP likely infeasible (cold-start issue)."
        )

    def test_route_return_after_avoidance(self, summary):
        """M4 must return to TRANSIT after avoidance (route-return complete)."""
        phases = summary.get("m4_phase_timeline", [])
        names = [p["phase"] for p in phases]
        assert names and names[-1] == "TRANSIT", (
            f"M4 did not return to TRANSIT after avoidance. "
            f"Final phase sequence: {names}"
        )

    def test_no_m7_veto_events(self, summary):
        """M7 checker must not have raised any veto during the scenario."""
        vetos = summary.get("veto_events", [])
        assert not vetos, (
            f"Unexpected M7 veto events ({len(vetos)} total): {vetos[:3]}"
        )
