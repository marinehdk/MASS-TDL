import importlib
import json
import sys
import types
from pathlib import Path
from types import SimpleNamespace


class _Publisher:
    __slots__ = ("messages", "msg_type", "qos", "topic")

    def __init__(self, msg_type=None, topic="", qos=None):
        self.msg_type = msg_type
        self.topic = topic
        self.qos = qos
        self.messages = []

    def publish(self, msg):
        self.messages.append(msg)


class _Logger:
    __slots__ = ("infos", "warnings")

    def __init__(self):
        self.infos = []
        self.warnings = []

    def info(self, msg):
        self.infos.append(msg)

    def warn(self, msg):
        self.warnings.append(msg)


class _FakeNode:
    __slots__ = ("destroyed", "logger", "name", "parameters", "publishers", "subscriptions")

    def __init__(self, name):
        self.name = name
        self.parameters = {}
        self.publishers = []
        self.subscriptions = []
        self.logger = _Logger()
        self.destroyed = False

    def declare_parameter(self, name, value):
        self.parameters[name] = value

    def get_parameter(self, name):
        return SimpleNamespace(value=self.parameters[name])

    def create_publisher(self, msg_type, topic, qos):
        publisher = _Publisher(msg_type=msg_type, topic=topic, qos=qos)
        self.publishers.append(publisher)
        return publisher

    def create_subscription(self, msg_type, topic, callback, qos):
        subscription = SimpleNamespace(msg_type=msg_type, topic=topic, callback=callback, qos=qos)
        self.subscriptions.append(subscription)
        return subscription

    def get_logger(self):
        return self.logger

    def destroy_node(self):
        self.destroyed = True


class _Time:
    __slots__ = ("nanosec", "sec")

    def __init__(self):
        self.sec = 0
        self.nanosec = 0


class _Header:
    __slots__ = ("frame_id", "stamp")

    def __init__(self):
        self.stamp = _Time()
        self.frame_id = ""


class _Point:
    __slots__ = ("x", "y", "z")

    def __init__(self):
        self.x = 0.0
        self.y = 0.0
        self.z = 0.0


class _Quaternion:
    __slots__ = ("w", "x", "y", "z")

    def __init__(self):
        self.x = 0.0
        self.y = 0.0
        self.z = 0.0
        self.w = 1.0


class _Pose:
    __slots__ = ("orientation", "position")

    def __init__(self):
        self.position = _Point()
        self.orientation = _Quaternion()


class _PoseStamped:
    __slots__ = ("header", "pose")

    def __init__(self):
        self.header = _Header()
        self.pose = _Pose()


class _Path:
    __slots__ = ("header", "poses")

    def __init__(self):
        self.header = _Header()
        self.poses = []


class _AvoidancePlan:
    pass


def _install_fake_ros_modules(monkeypatch):
    rclpy = types.ModuleType("rclpy")
    rclpy.init = lambda *args, **kwargs: None
    rclpy.spin = lambda *args, **kwargs: None
    rclpy.shutdown = lambda *args, **kwargs: None
    rclpy.node = types.ModuleType("rclpy.node")
    rclpy.node.Node = _FakeNode

    builtin_interfaces = types.ModuleType("builtin_interfaces")
    builtin_interfaces.msg = types.ModuleType("builtin_interfaces.msg")
    builtin_interfaces.msg.Time = _Time

    std_msgs = types.ModuleType("std_msgs")
    std_msgs.msg = types.ModuleType("std_msgs.msg")
    std_msgs.msg.Header = _Header

    geometry_msgs = types.ModuleType("geometry_msgs")
    geometry_msgs.msg = types.ModuleType("geometry_msgs.msg")
    geometry_msgs.msg.PoseStamped = _PoseStamped

    nav_msgs = types.ModuleType("nav_msgs")
    nav_msgs.msg = types.ModuleType("nav_msgs.msg")
    nav_msgs.msg.Path = _Path

    l3_msgs = types.ModuleType("l3_msgs")
    l3_msgs.msg = types.ModuleType("l3_msgs.msg")
    l3_msgs.msg.AvoidancePlan = _AvoidancePlan

    for module in (
        rclpy,
        rclpy.node,
        builtin_interfaces,
        builtin_interfaces.msg,
        std_msgs,
        std_msgs.msg,
        geometry_msgs,
        geometry_msgs.msg,
        nav_msgs,
        nav_msgs.msg,
        l3_msgs,
        l3_msgs.msg,
    ):
        monkeypatch.setitem(sys.modules, module.__name__, module)


