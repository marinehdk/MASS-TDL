from types import SimpleNamespace
import sys
from pathlib import Path
from unittest.mock import MagicMock


class DummyLifecycleNode:
    def __init__(self, node_name, **kwargs):
        self._logger = MagicMock()


sys.modules["rclpy"] = MagicMock()
sys.modules["rclpy.lifecycle"] = MagicMock()
sys.modules["rclpy.lifecycle"].LifecycleNode = DummyLifecycleNode
sys.modules["rclpy.qos"] = MagicMock()
sys.modules["sil_msgs"] = MagicMock()
sys.modules["sil_msgs.msg"] = MagicMock()

sys.path.insert(0, str(Path(__file__).parents[2]))
sys.path.insert(0, str(Path(__file__).parents[3] / "sil_common"))


def test_radar_callback_not_wall_clock_throttled():
    from sensor_mock.node import SensorMockNode

    node = SensorMockNode()
    node._own_state = SimpleNamespace(lat=63.44, lon=10.38)
    node._target_states = {}
    node._radar_pub = MagicMock()
    node._last_radar_pub_wall_time = 999999999.0
    clock = MagicMock()
    clock.now.return_value.to_msg.return_value = object()
    node.get_clock = MagicMock(return_value=clock)

    node._radar_callback()

    node._radar_pub.publish.assert_called_once()
