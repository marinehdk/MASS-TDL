"""conftest for tools/sil/tests — add src paths and mock ROS2 before any test imports."""

import sys
import types
from pathlib import Path

# Add source roots so imported modules resolve.
_HERE = Path(__file__).parent
sys.path.insert(0, str(_HERE.parent))  # tools/sil/
sys.path.insert(0, str(_HERE.parent.parent.parent / "src" / "sil_orchestrator"))
sys.path.insert(0, str(_HERE.parent.parent.parent / "src"))

# ---------------------------------------------------------------------------
# Mock ROS2 / rclpy so lifecycle_bridge can be imported on any platform.
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Stub classes for ROS2 types that lifecycle_bridge imports.
# ---------------------------------------------------------------------------


class FakeParameter:
    def __init__(self, name, value=None):
        self.name = name
        self.value = value


class FakeNode:
    pass


class FakeService:
    class Request:
        pass

    class Response:
        def __init__(self, success=True):
            self.success = success


class FakeTransition:
    TRANSITION_CONFIGURE = 1
    TRANSITION_ACTIVATE = 2
    TRANSITION_DEACTIVATE = 3
    TRANSITION_CLEANUP = 4


FakeChangeState = type("ChangeState", (), {"Request": type("Request", (), {})})
FakeGetState = type("GetState", (), {"Request": type("Request", (), {})})

# ---------------------------------------------------------------------------
# Mock-module assembly — construct dummy module objects and inject attributes.
# ---------------------------------------------------------------------------

_rclpy_mock = types.ModuleType("rclpy")
_rclpy_mock.parameter = types.ModuleType("rclpy.parameter")
_rclpy_mock.parameter.Parameter = FakeParameter
_rclpy_mock.node = types.ModuleType("rclpy.node")
_rclpy_mock.node.Node = FakeNode

_lifecycle_msgs = types.ModuleType("lifecycle_msgs")
_lifecycle_msgs.srv = types.ModuleType("lifecycle_msgs.srv")
_lifecycle_msgs.srv.ChangeState = FakeChangeState
_lifecycle_msgs.srv.GetState = FakeGetState
_lifecycle_msgs.msg = types.ModuleType("lifecycle_msgs.msg")
_lifecycle_msgs.msg.Transition = FakeTransition

_rcl_interfaces = types.ModuleType("rcl_interfaces")
_rcl_interfaces.srv = types.ModuleType("rcl_interfaces.srv")

sys.modules["rclpy"] = _rclpy_mock
sys.modules["rclpy.parameter"] = _rclpy_mock.parameter
sys.modules["rclpy.node"] = _rclpy_mock.node
sys.modules["lifecycle_msgs"] = _lifecycle_msgs
sys.modules["lifecycle_msgs.srv"] = _lifecycle_msgs.srv
sys.modules["lifecycle_msgs.msg"] = _lifecycle_msgs.msg
sys.modules["rcl_interfaces"] = _rcl_interfaces
sys.modules["rcl_interfaces.srv"] = _rcl_interfaces.srv
