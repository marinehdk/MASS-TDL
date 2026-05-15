"""LifecycleNode 回调测试 — 需要 rclpy, macOS 上跳过。"""

import pytest

rclpy = None
LifecycleNode = None

try:
    import rclpy
    from rclpy.lifecycle import LifecycleNode, TransitionCallbackReturn
except ImportError:
    pass


def _has_ros2():
    return rclpy is not None and LifecycleNode is not None


@pytest.fixture
def node():
    if not _has_ros2():
        pytest.skip("rclpy 不可用 (非 ROS2 环境)")
    rclpy.init(args=None)
    from ship_dynamics.node import ShipDynamicsNode
    n = ShipDynamicsNode(node_name="test_ship_dynamics")
    yield n
    try:
        n.destroy_node()
    except Exception:
        pass
    rclpy.shutdown()


def test_inherits_lifecycle_node(node):
    assert isinstance(node, LifecycleNode)


def test_on_configure_returns_success(node):
    from rclpy.lifecycle import State
    result = node.on_configure(State.PRIMARY_STATE_UNCONFIGURED)
    assert result == TransitionCallbackReturn.SUCCESS


def test_on_activate_returns_success(node):
    from rclpy.lifecycle import State
    node.on_configure(State.PRIMARY_STATE_UNCONFIGURED)
    result = node.on_activate(State.PRIMARY_STATE_INACTIVE)
    assert result == TransitionCallbackReturn.SUCCESS


def test_on_deactivate_returns_success(node):
    from rclpy.lifecycle import State
    node.on_configure(State.PRIMARY_STATE_UNCONFIGURED)
    node.on_activate(State.PRIMARY_STATE_INACTIVE)
    result = node.on_deactivate(State.PRIMARY_STATE_ACTIVE)
    assert result == TransitionCallbackReturn.SUCCESS


def test_on_cleanup_returns_success(node):
    from rclpy.lifecycle import State
    node.on_configure(State.PRIMARY_STATE_UNCONFIGURED)
    node.on_activate(State.PRIMARY_STATE_INACTIVE)
    node.on_deactivate(State.PRIMARY_STATE_ACTIVE)
    result = node.on_cleanup(State.PRIMARY_STATE_INACTIVE)
    assert result == TransitionCallbackReturn.SUCCESS
