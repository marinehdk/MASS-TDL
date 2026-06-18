from __future__ import annotations

import json
import math
import sys
from pathlib import Path
from unittest.mock import MagicMock


class DummyLifecycleNode:
    def __init__(self, node_name, **kwargs):
        self._logger = MagicMock()
        self.get_clock = MagicMock()


sys.modules["rclpy"] = MagicMock()
sys.modules["rclpy.lifecycle"] = MagicMock()
sys.modules["rclpy.lifecycle"].LifecycleNode = DummyLifecycleNode
sys.modules["rclpy.qos"] = MagicMock()
sys.modules["sil_msgs"] = MagicMock()
sys.modules["sil_msgs.msg"] = MagicMock()
sys.modules["sil_msgs.srv"] = MagicMock()

sys.path.insert(0, str(Path(__file__).parents[2]))
sys.path.insert(0, str(Path(__file__).parents[3] / "sil_common"))

from target_vessel.config import TargetBehaviorConfig
from target_vessel.geometry import VesselKinematics
from target_vessel.node import TargetMode, TargetVessel, TargetVesselNode


def test_intelligent_target_uses_colregs_fsm_with_ownship_observation():
    target = TargetVessel(
        mmsi=100,
        lat=63.01,
        lon=10.0,
        heading_deg=180.0,
        sog_kn=10.0,
        mode=TargetMode.INTELLIGENT,
        behavior_config=TargetBehaviorConfig(
            policy="colregs_rule_fsm",
            reaction_delay_s=0.0,
            min_turn_deg=30.0,
            rot_limit_deg_s=3.0,
        ),
    )
    own = VesselKinematics(lat=63.0, lon=10.0, heading_deg=0.0, sog_mps=10.0 * 0.514444)
    state = target.step(dt=1.0, ownship=own, now_s=1.0)
    assert state["mode"] == "intelligent"
    assert math.degrees(target.heading) > 180.0
    assert state["rot"] > 0.0


def test_colregs_target_without_ownship_degrades_to_nominal():
    target = TargetVessel(
        mmsi=101,
        lat=63.01,
        lon=10.0,
        heading_deg=180.0,
        sog_kn=10.0,
        mode=TargetMode.INTELLIGENT,
        behavior_config=TargetBehaviorConfig(policy="colregs_rule_fsm", reaction_delay_s=0.0),
    )
    state = target.step(dt=1.0, ownship=None, now_s=1.0)
    assert math.degrees(target.heading) == 180.0
    assert state["rot"] == 0.0


def test_node_configure_rejects_multiple_colregs_targets():
    node = TargetVesselNode()
    entries = [
        {
            "static": {"mmsi": 100},
            "initial": {"position": {"latitude": 63.0, "longitude": 10.0}, "heading": 0.0, "sog": 10.0},
            "source": {"type": "route"},
            "behavior": {"policy": "colregs_rule_fsm"},
        },
        {
            "static": {"mmsi": 101},
            "initial": {"position": {"latitude": 63.1, "longitude": 10.0}, "heading": 180.0, "sog": 10.0},
            "source": {"type": "route"},
            "behavior": {"policy": "colregs_rule_fsm"},
        },
    ]
    params = {"default_targets_json": json.dumps(entries), "root_seed": 0, "episode": 0, "worker": 0}
    node.declare_parameter = MagicMock()
    node.get_parameter = lambda name: type("P", (), {"value": params[name]})()
    result = node.on_configure(MagicMock())
    assert "ERROR" in str(result)


def test_handle_ownship_state_uses_heading_degrees_from_message():
    node = TargetVesselNode()
    msg = type(
        "OwnShipStateMsg",
        (),
        {"lat": 63.0, "lon": 10.0, "heading": 725.0, "sog": 4.0},
    )()
    node._handle_ownship_state(msg)
    assert node._latest_ownship is not None
    assert node._latest_ownship.heading_deg == 5.0
    assert node._latest_ownship.sog_mps == 4.0
