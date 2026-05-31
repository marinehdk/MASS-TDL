import sys
import math
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
sys.modules["sil_msgs"] = MagicMock()
sys.modules["sil_msgs.msg"] = MagicMock()

sys.path.insert(0, str(Path(__file__).parents[2]))
sys.path.insert(0, str(Path(__file__).parents[3] / "sil_common"))
import pytest


def test_env_disturbance_rng_reproducibility():
    from env_disturbance.node import EnvDisturbanceNode

    # Initialize two nodes with same seeds
    node1a = EnvDisturbanceNode(root_seed=123, episode=1, worker=0)
    node1b = EnvDisturbanceNode(root_seed=123, episode=1, worker=0)
    # Initialize one node with different seed
    node2 = EnvDisturbanceNode(root_seed=456, episode=1, worker=0)

    # Mock get_parameter
    params = {
        "tau_wind": 300.0,
        "sigma": 2.0,
        "wind_speed_mps": 10.0,
        "wind_dir_deg": 180.0,
        "visibility_nm": 2.0,
        "sea_state_beaufort": 4,
    }
    def mock_get_parameter(name):
        class MockParam:
            value = params[name]
        return MockParam()

    node1a.get_parameter = mock_get_parameter
    node1b.get_parameter = mock_get_parameter
    node2.get_parameter = mock_get_parameter

    # Configure initial wind speeds
    node1a._wind_speed = 10.0
    node1a._prev_wind_speed = 10.0
    node1a._wind_dir = 180.0

    node1b._wind_speed = 10.0
    node1b._prev_wind_speed = 10.0
    node1b._wind_dir = 180.0

    node2._wind_speed = 10.0
    node2._prev_wind_speed = 10.0
    node2._wind_dir = 180.0

    speeds_1a, dirs_1a = [], []
    speeds_1b, dirs_1b = [], []
    speeds_2, dirs_2 = [], []

    for _ in range(50):
        node1a._step_callback()
        node1b._step_callback()
        node2._step_callback()

        speeds_1a.append(node1a._wind_speed)
        dirs_1a.append(node1a._wind_dir)

        speeds_1b.append(node1b._wind_speed)
        dirs_1b.append(node1b._wind_dir)

        speeds_2.append(node2._wind_speed)
        dirs_2.append(node2._wind_dir)

    # Identical seeds -> identical trajectory
    assert speeds_1a == speeds_1b
    assert dirs_1a == dirs_1b

    # Different seeds -> different trajectory
    assert speeds_1a != speeds_2
    assert dirs_1a != dirs_2


def test_no_random_module_survives():
    import ast
    from pathlib import Path
    node_file = Path(__file__).parents[1] / "env_disturbance" / "node.py"
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
