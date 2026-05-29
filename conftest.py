import sys
import types
from pathlib import Path
from unittest.mock import Mock

# Ensure src/ and packages are on the Python path so tests can import modules directly
root_dir = Path(__file__).parent
sys.path.insert(0, str(root_dir / "src"))

# Add simulation workbench package parent directories to PYTHONPATH
sim_wb_dir = root_dir / "src" / "sim_workbench"
sys.path.insert(0, str(sim_wb_dir / "sil_lifecycle"))
for pkg in (
    "ship_dynamics",
    "env_disturbance",
    "target_vessel",
    "sensor_mock",
    "tracker_mock",
    "fault_injection",
    "scoring",
    "scenario_authoring",
):
    sys.path.insert(0, str(sim_wb_dir / "sil_nodes" / pkg))

# Check if rclpy is importable (native ROS2 environment)
try:
    import rclpy
    _HAS_ROS2 = True
except ImportError:
    _HAS_ROS2 = False

if not _HAS_ROS2:
    # Set up mock rclpy modules
    mock_rclpy = types.ModuleType("rclpy")
    mock_rclpy.logging = types.ModuleType("rclpy.logging")
    
    mock_rclpy_lifecycle = types.ModuleType("rclpy.lifecycle")
    class LifecycleNode:
        def __init__(self, *args, **kwargs):
            pass
        def declare_parameter(self, *args, **kwargs):
            class Param:
                value = kwargs.get("default_value") if len(args) < 2 else args[1]
            return Param()
        def get_parameter(self, name):
            class Param:
                value = None
            return Param()
        def get_logger(self):
            return Mock()
    mock_rclpy_lifecycle.LifecycleNode = LifecycleNode
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
    mock_sil_msg = types.ModuleType("sil_msgs.msg")
    class LifecycleStatus:
        pass
    mock_sil_msg.LifecycleStatus = LifecycleStatus
    mock_sil.msg = mock_sil_msg
    
    sys.modules["sil_msgs"] = mock_sil
    sys.modules["sil_msgs.msg"] = mock_sil_msg

    # Mock builtin_interfaces
    mock_builtin = types.ModuleType("builtin_interfaces")
    mock_builtin_msg = types.ModuleType("builtin_interfaces.msg")
    class Time:
        pass
    mock_builtin_msg.Time = Time
    mock_builtin.msg = mock_builtin_msg
    
    sys.modules["builtin_interfaces"] = mock_builtin
    sys.modules["builtin_interfaces.msg"] = mock_builtin_msg

    # Mock rosgraph_msgs
    mock_rosgraph = types.ModuleType("rosgraph_msgs")
    mock_rosgraph_msg = types.ModuleType("rosgraph_msgs.msg")
    class Clock:
        pass
    mock_rosgraph_msg.Clock = Clock
    mock_rosgraph.msg = mock_rosgraph_msg
    
    sys.modules["rosgraph_msgs"] = mock_rosgraph
    sys.modules["rosgraph_msgs.msg"] = mock_rosgraph_msg


