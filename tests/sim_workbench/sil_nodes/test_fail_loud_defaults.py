"""Fail-loud 参数默认值测试 — 验证 sentinel 校验逻辑在无参/部分参/完整参场景下行为正确。

所有测试需要 ROS2 环境（rclpy）。"""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

# Ensure packages are importable
_sil_nodes = Path(__file__).resolve().parents[3] / "src" / "sim_workbench" / "sil_nodes"
sys.path.insert(0, str(_sil_nodes / "ship_dynamics"))
sys.path.insert(0, str(_sil_nodes / "target_vessel"))

try:
    import rclpy
    from rclpy.lifecycle import State
    from rclpy.parameter import Parameter

    HAS_ROS2 = True
except ImportError:
    HAS_ROS2 = False

_SKIP_REASON = "rclpy not available (non-ROS2 environment)"


class TestShipDynamicsFailLoud:
    """ship_dynamics_node on_configure 行为：无参→FAILURE, 部分参→FAILURE, 完整参→SUCCESS."""

    @pytest.fixture(autouse=True)
    def _ros(self):
        if not HAS_ROS2:
            pytest.skip(_SKIP_REASON)
        rclpy.init()
        yield
        rclpy.shutdown()

    @pytest.fixture
    def make_node(self):
        """每次测试创建独立节点。"""
        from ship_dynamics.node import ShipDynamicsNode

        nodes = []

        def _make():
            n = ShipDynamicsNode(node_name="test_fail_loud_sd")
            nodes.append(n)
            return n

        yield _make
        for n in nodes:
            try:
                n.destroy_node()
            except Exception:
                pass

    def test_no_params_returns_failure(self, make_node):
        """case 1: 不注入任何参数 → on_configure 必须 FAILURE."""
        node = make_node()
        result = node.on_configure(State.PRIMARY_STATE_UNCONFIGURED)
        # sentinel 默认值应触发校验失败
        assert result != rclpy.lifecycle.TransitionCallbackReturn.SUCCESS, (
            "Expected FAILURE with no params injected, got SUCCESS"
        )

    def test_partial_params_returns_failure(self, make_node):
        """case 2: 只注入 origin_lat 不注入 origin_lon → 必须 FAILURE."""
        node = make_node()
        # 预声明部分参数
        node.declare_parameter("origin_lat", 63.44)
        # origin_lon 保持 sentinel (-999.0) → 应触发 FAILURE
        result = node.on_configure(State.PRIMARY_STATE_UNCONFIGURED)
        assert result != rclpy.lifecycle.TransitionCallbackReturn.SUCCESS, (
            "Expected FAILURE with partial params, got SUCCESS"
        )

    def test_full_params_returns_success(self, make_node):
        """case 3: 完整注入 imazu-08 参数 → SUCCESS."""
        node = make_node()
        # 预声明所有关键初始参数 (imazu-08-ms values)
        node.declare_parameter("origin_lat", 63.44)
        node.declare_parameter("origin_lon", 10.38)
        node.declare_parameter("u0", 5.14444)   # 10 kn → m/s
        node.declare_parameter("psi0", 0.0)      # heading 0°
        node.declare_parameter("dt", 0.02)
        result = node.on_configure(State.PRIMARY_STATE_UNCONFIGURED)
        assert result == rclpy.lifecycle.TransitionCallbackReturn.SUCCESS, (
            f"Expected SUCCESS with full imazu-08 params, got {result}"
        )


class TestTargetVesselFailLoud:
    """target_vessel_node on_configure 行为：非法目标字段 → FAILURE, 有效/空 → SUCCESS."""

    @pytest.fixture(autouse=True)
    def _ros(self):
        if not HAS_ROS2:
            pytest.skip(_SKIP_REASON)
        rclpy.init()
        yield
        rclpy.shutdown()

    @pytest.fixture
    def make_node(self):
        from target_vessel.node import TargetVesselNode

        nodes = []

        def _make():
            n = TargetVesselNode()
            nodes.append(n)
            return n

        yield _make
        for n in nodes:
            try:
                n.destroy_node()
            except Exception:
                pass

    def test_empty_targets_succeeds(self, make_node):
        """空 targets 数组是合法状态 → SUCCESS."""
        node = make_node()
        result = node.on_configure(None)
        assert result == rclpy.lifecycle.TransitionCallbackReturn.SUCCESS

    def test_invalid_mmsi_fails(self, make_node):
        """mmsi=0 的目标触发 FAILURE."""
        node = make_node()
        import json
        node.declare_parameter(
            "default_targets_json",
            json.dumps([{"mmsi": 0, "lat": 63.5, "lon": 10.4, "heading_deg": 90, "sog_kn": 10}])
        )
        result = node.on_configure(None)
        assert result != rclpy.lifecycle.TransitionCallbackReturn.SUCCESS

    def test_valid_target_succeeds(self, make_node):
        """合法目标字段 → SUCCESS."""
        node = make_node()
        import json
        node.declare_parameter(
            "default_targets_json",
            json.dumps([{"mmsi": 100000001, "lat": 63.503492, "lon": 10.241335,
                         "heading_deg": 90, "sog_kn": 10}])
        )
        result = node.on_configure(None)
        assert result == rclpy.lifecycle.TransitionCallbackReturn.SUCCESS