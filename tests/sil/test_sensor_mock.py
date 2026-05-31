import sys
import types
from pathlib import Path
from unittest.mock import Mock

# Check if rclpy is importable and has lifecycle module
try:
    import rclpy
    import rclpy.lifecycle
    import rclpy.qos
    from rclpy.lifecycle import LifecycleState
    HAS_ROS2 = True
except ImportError:
    HAS_ROS2 = False

if not HAS_ROS2:
    # Set up mock rclpy modules
    mock_rclpy = types.ModuleType("rclpy")
    mock_rclpy.logging = types.ModuleType("rclpy.logging")
    
    mock_rclpy_lifecycle = types.ModuleType("rclpy.lifecycle")
    class LifecycleNode:
        def __init__(self, *args, **kwargs):
            self._parameters = {}
            for k, v in kwargs.items():
                self._parameters[k] = v

        def declare_parameter(self, name, default_value=None):
            class Param:
                value = default_value
            self._parameters[name] = Param()
            return self._parameters[name]

        def get_parameter(self, name):
            class Param:
                value = None
            return self._parameters.get(name, Param())

        def get_logger(self):
            return Mock()
    mock_rclpy_lifecycle.LifecycleNode = LifecycleNode
    mock_rclpy_lifecycle.LifecycleState = Mock()
    mock_rclpy_lifecycle.TransitionCallbackReturn = Mock()
    
    mock_rclpy_qos = types.ModuleType("rclpy.qos")
    class DummyQoS:
        def __init__(self, *args, **kwargs):
            pass
    mock_rclpy_qos.QoSProfile = DummyQoS
    mock_rclpy_qos.DurabilityPolicy = Mock()
    mock_rclpy_qos.HistoryPolicy = Mock()
    mock_rclpy_qos.ReliabilityPolicy = Mock()
    
    mock_rclpy.lifecycle = mock_rclpy_lifecycle
    mock_rclpy.qos = mock_rclpy_qos
    
    sys.modules["rclpy"] = mock_rclpy
    sys.modules["rclpy.lifecycle"] = mock_rclpy_lifecycle
    sys.modules["rclpy.qos"] = mock_rclpy_qos

    # Mock sil_msgs
    mock_sil = types.ModuleType("sil_msgs")
    
    class MockMsgModule(types.ModuleType):
        def __getattr__(self, name):
            class DummyMessage:
                def __init__(self, **kwargs):
                    for k, v in kwargs.items():
                        setattr(self, k, v)
            DummyMessage.__name__ = name
            return DummyMessage
            
    mock_sil_msg = MockMsgModule("sil_msgs.msg")
    mock_sil.msg = mock_sil_msg
    
    sys.modules["sil_msgs"] = mock_sil
    sys.modules["sil_msgs.msg"] = mock_sil_msg

from sensor_mock.node import SensorMockNode


def test_ais_no_drop():
    node = SensorMockNode(ais_drop_pct=0)
    if not HAS_ROS2:
        node.ais_drop_pct = 0.0
    msg = node.generate_ais(63.4, 10.4, {"mmsi": 1, "sog": 5.0, "cog": 0.5, "lat": 63.41, "lon": 10.41, "heading": 0.5})
    assert msg is not None
    assert msg["dropout_flag"] is False


def test_ais_100pct_drop():
    node = SensorMockNode(ais_drop_pct=100)
    if not HAS_ROS2:
        node.ais_drop_pct = 100.0
    msg = node.generate_ais(63.4, 10.4, {"mmsi": 1, "sog": 5.0, "cog": 0.5, "lat": 63.41, "lon": 10.41, "heading": 0.5})
    assert msg is None


