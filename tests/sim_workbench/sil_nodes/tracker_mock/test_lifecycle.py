"""Tests for tracker_mock LifecycleNode upgrade.

Pure-logic tests (KF, TrackerMockCore) use lazy imports to avoid
shadowing from the test directory layout. LifecycleNode tests are
skipped when rclpy is not available (macOS without ROS 2).
"""

import sys
from pathlib import Path

import pytest

# Outer ROS package dir (contains the Python sub-package tracker_mock/tracker_mock/)
_SRC = Path(__file__).resolve().parents[4] / "src" / "sim_workbench" / "sil_nodes" / "tracker_mock"
if str(_SRC) not in sys.path:
    sys.path.insert(0, str(_SRC))


# ===================================================================
# Pure-logic tests — no ROS required
# ===================================================================


class TestKalmanFilter2D:
    """4-state KF (x, y, vx, vy) correctness."""

    def _make_kf(self, dt=0.1):
        from tracker_mock.node import KalmanFilter2D

        return KalmanFilter2D(dt=dt)

    def test_init_sets_state_and_covariance(self):
        kf = self._make_kf()
        kf.init(1.0, 2.0, 0.5, 0.3)
        assert kf.x == [1.0, 2.0, 0.5, 0.3]
        assert kf.P == [[1, 0, 0, 0],
                        [0, 1, 0, 0],
                        [0, 0, 1, 0],
                        [0, 0, 0, 1]]

    def test_predict_propagates_velocity(self):
        kf = self._make_kf(dt=0.1)
        kf.init(0.0, 0.0, 1.0, 0.0)
        kf.predict()
        assert kf.x[0] == pytest.approx(0.1)
        assert kf.x[1] == pytest.approx(0.0)
        assert kf.x[2] == pytest.approx(1.0)
        assert kf.x[3] == pytest.approx(0.0)

    def test_update_sets_position_directly(self):
        kf = self._make_kf()
        kf.init(0.0, 0.0, 0.0, 0.0)
        kf.update(5.0, 7.0)
        assert kf.x[0] == pytest.approx(5.0)
        assert kf.x[1] == pytest.approx(7.0)

    def test_get_state_returns_dict(self):
        kf = self._make_kf()
        kf.init(10.0, 20.0, 0.5, -0.3)
        s = kf.get_state()
        assert s == {"x": 10.0, "y": 20.0, "vx": 0.5, "vy": -0.3}

    def test_update_before_init_auto_inits(self):
        kf = self._make_kf()
        kf.update(3.0, 4.0)
        assert kf.x[0] == pytest.approx(3.0)

    def test_predict_before_init_is_noop(self):
        kf = self._make_kf()
        kf.predict()
        assert kf.x is None

    def test_get_state_before_init_returns_zeros(self):
        kf = self._make_kf()
        assert kf.get_state() == {"x": 0, "y": 0, "vx": 0, "vy": 0}


class TestTrackerMockCore:
    """God and KF tracking modes."""

    def _make_core(self, tracker_type="god"):
        from tracker_mock.node import TrackerMockCore

        return TrackerMockCore(tracker_type=tracker_type)

    def test_god_tracker_passthrough(self):
        core = self._make_core("god")
        targets = [{"mmsi": 123, "lat": 25.0, "lon": 121.5, "sog": 12.0, "cog": 45.0}]
        results = core.track(targets)
        assert len(results) == 1
        for key in ("mmsi", "lat", "lon", "sog", "cog"):
            assert results[0][key] == targets[0][key]

    def test_god_tracker_multiple_targets(self):
        core = self._make_core("god")
        targets = [
            {"mmsi": 1, "lat": 10.0, "lon": 20.0, "sog": 5.0, "cog": 0.0},
            {"mmsi": 2, "lat": 15.0, "lon": 25.0, "sog": 8.0, "cog": 90.0},
        ]
        results = core.track(targets)
        assert len(results) == 2
        assert results[0]["lat"] == 10.0

    def test_kf_tracker_accumulates_state(self):
        core = self._make_core("kf")
        r1 = core.track([{"mmsi": 456, "lat": 0.0, "lon": 0.0, "sog": 0.0, "cog": 0.0}])
        assert r1[0]["lat"] == 0.0
        r2 = core.track([{"mmsi": 456, "lat": 0.1, "lon": 0.1, "sog": 1.0, "cog": 45.0}])
        assert r2[0]["lon"] == pytest.approx(0.1, abs=0.02)

    def test_kf_tracker_two_mmsi_isolation(self):
        core = self._make_core("kf")
        core.track([{"mmsi": 100, "lat": 1.0, "lon": 2.0, "sog": 0.0, "cog": 0.0}])
        core.track([{"mmsi": 200, "lat": 3.0, "lon": 4.0, "sog": 0.0, "cog": 0.0}])
        assert len(core._kfs) == 2

    def test_tracker_core_default_type(self):
        core = self._make_core()
        assert core.tracker_type == "god"


# ===================================================================
# rclpy-based tests — skipped when rclpy not importable
# ===================================================================

_HAS_RCLPY: bool = False
try:
    import rclpy  # noqa: F401

    _HAS_RCLPY = True
except ImportError:
    pass


@pytest.mark.skipif(not _HAS_RCLPY, reason="rclpy not available (no ROS 2)")
class TestTrackerMockLifecycleNode:
    """LifecycleNode instantiation and transition tests."""

    def test_node_is_lifecycle_node(self):
        from tracker_mock.node import TrackerMockNode

        rclpy.init()
        try:
            node = TrackerMockNode()
            assert isinstance(node, rclpy.lifecycle.LifecycleNode)
        finally:
            node.destroy_node()
            rclpy.shutdown()

    def test_on_configure_declares_parameter(self):
        from tracker_mock.node import TrackerMockNode

        rclpy.init()
        try:
            node = TrackerMockNode()
            node.trigger_configure()
            assert node.has_parameter("tracker_type")
            val = node.get_parameter("tracker_type").get_parameter_value().string_value
            assert val == "god"
        finally:
            node.destroy_node()
            rclpy.shutdown()

    def test_on_configure_returns_success(self):
        from tracker_mock.node import TrackerMockNode

        rclpy.init()
        try:
            node = TrackerMockNode()
            assert node.trigger_configure()
        finally:
            node.destroy_node()
            rclpy.shutdown()

    def test_full_lifecycle_sequence(self):
        """configure → activate → deactivate → cleanup."""
        from tracker_mock.node import TrackerMockNode

        rclpy.init()
        try:
            node = TrackerMockNode()
            assert node.trigger_configure()
            assert node.trigger_activate()
            assert node._timer is not None
            assert node._pub is not None
            assert node._sub_radar is not None
            assert node._sub_ais is not None
            assert node._core is not None
            assert node.trigger_deactivate()
            assert node._timer is None
            assert node._pub is None
            assert node.trigger_cleanup()
            assert node._core is None
        finally:
            node.destroy_node()
            rclpy.shutdown()

    def test_fallback_publisher(self):
        """Without l3_external_msgs, publisher uses std_msgs/String."""
        import tracker_mock.node as tn

        original_flag = tn._USE_REAL_MSGS
        tn._USE_REAL_MSGS = False

        rclpy.init()
        try:
            node = tn.TrackerMockNode()
            node.trigger_configure()
            node.trigger_activate()
            assert node._pub.topic_name == "/sil/tracked_targets"
            assert type(node._pub.msg_type).__name__ == "String"
        finally:
            tn._USE_REAL_MSGS = original_flag
            node.destroy_node()
            rclpy.shutdown()
