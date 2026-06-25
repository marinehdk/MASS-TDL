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


class _GeoPoint:
    def __init__(self, latitude=0.0, longitude=0.0, altitude=0.0):
        self.latitude = latitude
        self.longitude = longitude
        self.altitude = altitude


class _GeoPoseStamped:
    def __init__(self):
        self.header = None
        self.pose = SimpleNamespace(
            position=_GeoPoint(),
            orientation=SimpleNamespace(x=0.0, y=0.0, z=0.0, w=1.0),
        )


class _GeoPath:
    def __init__(self):
        self.header = None
        self.poses = []


class _PlannedRoute:
    def __init__(self):
        self.schema_version = 0
        self.stamp = None
        self.route_id = 0
        self.route = None
        self.total_distance_nm = 0.0
        self.estimated_duration_s = 0.0
        self.speed_profile_kn = []
        self.safety_zone = ""
        self.confidence = 0.0
        self.rationale = ""


def _install_fake_ros_modules(monkeypatch):
    rclpy = types.ModuleType("rclpy")
    rclpy.init = lambda *args, **kwargs: None
    rclpy.spin = lambda *args, **kwargs: None
    rclpy.shutdown = lambda *args, **kwargs: None
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

    geographic_msgs = types.ModuleType("geographic_msgs")
    geographic_msgs.msg = types.ModuleType("geographic_msgs.msg")
    geographic_msgs.msg.GeoPath = _GeoPath
    geographic_msgs.msg.GeoPoseStamped = _GeoPoseStamped
    geographic_msgs.msg.GeoPoint = _GeoPoint

    ship_interfaces = types.ModuleType("ship_interfaces")
    ship_interfaces.msg = types.ModuleType("ship_interfaces.msg")
    ship_interfaces.msg.RoutePlan = type("RoutePlan", (), {})

    l3_external_msgs = types.ModuleType("l3_external_msgs")
    l3_external_msgs.msg = types.ModuleType("l3_external_msgs.msg")
    l3_external_msgs.msg.PlannedRoute = _PlannedRoute

    sil_msgs = types.ModuleType("sil_msgs")
    sil_msgs.msg = types.ModuleType("sil_msgs.msg")
    sil_msgs.msg.LifecycleStatus = type("LifecycleStatus", (), {})

    for module in (
        rclpy,
        rclpy.node,
        rclpy.qos,
        std_msgs,
        std_msgs.msg,
        geographic_msgs,
        geographic_msgs.msg,
        ship_interfaces,
        ship_interfaces.msg,
        l3_external_msgs,
        l3_external_msgs.msg,
        sil_msgs,
        sil_msgs.msg,
    ):
        monkeypatch.setitem(sys.modules, module.__name__, module)


def _load_route_ingest(monkeypatch):
    _install_fake_ros_modules(monkeypatch)
    path = Path(__file__).resolve().parents[2] / "docker" / "route_ingest_node.py"
    spec = importlib.util.spec_from_file_location("route_ingest_node_under_test", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def _route_msg(lats, lons, stamp_sec=1):
    return SimpleNamespace(
        header=SimpleNamespace(stamp=SimpleNamespace(sec=stamp_sec, nanosec=0)),
        latitude=lats,
        longitude=lons,
        total_waypoints=len(lats),
    )


def _fake_node():
    logger = SimpleNamespace(info=lambda *args, **kwargs: None,
                             warn=lambda *args, **kwargs: None)
    return SimpleNamespace(
        _route_id=0,
        _last_route_signature=None,
        _last_stamp_ns=-1,
        _expected_route_signature=None,
        _current_scenario_id="safe_route",
        _pub=_Publisher(),
        get_logger=lambda: logger,
    )


def test_repeated_identical_route_keeps_stable_route_id(monkeypatch):
    module = _load_route_ingest(monkeypatch)
    node = _fake_node()

    module.RouteIngestNode._publish_internal(
        node, _route_msg([1.0, 1.1], [100.0, 100.1], stamp_sec=1))
    module.RouteIngestNode._publish_internal(
        node, _route_msg([1.0, 1.1], [100.0, 100.1], stamp_sec=2))

    route_ids = [msg.route_id for msg in node._pub.messages]
    assert route_ids[0] == route_ids[1]


def test_changed_route_increments_route_id(monkeypatch):
    module = _load_route_ingest(monkeypatch)
    node = _fake_node()

    module.RouteIngestNode._publish_internal(
        node, _route_msg([1.0, 1.1], [100.0, 100.1], stamp_sec=1))
    module.RouteIngestNode._publish_internal(
        node, _route_msg([1.0, 1.2], [100.0, 100.2], stamp_sec=2))

    route_ids = [msg.route_id for msg in node._pub.messages]
    assert route_ids[0] != route_ids[1]


def test_route_filter_rejects_non_scenario_gnc_plan(monkeypatch):
    module = _load_route_ingest(monkeypatch)
    node = _fake_node()
    node._expected_route_signature = module._route_signature(
        [1.0, 1.1], [100.0, 100.1])
    node._publish_internal = lambda msg: module.RouteIngestNode._publish_internal(
        node, msg)

    module.RouteIngestNode._on_route(
        node, _route_msg([9.0, 9.1], [120.0, 120.1], stamp_sec=1))

    assert node._pub.messages == []
    assert node._last_stamp_ns == -1
