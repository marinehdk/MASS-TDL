"""D2.4 — Ornstein-Uhlenbeck TargetVessel + NcdmVessel tests."""
from __future__ import annotations
import sys, math, statistics
from pathlib import Path
from unittest.mock import MagicMock

class DummyLifecycleNode:
    def __init__(self, node_name, **kwargs):
        from unittest.mock import MagicMock
        self._logger = MagicMock()
        self.get_clock = MagicMock()

sys.modules["rclpy"] = MagicMock()
sys.modules["rclpy.lifecycle"] = MagicMock()
sys.modules["rclpy.lifecycle"].LifecycleNode = DummyLifecycleNode
sys.modules["rclpy.qos"] = MagicMock()
sys.modules["rclpy.duration"] = MagicMock()
sys.modules["sil_msgs"] = MagicMock()
sys.modules["sil_msgs.msg"] = MagicMock()
sys.modules["sil_msgs.srv"] = MagicMock()

sys.path.insert(0, str(Path(__file__).parents[2]))
sys.path.insert(0, str(Path(__file__).parents[3] / "sil_common"))
import pytest


class FakeDuration:
    def __init__(self, nanoseconds=0):
        self.nanoseconds = nanoseconds


class FakeTime:
    def __init__(self, nanoseconds=0):
        self.nanoseconds = nanoseconds

    def __sub__(self, other):
        return FakeDuration(self.nanoseconds - other.nanoseconds)

    def __iadd__(self, duration):
        self.nanoseconds += duration.nanoseconds
        return self

    def to_msg(self):
        return object()


sys.modules["rclpy.duration"].Duration = FakeDuration


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


def test_target_vessel_rng_reproducibility():
    import numpy as np
    from target_vessel.node import TargetVessel, TargetMode

    # Generate two RNGs with the same seed, one with different seed
    rng1a = np.random.default_rng(12345)
    rng1b = np.random.default_rng(12345)
    rng2 = np.random.default_rng(54321)

    tv1a = TargetVessel(mmsi=100, lat=63.44, lon=10.38, heading_deg=0.0, sog_kn=10.0,
                        mode=TargetMode.NCDM, ou_theta=0.05, ou_sigma=0.5, rng=rng1a)
    tv1b = TargetVessel(mmsi=100, lat=63.44, lon=10.38, heading_deg=0.0, sog_kn=10.0,
                        mode=TargetMode.NCDM, ou_theta=0.05, ou_sigma=0.5, rng=rng1b)
    tv2 = TargetVessel(mmsi=100, lat=63.44, lon=10.38, heading_deg=0.0, sog_kn=10.0,
                       mode=TargetMode.NCDM, ou_theta=0.05, ou_sigma=0.5, rng=rng2)

    headings_1a = []
    headings_1b = []
    headings_2 = []

    for _ in range(50):
        tv1a.step(dt=0.1)
        tv1b.step(dt=0.1)
        tv2.step(dt=0.1)
        headings_1a.append(tv1a.heading)
        headings_1b.append(tv1b.heading)
        headings_2.append(tv2.heading)

    # Assert same seed twice -> identical noise (headings are exactly the same)
    assert headings_1a == headings_1b
    # Assert different seed -> different (headings are different)
    assert headings_1a != headings_2


def test_target_vessel_node_rng_lifecycle():
    from target_vessel.node import TargetVesselNode
    from unittest.mock import MagicMock
    
    node = TargetVesselNode()
    
    # Mock declare_parameter and get_parameter
    params = {
        "default_targets_json": '[{"mmsi": 100, "lat": 63.44, "lon": 10.38, "heading_deg": 0.0, "sog_kn": 10.0, "mode": "ncdm"}]',
        "root_seed": 42,
        "episode": 1,
        "worker": 2
    }
    
    def mock_get_parameter(name):
        class MockParam:
            value = params[name]
        return MockParam()
        
    node.get_parameter = mock_get_parameter
    node.declare_parameter = MagicMock()
    
    # Configure 
    node.on_configure(MagicMock())
    assert len(node._targets) == 1
    t1 = node._targets[0]
    
    # Let's verify that the generator was created and stored in t1
    assert hasattr(t1, "rng")
    assert t1.rng is not None


def test_target_vessel_catchup_publishes_each_sim_step():
    from target_vessel.node import TargetVesselNode

    node = TargetVesselNode()
    node._targets = []
    node.add_target(100, 63.44, 10.38, 90.0, 10.0)
    node._tv_pub = MagicMock()
    node._last_sim_time = FakeTime(nanoseconds=0)
    node._last_pub_sim_time = 0.0
    clock = MagicMock()
    clock.now.return_value = FakeTime(nanoseconds=1_000_000_000)
    node.get_clock = MagicMock(return_value=clock)

    node._step_callback()

    assert node._tv_pub.publish.call_count == 10


def test_no_random_module_survives():
    import ast
    from pathlib import Path
    node_file = Path(__file__).parents[1] / "target_vessel" / "node.py"
    with open(node_file, "r") as f:
        tree = ast.parse(f.read(), filename=str(node_file))
        
    for node in ast.walk(tree):
        # Assert no import random
        if isinstance(node, ast.Import):
            for alias in node.names:
                assert alias.name != "random", "Found 'import random' in node.py!"
        if isinstance(node, ast.ImportFrom):
            assert node.module != "random", "Found 'from random import ...' in node.py!"
        
        # Assert no random.* call
        if isinstance(node, ast.Attribute):
            if isinstance(node.value, ast.Name) and node.value.id == "random":
                raise AssertionError("Found 'random.' usage in node.py!")
