"""Tests for gnc_route_mock_publisher per-scenario speed injection (W1).

Verifies _load() preserves target_sog_kn from nominalRoute and _on_timer()
populates RoutePlan.speed_limit_mps in m/s, so GNC respects the per-scenario
own-ship design speed instead of falling back to global max_transit_speed.
"""
import importlib.util
import sys
import types
from pathlib import Path
from types import SimpleNamespace


class _Publisher:
    def __init__(self):
        self.messages = []

    def publish(self, msg):
        self.messages.append(msg)


class _Header:
    def __init__(self, stamp=None, frame_id=""):
        self.stamp = stamp
        self.frame_id = frame_id


class _BuiltinTime:
    @staticmethod
    def sec():  # pragma: no cover - not used in these tests
        return 0


def _install_fake_ros_modules(monkeypatch):
    rclpy = types.ModuleType("rclpy")
    rclpy.init = lambda *a, **k: None
    rclpy.spin = lambda *a, **k: None
    rclpy.shutdown = lambda *a, **k: None
    rclpy.node = types.ModuleType("rclpy.node")
    rclpy.node.Node = object
    rclpy.qos = types.ModuleType("rclpy.qos")
    rclpy.qos.QoSProfile = lambda **kwargs: kwargs
    rclpy.qos.QoSReliabilityPolicy = SimpleNamespace(RELIABLE=1)
    rclpy.qos.QoSDurabilityPolicy = SimpleNamespace(TRANSIENT_LOCAL=1)
    rclpy.qos.QoSHistoryPolicy = SimpleNamespace(KEEP_LAST=1)

    std_msgs = types.ModuleType("std_msgs")
    std_msgs.msg = types.ModuleType("std_msgs.msg")
    std_msgs.msg.Header = _Header
    std_msgs.msg.String = type("String", (), {})

    builtin_interfaces = types.ModuleType("builtin_interfaces")
    builtin_interfaces.msg = types.ModuleType("builtin_interfaces.msg")
    builtin_interfaces.msg.Time = SimpleNamespace  # _now builds it directly

    ship_interfaces = types.ModuleType("ship_interfaces")
    ship_interfaces.msg = types.ModuleType("ship_interfaces.msg")
    ship_interfaces.msg.RoutePlan = type("RoutePlan", (), {})

    sil_msgs = types.ModuleType("sil_msgs")
    sil_msgs.msg = types.ModuleType("sil_msgs.msg")
    sil_msgs.msg.LifecycleStatus = type("LifecycleStatus", (), {})

    for module in (
        rclpy, rclpy.node, rclpy.qos,
        std_msgs, std_msgs.msg,
        builtin_interfaces, builtin_interfaces.msg,
        ship_interfaces, ship_interfaces.msg,
        sil_msgs, sil_msgs.msg,
    ):
        monkeypatch.setitem(sys.modules, module.__name__, module)


def _load_publisher_module(monkeypatch):
    _install_fake_ros_modules(monkeypatch)
    path = Path(__file__).resolve().parents[2] / "docker" / "gnc_route_mock_publisher.py"
    spec = importlib.util.spec_from_file_location("gnc_route_mock_publisher_under_test", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def _make_node(module, scenario_dir):
    """Build a GncRouteMockPublisher instance without running __init__ (ROS2)."""
    node = object.__new__(module.GncRouteMockPublisher)
    node._scenario_dir = str(scenario_dir)
    node._is_active = False
    node._current_scenario_id = ""
    node._waypoints = []
    node._yaml_speeds_kn = []
    node._pub = _Publisher()
    node.get_logger = lambda: SimpleNamespace(
        info=lambda *a, **k: None, warn=lambda *a, **k: None, error=lambda *a, **k: None)
    # _now() reads node.get_clock().now().seconds_nanoseconds()
    clock = SimpleNamespace(now=lambda: SimpleNamespace(
        seconds_nanoseconds=lambda: (1, 0)))
    node.get_clock = lambda: clock
    return node


def _write_scenario(tmp_path, speeds_kn):
    scenario_dir = tmp_path / "scenarios"
    scenario_dir.mkdir()
    lines = ["ownShip:", "  nominalRoute:"]
    for lat, lon, sog in zip([63.44, 63.606667], [10.38, 10.38], speeds_kn):
        lines += [f"  - latitude: {lat}",
                  f"    longitude: {lon}",
                  f"    target_sog_kn: {sog}"]
    (scenario_dir / "colreg-test.yaml").write_text("\n".join(lines) + "\n")
    return scenario_dir


def test_load_preserves_target_sog_kn(tmp_path, monkeypatch):
    """_load() must keep per-waypoint target_sog_kn, not just lat/lon."""
    module = _load_publisher_module(monkeypatch)
    scenario_dir = _write_scenario(tmp_path, [4.3, 4.3])
    node = _make_node(module, scenario_dir)

    node._load("colreg-test")

    assert len(node._waypoints) == 2
    assert node._yaml_speeds_kn == [4.3, 4.3]
    assert node._is_active is True


def test_on_timer_fills_speed_limit_mps(tmp_path, monkeypatch):
    """_on_timer built RoutePlan must populate speed_limit_mps (kn -> m/s)."""
    module = _load_publisher_module(monkeypatch)
    scenario_dir = _write_scenario(tmp_path, [4.3, 4.3])
    node = _make_node(module, scenario_dir)
    node._load("colreg-test")

    node._on_timer()

    assert len(node._pub.messages) == 1
    msg = node._pub.messages[0]
    assert hasattr(msg, "speed_limit_mps")
    assert len(msg.speed_limit_mps) == 2
    # 4.3 kn * 0.514444 m/s per kn
    assert abs(msg.speed_limit_mps[0] - 4.3 * 0.514444) < 1e-6
    assert abs(msg.speed_limit_mps[1] - 4.3 * 0.514444) < 1e-6


def test_missing_or_zero_target_sog_omits_speed_limit(tmp_path, monkeypatch):
    """Counterfactual: waypoints without a usable target_sog_kn (missing/0)
    must NOT populate speed_limit_mps, so GNC keeps "no limit" semantics rather
    than being incorrectly clamped to 0 m/s."""
    scenario_dir = tmp_path / "scenarios"
    scenario_dir.mkdir()
    (scenario_dir / "colreg-nospeed.yaml").write_text(
        "ownShip:\n"
        "  nominalRoute:\n"
        "  - latitude: 63.44\n"
        "    longitude: 10.38\n"
        "  - latitude: 63.606667\n"
        "    longitude: 10.38\n"
    )
    module = _load_publisher_module(monkeypatch)
    node = _make_node(module, scenario_dir)
    node._load("colreg-nospeed")

    node._on_timer()

    assert len(node._pub.messages) == 1
    msg = node._pub.messages[0]
    assert not hasattr(msg, "speed_limit_mps"), \
        "speed_limit_mps must be omitted when target_sog is missing/zero"
