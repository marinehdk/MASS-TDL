"""Tests for ScenarioAuthoringNode lifecycle callbacks and YAML CRUD.
Skip all rclpy-dependent tests if ROS2 is not available.
"""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

import pytest

# ensure the package is importable when running tests directly (without colcon)
_pkg = (
    Path(__file__).resolve().parents[4]
    / "src"
    / "sim_workbench"
    / "sil_nodes"
    / "scenario_authoring"
)
sys.path.insert(0, str(_pkg))

try:
    import rclpy
    from rclpy.lifecycle import LifecycleNode, TransitionCallbackReturn

    from scenario_authoring.node import ScenarioAuthoringNode

    HAS_ROS2 = True
except ImportError:
    HAS_ROS2 = False

_ROS2_SKIP_REASON = "rclpy not available (ROS2 not installed on this platform)"


# ---------------------------------------------------------------------------
# Lifecycle callback tests  (require rclpy)
# ---------------------------------------------------------------------------


@pytest.mark.skipif(not HAS_ROS2, reason=_ROS2_SKIP_REASON)
class TestScenarioAuthoringLifecycle:
    """ROS2 LifecycleNode callback contract tests."""

    @pytest.fixture(autouse=True)
    def ros_context(self) -> None:
        rclpy.init()
        self.node = ScenarioAuthoringNode()
        yield
        self.node.destroy_node()
        rclpy.shutdown()

    def test_inherits_lifecycle_node(self) -> None:
        assert isinstance(self.node, LifecycleNode)

    def test_on_configure_returns_success_and_sets_dir(self) -> None:
        ret = self.node.on_configure(None)
        assert ret == TransitionCallbackReturn.SUCCESS
        assert self.node._dir is not None
        assert self.node._dir.exists()

    def test_on_configure_declares_scenario_dir_parameter(self) -> None:
        self.node.on_configure(None)
        assert self.node.has_parameter("scenario_dir")
        val = (
            self.node.get_parameter("scenario_dir")
            .get_parameter_value()
            .string_value
        )
        assert val == "/var/sil/scenarios"

    def test_on_activate_creates_lifecycle_publisher(self) -> None:
        self.node.on_configure(None)
        ret = self.node.on_activate(None)
        assert ret == TransitionCallbackReturn.SUCCESS
        assert self.node._loaded_pub is not None
        assert self.node._loaded_pub.topic_name == "/sil/scenario_loaded"

    def test_on_deactivate_destroys_lifecycle_publisher(self) -> None:
        self.node.on_configure(None)
        self.node.on_activate(None)
        ret = self.node.on_deactivate(None)
        assert ret == TransitionCallbackReturn.SUCCESS
        assert self.node._loaded_pub is None


# ---------------------------------------------------------------------------
# YAML CRUD operation tests  (rclpy required for node construction)
# ---------------------------------------------------------------------------


@pytest.mark.skipif(not HAS_ROS2, reason=_ROS2_SKIP_REASON)
class TestScenarioAuthoringCRUD:
    """CRUD operations via the backward-compatible constructor arg."""

    def test_list_empty_returns_empty_list(self) -> None:
        rclpy.init()
        with tempfile.TemporaryDirectory() as d:
            node = ScenarioAuthoringNode(d)
            assert node.list_scenarios() == []
            node.destroy_node()
        rclpy.shutdown()

    def test_create_persists_file_and_returns_id_and_hash(self) -> None:
        rclpy.init()
        with tempfile.TemporaryDirectory() as d:
            node = ScenarioAuthoringNode(d)
            result = node.create_scenario("name: test\ntype: head-on\n")
            sid = result["scenario_id"]
            assert len(sid) == 12
            assert len(result["hash"]) == 64  # SHA-256 hex
            assert (Path(d) / f"{sid}.yaml").exists()
            node.destroy_node()
        rclpy.shutdown()

    def test_get_scenario_by_id_returns_content_and_hash(self) -> None:
        rclpy.init()
        with tempfile.TemporaryDirectory() as d:
            node = ScenarioAuthoringNode(d)
            content = "name: demo\n"
            sid = node.create_scenario(content)["scenario_id"]
            detail = node.get_scenario(sid)
            assert detail is not None
            assert detail["yaml_content"] == content
            assert len(detail["hash"]) == 64
            node.destroy_node()
        rclpy.shutdown()

    def test_get_missing_scenario_returns_none(self) -> None:
        rclpy.init()
        with tempfile.TemporaryDirectory() as d:
            node = ScenarioAuthoringNode(d)
            assert node.get_scenario("nonexistent") is None
            node.destroy_node()
        rclpy.shutdown()

    def test_delete_missing_scenario_returns_false(self) -> None:
        rclpy.init()
        with tempfile.TemporaryDirectory() as d:
            node = ScenarioAuthoringNode(d)
            assert node.delete_scenario("nonexistent") is False
            node.destroy_node()
        rclpy.shutdown()

    def test_create_get_delete_full_cycle(self) -> None:
        rclpy.init()
        with tempfile.TemporaryDirectory() as d:
            node = ScenarioAuthoringNode(d)
            sid = node.create_scenario("name: cycle\n")["scenario_id"]
            assert node.get_scenario(sid) is not None
            assert node.delete_scenario(sid) is True
            assert node.get_scenario(sid) is None
            assert node.list_scenarios() == []
            node.destroy_node()
        rclpy.shutdown()

    def test_hash_consistency_same_content_same_hash(self) -> None:
        rclpy.init()
        with tempfile.TemporaryDirectory() as d:
            node = ScenarioAuthoringNode(d)
            content = "name: consistency\nencounter: overtake\n"
            r1 = node.create_scenario(content)
            r2 = node.create_scenario(content)
            assert r1["hash"] == r2["hash"]
            assert r1["scenario_id"] != r2["scenario_id"]
            node.destroy_node()
        rclpy.shutdown()

    def test_persists_across_node_recreation_same_dir(self) -> None:
        rclpy.init()
        with tempfile.TemporaryDirectory() as d:
            node = ScenarioAuthoringNode(d)
            sid = node.create_scenario("name: persist\n")["scenario_id"]
            node.destroy_node()

            node2 = ScenarioAuthoringNode(d)
            detail = node2.get_scenario(sid)
            assert detail is not None
            assert detail["yaml_content"] == "name: persist\n"
            node2.destroy_node()
        rclpy.shutdown()
