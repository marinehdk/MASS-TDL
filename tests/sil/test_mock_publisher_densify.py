#!/usr/bin/env python3
"""Phase 3.10.1: test mock_l2 _densify_waypoints + _resample_speeds + auto-detect.

These run on the host without ROS2 by loading only the pure-Python helpers
from ``docker/mock_l2_publisher.py`` (skipping the rclpy-dependent module
header). The functions under test depend only on ``math`` / ``os`` / ``yaml``.
"""

import os
import sys
import tempfile
import types
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DOCKER_DIR = REPO_ROOT / "docker"


def _load_pure_helpers():
    """Extract _densify_waypoints / _resample_speeds / MockL2Publisher from
    mock_l2_publisher.py without importing rclpy-dependent modules.

    Strategy: stub rclpy + ROS2 message packages in sys.modules, then exec the
    file. Subsequent module-level code that uses these stubs (e.g. class
    definitions inheriting from `Node`) only runs at class-definition time,
    not at helper-function call time, so the helpers work despite stubs.
    """
    # Make rclpy importable as a package.
    rclpy = types.ModuleType("rclpy")
    rclpy.__path__ = []  # mark as package
    sys.modules["rclpy"] = rclpy

    rclpy_node = types.ModuleType("rclpy.node")
    rclpy_node.Node = object
    sys.modules["rclpy.node"] = rclpy_node
    rclpy.node = rclpy_node

    rclpy_qos = types.ModuleType("rclpy.qos")
    rclpy_qos.QoSProfile = object
    rclpy_qos.QoSReliabilityPolicy = types.SimpleNamespace(RELIABLE=1, BEST_EFFORT=2)
    rclpy_qos.QoSDurabilityPolicy = types.SimpleNamespace(TRANSIENT_LOCAL=1, VOLATILE=2)
    rclpy_qos.QoSHistoryPolicy = types.SimpleNamespace(KEEP_LAST=1)
    sys.modules["rclpy.qos"] = rclpy_qos
    rclpy.qos = rclpy_qos

    for mod_name, class_names in [
        ("std_msgs.msg", ["String", "Header"]),
        ("builtin_interfaces.msg", ["Time"]),
        ("geographic_msgs.msg", ["GeoPoint", "GeoPose", "GeoPoseStamped", "GeoPath"]),
        ("sil_msgs.msg", ["LifecycleStatus", "OwnShipState"]),
        ("l3_external_msgs.msg", [
            "VoyageTask", "PlannedRoute", "SpeedProfile", "TimeWindow", "ReplanResponse"]),
        ("l3_msgs.msg", ["RouteReplanRequest"]),
    ]:
        mod = types.ModuleType(mod_name)
        for cls in class_names:
            setattr(mod, cls, type(cls, (), {}))
        sys.modules[mod_name] = mod

    src = (DOCKER_DIR / "mock_l2_publisher.py").read_text()
    pub_ns = {"__name__": "mock_l2_publisher", "__file__": str(DOCKER_DIR / "mock_l2_publisher.py")}
    exec(compile(src, str(DOCKER_DIR / "mock_l2_publisher.py"), "exec"), pub_ns)
    return types.SimpleNamespace(**{k: v for k, v in pub_ns.items() if not k.startswith("__")})


_pub = _load_pure_helpers()