def test_radar_detects_nearby_target():
    node = SensorMockNode()
    if not HAS_ROS2:
        node.radar_max_range = 12000.0
        node.range_sigma_m = 3.0
        node.bearing_sigma_rad = 0.005
        node.rcs_min = 10.0
        node.rcs_max = 50.0
        node.clutter_max = 5
    result = node.generate_radar(63.4, 10.4, [{"lat": 63.401, "lon": 10.401, "heading": 0.0}])
    assert len(result["polar_targets"]) == 1
    assert "clutter_cardinality" in result


def test_radar_ignores_far_target():
    node = SensorMockNode(radar_max_range=100)
    if not HAS_ROS2:
        node.radar_max_range = 100.0
        node.range_sigma_m = 3.0
        node.bearing_sigma_rad = 0.005
        node.rcs_min = 10.0
        node.rcs_max = 50.0
        node.clutter_max = 5
    result = node.generate_radar(63.4, 10.4, [{"lat": 64.0, "lon": 11.0, "heading": 0.0}])
    assert len(result["polar_targets"]) == 0


def test_rng_reproducibility():
    """Verify that same seed twice → identical noise."""
    node1 = SensorMockNode(root_seed=42, episode=1, worker=0)
    if not HAS_ROS2:
        node1.radar_max_range = 12000.0
        node1.range_sigma_m = 3.0
        node1.bearing_sigma_rad = 0.005
        node1.rcs_min = 10.0
        node1.rcs_max = 50.0
        node1.clutter_max = 5
        node1.ais_drop_pct = 10.0
    
    node2 = SensorMockNode(root_seed=42, episode=1, worker=0)
    if not HAS_ROS2:
        node2.radar_max_range = 12000.0
        node2.range_sigma_m = 3.0
        node2.bearing_sigma_rad = 0.005
        node2.rcs_min = 10.0
        node2.rcs_max = 50.0
        node2.clutter_max = 5
        node2.ais_drop_pct = 10.0

    targets = [{"lat": 63.401, "lon": 10.401, "heading": 0.0}] * 5

    res1 = node1.generate_radar(63.4, 10.4, targets)
    res2 = node2.generate_radar(63.4, 10.4, targets)
    assert res1 == res2

    # Verify AIS dropout is also reproducible
    target_dict = {"mmsi": 1, "sog": 5.0, "cog": 0.5, "lat": 63.41, "lon": 10.41, "heading": 0.5}
    ais1 = [node1.generate_ais(63.4, 10.4, target_dict) for _ in range(20)]
    ais2 = [node2.generate_ais(63.4, 10.4, target_dict) for _ in range(20)]
    assert ais1 == ais2


def test_different_seeds_different_noise():
    """Verify that different seed → different noise."""
    node1 = SensorMockNode(root_seed=42, episode=1, worker=0)
    if not HAS_ROS2:
        node1.radar_max_range = 12000.0
        node1.range_sigma_m = 3.0
        node1.bearing_sigma_rad = 0.005
        node1.rcs_min = 10.0
        node1.rcs_max = 50.0
        node1.clutter_max = 5
        node1.ais_drop_pct = 10.0
        
    node2 = SensorMockNode(root_seed=99, episode=1, worker=0)
    if not HAS_ROS2:
        node2.radar_max_range = 12000.0
        node2.range_sigma_m = 3.0
        node2.bearing_sigma_rad = 0.005
        node2.rcs_min = 10.0
        node2.rcs_max = 50.0
        node2.clutter_max = 5
        node2.ais_drop_pct = 10.0

    targets = [{"lat": 63.401, "lon": 10.401, "heading": 0.0}] * 5

    res1 = node1.generate_radar(63.4, 10.4, targets)
    res2 = node2.generate_radar(63.4, 10.4, targets)
    assert res1 != res2


def test_no_random_survives():
    """Verify that no random. imports/calls remain in node.py."""
    import re
    node_file = Path(__file__).resolve().parent.parent.parent / "src" / "sim_workbench" / "sil_nodes" / "sensor_mock" / "sensor_mock" / "node.py"
    content = node_file.read_text()
    assert "import random" not in content
    assert not re.search(r"\brandom\.", content)
