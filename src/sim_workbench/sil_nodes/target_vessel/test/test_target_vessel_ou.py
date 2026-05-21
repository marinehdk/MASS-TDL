"""D2.4 — Ornstein-Uhlenbeck TargetVessel + NcdmVessel tests."""
from __future__ import annotations
import sys, math, statistics
from pathlib import Path
from unittest.mock import MagicMock

sys.modules["rclpy"] = MagicMock()
sys.modules["rclpy.lifecycle"] = MagicMock()
sys.modules["rclpy.qos"] = MagicMock()
sys.modules["sil_msgs"] = MagicMock()
sys.modules["sil_msgs.msg"] = MagicMock()

sys.path.insert(0, str(Path(__file__).parents[2]))
import pytest


def test_target_vessel_accepts_ou_params():
    from target_vessel.node import TargetVessel, TargetMode
    tv = TargetVessel(mmsi=100, lat=63.44, lon=10.38, heading_deg=0.0, sog_kn=10.0,
                      mode=TargetMode.NCDM, ou_theta=0.05, ou_sigma=0.5)
    assert tv.mode.value == "ncdm"


def test_ou_heading_drifts_from_reference():
    from target_vessel.node import TargetVessel, TargetMode
    tv = TargetVessel(mmsi=101, lat=63.44, lon=10.38, heading_deg=90.0, sog_kn=10.0,
                      mode=TargetMode.NCDM, ou_theta=0.05, ou_sigma=1.0)
    headings = [math.degrees(tv.heading)]
    for _ in range(600):
        tv.step(dt=0.1)
        headings.append(math.degrees(tv.heading))
    assert statistics.stdev(headings) > 0.5


def test_ou_heading_reverts_to_reference():
    from target_vessel.node import TargetVessel, TargetMode
    tv = TargetVessel(mmsi=102, lat=63.44, lon=10.38, heading_deg=90.0, sog_kn=10.0,
                      mode=TargetMode.NCDM, ou_theta=1.0, ou_sigma=0.1)
    headings_deg = []
    for _ in range(3000):
        tv.step(dt=0.1)
        headings_deg.append(math.degrees(tv.heading))
    assert abs(statistics.mean(headings_deg) - 90.0) < 5.0


def test_ncdm_vessel_generates_trajectory():
    sys.path.insert(0, str(Path(__file__).parents[3] / "scenario_authoring"))
    from scenario_authoring.replay.target_modes import NcdmVessel
    nv = NcdmVessel(lat0=63.44, lon0=10.38, heading0_deg=90.0, sog_kn=10.0,
                    duration_s=60.0, dt=0.1, ou_theta=0.05, ou_sigma=0.5, seed=42)
    state = nv.get_targets_at(30.0)
    assert state is not None
    assert "lat" in state and "cog_deg" in state


def test_ncdm_vessel_out_of_range_returns_none():
    sys.path.insert(0, str(Path(__file__).parents[3] / "scenario_authoring"))
    from scenario_authoring.replay.target_modes import NcdmVessel
    nv = NcdmVessel(lat0=63.44, lon0=10.38, heading0_deg=0.0, sog_kn=5.0,
                    duration_s=30.0, dt=0.1, seed=1)
    assert nv.get_targets_at(-1.0) is None
    assert nv.get_targets_at(999.0) is None