class TestDensifyWaypoints(unittest.TestCase):
    """_densify_waypoints must split long legs at L2_DENSIFY_DEFAULT_SPACING_NM."""

    def test_two_endpoint_north_south_leg_yields_dense_polyline(self):
        """rule14-ho nominalRoute (63.44 → 63.606667, 10.38) densifies to ≥3 wps.

        Without densification the M5 prefix helper cannot index history
        waypoints before ownship (probe run-19f320d48d5: 0 ACCEPTED / 98 REJECTED).
        """
        wps = [(63.44, 10.38), (63.606667, 10.38)]
        out = _pub._densify_waypoints(wps)
        self.assertGreaterEqual(
            len(out), 3,
            f"rule14-ho 2-endpoint nominalRoute must densify to ≥3 wps, got {len(out)}")
        self.assertAlmostEqual(out[0][0], 63.44, places=6)
        self.assertAlmostEqual(out[0][1], 10.38, places=6)
        self.assertAlmostEqual(out[-1][0], 63.606667, places=6)
        self.assertAlmostEqual(out[-1][1], 10.38, places=6)

    def test_already_dense_route_passthrough(self):
        wps = [(63.44, 10.38), (63.445, 10.38), (63.45, 10.38)]
        out = _pub._densify_waypoints(wps, spacing_nm=1.0)
        self.assertEqual(len(out), len(wps))

    def test_empty_or_single_passthrough(self):
        self.assertEqual(_pub._densify_waypoints([]), [])
        self.assertEqual(_pub._densify_waypoints([(63.44, 10.38)]), [(63.44, 10.38)])


class TestResampleSpeeds(unittest.TestCase):
    """_resample_speeds must return densified_count entries.

    Regression: probe run-19f3231690a traceback
    ``AttributeError: 'MockL2Publisher' object has no attribute '_resample_speeds'``
    was caused by ``self._resample_speeds(...)`` on what is a module-level
    function. This test pins the function shape so the typo cannot return.
    """

    def test_resample_returns_densified_count(self):
        speeds = [6.0, 6.0]
        out = _pub._resample_speeds(speeds, nominal_count=2, densified_count=12)
        self.assertEqual(len(out), 12)
        for v in out:
            self.assertAlmostEqual(v, 6.0, places=6)

    def test_resample_handles_empty_speeds(self):
        out = _pub._resample_speeds([], nominal_count=0, densified_count=5)
        self.assertEqual(len(out), 5)
        self.assertGreater(out[0], 0.0)

    def test_resample_single_nominal(self):
        out = _pub._resample_speeds([6.0], nominal_count=1, densified_count=4)
        self.assertEqual(len(out), 4)


class TestAutoDetectMetadataPreference(unittest.TestCase):
    """Auto-detect must prefer YAML whose metadata.scenario_id matches the
    current_scenario_id latch (Phase 3.10.1).

    Regression: alphabetical-first selection picked
    ``colreg-rule13-ot-target-giveway`` instead of the configured scenario when
    the lifecycle_status.scenario_id latch was lost (VOLATILE subscriber QoS).
    """

    def test_metadata_match_preferred_over_alphabetical(self):
        import yaml
        with tempfile.TemporaryDirectory() as tmp:
            with open(os.path.join(tmp, "aaa.yaml"), "w") as f:
                yaml.dump({"metadata": {"scenario_id": "wrong-scenario"}}, f)
            with open(os.path.join(tmp, "zzz.yaml"), "w") as f:
                yaml.dump({"metadata": {"scenario_id": "colreg-rule14-ho-v2.0"}}, f)

            captured = {}

            class _Stub(_pub.MockL2Publisher):
                """Bypass __init__ (which needs rclpy); only _auto_detect_scenario is exercised."""
                def __init__(self, scenario_dir, current_scenario_id):
                    self._scenario_dir = scenario_dir
                    self._current_scenario_id = current_scenario_id

                def get_logger(self):
                    class _L:
                        def info(self, *a, **k): pass
                        def warn(self, *a, **k): pass
                    return _L()

                def _load_scenario(self, sid):
                    captured["sid"] = sid

                def _generate_default_route(self):
                    captured["sid"] = None

            stub = _Stub(tmp, "colreg-rule14-ho-v2.0")
            stub._auto_detect_scenario()
            self.assertEqual(
                captured.get("sid"), "zzz",
                "auto-detect must prefer metadata.scenario_id match, "
                "not alphabetical-first")


if __name__ == "__main__":
    unittest.main()

