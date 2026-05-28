"""DEMO-1 imazu-01-ho end-to-end physical acceptance test.

Success condition: A-1 ~ A-9 + V1-V3 all PASS. Any FAIL = DEMO-1 not met.
Run command:
  npm run sys:start
  sleep 60
  pytest tools/sil/test_demo1_imazu01ho_e2e.py -v
"""
import math
import os
import sys
import pytest
import subprocess
import json

# Add project root to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))
from tools.sil._e2e_helpers import (
    _get, _post, _collect_topic, _topic_echo_once, _wait_until_sim_t,
)

SCENARIO = "imazu-01-ho"


@pytest.fixture(scope="module", autouse=True)
def lifecycle_session():
    """Reset + configure + activate scenario; tear down after all tests."""
    _post("/lifecycle/cleanup")
    cfg = _post("/lifecycle/configure", {"scenario_id": SCENARIO})
    assert cfg.get("success"), f"configure failed: {cfg}"
    act = _post("/lifecycle/activate")
    assert act.get("success"), f"activate failed: {act}"
    yield
    _post("/lifecycle/cleanup")


# ============================================================
# A-1 ~ A-9 — Demo-1 5-phase physical acceptance
# ============================================================

def test_A1_transit_straight():
    """Section 1 (T+0~200s): own_heading deviation from north <= 5 deg."""
    samples = _collect_topic("/sil/own_ship_state", from_sim_t=10, to_sim_t=190, n=10)
    assert len(samples) >= 5, f"only {len(samples)} samples collected"
    max_dev = max(abs(float(s.get("heading", 0))) for s in samples)
    assert max_dev <= math.radians(5.0), \
        f"transit heading drift {math.degrees(max_dev):.2f} deg > 5 deg"


def test_A2_rule14_triggered():
    """Section 2 (T+180~320s): applicable_rule switches to Rule 14."""
    samples = _collect_topic("/l3/m6/rule_assessment", from_sim_t=180, to_sim_t=320, n=20)
    rules = [str(s.get("applicable_rule", "")) for s in samples]
    assert any("Rule 14" in r for r in rules), \
        f"Rule 14 never triggered, observed rules: {set(rules)}"


def test_A3_starboard_turn():
    """Section 3 (T+200~500s): own_heading starboard turn 25-45 deg."""
    samples = _collect_topic("/sil/own_ship_state", from_sim_t=200, to_sim_t=500, n=30)
    headings_rad = [float(s.get("heading", 0)) for s in samples]
    max_hdg = max(headings_rad)
    assert math.radians(25) <= max_hdg <= math.radians(45), \
        f"turn magnitude {math.degrees(max_hdg):.1f} deg outside [25, 45]"


def test_A4_safe_cpa():
    """Section 4 min CPA >= 500m."""
    _wait_until_sim_t(620)
    s = _get("/scoring/last_run")
    assert s.get("kpis") is not None, f"scoring kpis null: {s}"
    min_cpa_nm = float(s["kpis"]["min_cpa_nm"])
    min_cpa_m = min_cpa_nm * 1852.0
    assert min_cpa_m >= 500.0, f"min_cpa={min_cpa_m:.0f}m < 500m"


def test_A5_return_to_nominal():
    """Section 4 end (t=650s): heading returns to 0 deg +/- 5 deg."""
    _wait_until_sim_t(650)
    msg = _topic_echo_once("/sil/own_ship_state")
    assert msg is not None, "own_ship_state topic silent at t=650s"
    hdg = float(msg.get("heading", 999))
    assert abs(hdg) <= math.radians(5.0), \
        f"return heading {math.degrees(hdg):.1f} deg not within +/-5 deg"


def test_A6_auto_stop():
    """Section 5: lifecycle inactive at sim_time=710s."""
    _wait_until_sim_t(710)
    status = _get("/lifecycle/status")
    assert status["current_state"] == "inactive", \
        f"lifecycle still '{status['current_state']}' at sim_t=710s"


def test_A7_scoring_complete():
    """Section 5: scoring returns verdict + dimensions."""
    s = _get("/scoring/last_run")
    assert s.get("kpis") is not None, f"kpis null: {s}"
    assert s.get("scoring_dimensions") is not None, f"scoring_dimensions null: {s}"
    assert s.get("verdict") in {"pass", "fail"}, f"unexpected verdict: {s.get('verdict')}"


def test_A8_decision_chain_real():
    """Section 3: M4 not in IvP infeasible fallback."""
    samples = _collect_topic("/l3/m4/behavior_plan", from_sim_t=300, to_sim_t=400, n=10)
    assert len(samples) >= 3, f"only {len(samples)} behavior_plan samples"
    for s in samples:
        rat = str(s.get("rationale", ""))
        assert "IvP infeasible" not in rat, \
            f"M4 still in fallback: rationale={rat!r}"


def test_A9_no_demo_path():
    """Demo dead-reckoning path fully removed."""
    r = subprocess.run(
        ["grep", "-c", "demo", "src/sil_orchestrator/main.py"],
        capture_output=True, text=True,
    )
    n = int(r.stdout.strip() or "0")
    assert n == 0, f"demo references remain in main.py: {n}"
    assert not os.path.exists("src/sil_orchestrator/demo_avoidance.py")
    assert not os.path.exists("src/sil_orchestrator/demo_scorer.py")


# ============================================================
# V1 ~ V3 — Validation suite
# ============================================================

def test_V1_m5_midmpc_runs():
    """V1: M5 Mid-MPC runs on main path (not geometric fallback)."""
    r = subprocess.run(
        ["bash", "tools/sil/v1_m5_midmpc_check.sh"],
        capture_output=True, text=True, timeout=420,
        cwd=os.path.join(os.path.dirname(__file__), "..", ".."),
    )
    assert r.returncode == 0, f"V1 fail:\n{r.stdout}\n{r.stderr}"


def test_V2_m7_independence():
    """V2: M7 has no algorithmic coupling to M4/M5 source headers."""
    r = subprocess.run(
        ["bash", "tools/sil/v2_m7_independence_audit.sh"],
        capture_output=True, text=True, timeout=30,
        cwd=os.path.join(os.path.dirname(__file__), "..", ".."),
    )
    assert r.returncode == 0, f"V2 fail:\n{r.stdout}\n{r.stderr}"


def test_V3_m2_threats():
    """V3: M2 World Model publishes threats during imazu-01-ho."""
    r = subprocess.run(
        ["bash", "tools/sil/v3_m2_threats_check.sh"],
        capture_output=True, text=True, timeout=60,
        cwd=os.path.join(os.path.dirname(__file__), "..", ".."),
    )
    assert r.returncode == 0, f"V3 fail:\n{r.stdout}\n{r.stderr}"
