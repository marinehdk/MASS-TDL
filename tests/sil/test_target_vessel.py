"""Tests for target_vessel node — linear kinematics, node container."""
import sys
from pathlib import Path

# Ensure the package is importable when running tests from the repo root
_pkg = Path(__file__).resolve().parents[3] / "src" / "sim_workbench" / "sil_nodes" / "target_vessel"
sys.path.insert(0, str(_pkg))

from target_vessel.node import TargetVesselNode, TargetVessel, TargetMode


def test_single_target_moves():
    """Eastward vessel at 10 kn → lat constant, lon increases."""
    t = TargetVessel(123456789, 63.4, 10.4, 90.0, 10.0)
    s0 = t.step(1.0)
    assert s0["lat"] == 63.4  # eastward → no lat change
    assert s0["lon"] > 10.4   # lon increases
    assert s0["mmsi"] == 123456789
    assert s0["mode"] == "replay"


def test_northward_target():
    """Northward vessel at 10 kn → lon constant, lat increases."""
    t = TargetVessel(987654321, 63.4, 10.4, 0.0, 10.0)
    s0 = t.step(1.0)
    assert s0["lon"] == 10.4
    assert s0["lat"] > 63.4


def test_node_add_and_step():
    """TargetVesselNode manages multiple targets correctly."""
    node = TargetVesselNode()
    node.add_target(1, 63.4, 10.4, 90.0, 12.0)
    node.add_target(2, 63.42, 10.4, 180.0, 8.0)
    results = node.step_all(1.0)
    assert len(results) == 2
    assert results[0]["lon"] > 10.4     # eastward
    assert results[1]["lat"] < 63.42    # southward


def test_ncdm_mode():
    """TargetMode.NCDM is accepted and stored."""
    t = TargetVessel(111, 63.0, 10.0, 45.0, 5.0, mode=TargetMode.NCDM)
    s = t.step(0.5)
    assert s["mode"] == "ncdm"


def test_node_add_target_string_mode():
    """add_target accepts string alias for mode."""
    node = TargetVesselNode()
    t = node.add_target(222, 63.0, 10.0, 0.0, 6.0, mode="intelligent")
    assert t.mode == TargetMode.INTELLIGENT


def test_zero_sog_stays_still():
    """Zero speed → no positional change."""
    t = TargetVessel(333, 50.0, 5.0, 90.0, 0.0)
    s = t.step(10.0)
    assert s["lat"] == 50.0
    assert s["lon"] == 5.0