def _load_module(monkeypatch, name):
    _install_fake_ros_modules(monkeypatch)
    sys.modules.pop(name, None)
    return importlib.import_module(name)


def _plan(waypoints):
    return SimpleNamespace(
        header=SimpleNamespace(stamp=SimpleNamespace(sec=77, nanosec=880)),
        confidence=0.86,
        rationale="route out",
        waypoints=waypoints,
    )


def _waypoint(lat, lon, speed_kn):
    return SimpleNamespace(
        position=SimpleNamespace(latitude=lat, longitude=lon),
        target_speed_kn=speed_kn,
    )


def test_path_payload_to_plain_path_keeps_stamp_frame_and_lon_lat_speed_semantics(monkeypatch):
    module = _load_module(monkeypatch, "external_adapters.route_out_external_path_node")
    payload = {
        "kind": "route_out_path",
        "stamp": {"sec": 12, "nanosec": 345},
        "points": [
            {"lat": 31.11, "lon": 121.22, "speed_kn": 8.5},
            {"lat": 31.33, "lon": 121.44, "speed_kn": 9.0},
        ],
    }

    msg = module.path_payload_to_plain_path(payload)

    assert msg.header.frame_id == "WGS84"
    assert msg.header.stamp.sec == 12
    assert msg.header.stamp.nanosec == 345
    assert len(msg.poses) == 2
    assert msg.poses[0].header.frame_id == "WGS84"
    assert msg.poses[0].header.stamp.sec == 12
    assert msg.poses[0].pose.position.x == 121.22
    assert msg.poses[0].pose.position.y == 31.11
    assert msg.poses[0].pose.position.z == 8.5
    assert msg.poses[0].pose.orientation.w == 1.0
    assert msg.poses[1].pose.position.x == 121.44
    assert msg.poses[1].pose.position.y == 31.33
    assert msg.poses[1].pose.position.z == 9.0


def test_route_out_tdl_node_sends_newline_json_for_nonempty_plan_and_ignores_empty(
    monkeypatch,
):
    module = _load_module(monkeypatch, "external_adapters.route_out_tdl_node")
    sent = []

    class _Socket:
        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc, tb):
            return False

        def sendall(self, data):
            sent.append(data)

    def fake_create_connection(address, timeout):
        assert address == ("127.0.0.1", 8766)
        assert timeout == 1.0
        return _Socket()

    monkeypatch.setattr(module.socket, "create_connection", fake_create_connection)
    node = module.RouteOutTdlNode()

    node._on_plan(_plan([_waypoint(31.11, 121.22, 8.5)]))
    node._on_plan(_plan([]))

    assert len(sent) == 1
    assert sent[0].endswith(b"\n")
    payload = json.loads(sent[0])
    assert payload["kind"] == "route_out_path"
    assert payload["stamp"] == {"sec": 77, "nanosec": 880}
    assert payload["points"] == [{"lat": 31.11, "lon": 121.22, "speed_kn": 8.5}]
    assert node.logger.warnings == ["route_out_tdl ignored empty avoidance plan"]


