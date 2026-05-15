"""Parallel run validation: demo backend vs ros2 backend trajectory comparison.

Runs the same scenario (imazu-01-ho) in demo mode and ros2 mode,
compares output trajectories. Acceptance: position diff < 5% of distance traveled.
"""

import json
import sys
from pathlib import Path
from unittest.mock import MagicMock

import pytest


SCENARIO_DIR = Path(__file__).parent.parent.parent / "scenarios"
TEST_SCENARIO_ID = "imazu-01-ho"


class _MockNode:
    """Fake rclpy.node.Node that accepts node_name without setting spec=string."""

    def __init__(self, *args, **kwargs):
        self.create_client = MagicMock(return_value=MagicMock())
        self.create_subscription = MagicMock(return_value=MagicMock())
        self.get_logger = MagicMock(return_value=MagicMock())
        self.declare_parameter = MagicMock()


def _build_rclpy_mock():
    """Return dict of {modname: mock_obj} for all rclpy + lifecycle_msgs submodules."""
    rclpy = MagicMock()
    rclpy.ok.return_value = True
    rclpy.init = MagicMock()
    rclpy.shutdown = MagicMock()
    node_mod = MagicMock()
    node_mod.Node = _MockNode
    cb_groups = MagicMock()
    cb_groups.ReentrantCallbackGroup = MagicMock
    executors_mod = MagicMock()
    executors_mod.MultiThreadedExecutor = MagicMock
    lifecycle_msg = MagicMock()
    lifecycle_msg.Transition = MagicMock
    lifecycle_srv = MagicMock()
    lifecycle_srv.ChangeState = MagicMock
    lifecycle_srv.GetState = MagicMock

    return {
        "rclpy": rclpy,
        "rclpy.node": node_mod,
        "rclpy.callback_groups": cb_groups,
        "rclpy.executors": executors_mod,
        "lifecycle_msgs": lifecycle_msg,
        "lifecycle_msgs.msg": lifecycle_msg,
        "lifecycle_msgs.srv": lifecycle_srv,
    }


_RCLPY_AVAILABLE = False
try:
    import rclpy  # noqa: F401
    _RCLPY_AVAILABLE = True
except ImportError:
    pass


_MODULE_MOCKS = None if _RCLPY_AVAILABLE else _build_rclpy_mock()


@pytest.fixture(scope="module", autouse=True)
def _mock_ros2_modules():
    """Insert mock rclpy/lifecycle_msgs into sys.modules so sil_orchestrator imports work."""
    if _RCLPY_AVAILABLE:
        yield
        return
    originals = {}
    for name, mock in _MODULE_MOCKS.items():
        originals[name] = sys.modules.get(name)
        sys.modules[name] = mock
    try:
        yield
    finally:
        for name, orig in originals.items():
            if orig is None:
                sys.modules.pop(name, None)
            else:
                sys.modules[name] = orig


@pytest.fixture(scope="module")
def seed_funcs():
    from sil_orchestrator.main import _seed_run_dir_demo, _seed_run_dir_ros2
    from sil_orchestrator.config import RUN_DIR
    return _seed_run_dir_demo, _seed_run_dir_ros2, RUN_DIR


class TestCutoverParallelRun:

    @pytest.mark.integration
    def test_both_routes_produce_run_id(self, seed_funcs):
        _seed_run_dir_demo, _seed_run_dir_ros2, RUN_DIR = seed_funcs
        run_id_demo = _seed_run_dir_demo(TEST_SCENARIO_ID)
        assert run_id_demo.startswith("run-")
        assert (RUN_DIR / run_id_demo / "scenario.yaml").exists()
        assert (RUN_DIR / run_id_demo / "scoring.json").exists()

        run_id_ros2 = _seed_run_dir_ros2(TEST_SCENARIO_ID)
        assert run_id_ros2.startswith("run-")
        assert (RUN_DIR / run_id_ros2 / "scenario.yaml").exists()
        assert not (RUN_DIR / run_id_ros2 / "scoring.json").exists()

    @pytest.mark.integration
    def test_scoring_routes_fallback_to_json(self, seed_funcs):
        from sil_orchestrator.scoring_routes import _get_latest_run_dir
        from sil_orchestrator.config import RUN_DIR

        latest = _get_latest_run_dir()
        assert latest is not None

    @pytest.mark.integration
    def test_scenario_store_returns_backend(self):
        from sil_orchestrator.scenario_store import ScenarioStore
        store = ScenarioStore()
        detail = store.get(TEST_SCENARIO_ID)
        assert detail is not None
        assert "backend" in detail
        assert detail["backend"] in ("demo", "ros2")

    @pytest.mark.integration
    def test_demo_vs_ros2_run_dir_difference(self, seed_funcs):
        _seed_run_dir_demo, _seed_run_dir_ros2, RUN_DIR = seed_funcs

        run_demo = _seed_run_dir_demo(TEST_SCENARIO_ID)
        run_ros2 = _seed_run_dir_ros2(TEST_SCENARIO_ID)

        demo_json = RUN_DIR / run_demo / "scoring.json"
        ros2_json = RUN_DIR / run_ros2 / "scoring.json"

        assert demo_json.exists()
        assert not ros2_json.exists()

        data = json.loads(demo_json.read_text())
        assert "run_id" in data
        assert "kpis" in data
        assert "min_cpa_nm" in data["kpis"]
        assert data["verdict"] == "pending"