def test_route_out_tdl_node_logs_socket_failure_without_raising(monkeypatch):
    module = _load_module(monkeypatch, "external_adapters.route_out_tdl_node")

    def fake_create_connection(address, timeout):
        raise ConnectionRefusedError("receiver unavailable")

    monkeypatch.setattr(module.socket, "create_connection", fake_create_connection)
    node = module.RouteOutTdlNode()

    node._on_plan(_plan([_waypoint(31.11, 121.22, 8.5)]))

    assert node.logger.warnings == [
        "route_out_tdl failed to send route_out_path: receiver unavailable"
    ]


def test_route_out_external_path_node_publishes_path_for_route_out_payload(monkeypatch):
    module = _load_module(monkeypatch, "external_adapters.route_out_external_path_node")
    node = module.RouteOutExternalPathNode()
    try:
        publishers = {publisher.topic: publisher for publisher in node.publishers}
        assert set(publishers) == {"/ship/waypoints"}
        assert publishers["/ship/waypoints"].qos == 5

        node._handle_payload(
            {
                "kind": "route_out_path",
                "stamp": {"sec": 12, "nanosec": 345},
                "points": [{"lat": 31.11, "lon": 121.22, "speed_kn": 8.5}],
            }
        )
        node._handle_payload({"kind": "targets", "targets": []})

        assert len(publishers["/ship/waypoints"].messages) == 1
        msg = publishers["/ship/waypoints"].messages[0]
        assert msg.header.frame_id == "WGS84"
        assert msg.poses[0].pose.position.x == 121.22
        assert msg.poses[0].pose.position.y == 31.11
        assert msg.poses[0].pose.position.z == 8.5
    finally:
        node.destroy_node()

    assert node.destroyed is True
    assert node._server.fileno() == -1


def test_path_payload_to_plain_path_rejects_invalid_points(monkeypatch):
    module = _load_module(monkeypatch, "external_adapters.route_out_external_path_node")
    invalid_payloads = [
        {"kind": "route_out_path"},
        {"kind": "route_out_path", "points": []},
        {"kind": "route_out_path", "points": [{"lon": 121.22, "speed_kn": 8.5}]},
        {"kind": "route_out_path", "points": [{"lat": 31.11, "speed_kn": 8.5}]},
        {"kind": "route_out_path", "points": [{"lat": 31.11, "lon": 121.22}]},
        {
            "kind": "route_out_path",
            "points": [{"lat": "bad", "lon": 121.22, "speed_kn": 8.5}],
        },
        {
            "kind": "route_out_path",
            "points": [{"lat": 31.11, "lon": "bad", "speed_kn": 8.5}],
        },
        {
            "kind": "route_out_path",
            "points": [{"lat": 31.11, "lon": 121.22, "speed_kn": "bad"}],
        },
    ]

    for payload in invalid_payloads:
        try:
            module.path_payload_to_plain_path(payload)
        except ValueError:
            continue
        raise AssertionError(f"expected invalid payload to fail: {payload}")


def test_route_out_external_path_node_logs_invalid_payload_without_publish(monkeypatch):
    module = _load_module(monkeypatch, "external_adapters.route_out_external_path_node")
    node = module.RouteOutExternalPathNode()
    try:
        publishers = {publisher.topic: publisher for publisher in node.publishers}

        node._handle_payload({"kind": "route_out_path", "points": []})

        assert publishers["/ship/waypoints"].messages == []
        assert node.logger.warnings == [
            "ignored invalid route_out_path payload: route_out_path points must be a non-empty list"
        ]
    finally:
        node.destroy_node()


def test_route_out_modules_do_not_reference_actuator_topics():
    repo_root = Path(__file__).resolve().parents[3]
    module_paths = [
        repo_root
        / "src/sim_workbench/external_adapters/external_adapters/route_out_tdl_node.py",
        repo_root
        / "src/sim_workbench/external_adapters/external_adapters/route_out_external_path_node.py",
    ]

    source = "\n".join(path.read_text() for path in module_paths)

    assert "/cmd_tau" not in source
    assert "/thruster/commands" not in source
