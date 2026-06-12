import importlib
import sys
import types
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
    __slots__ = ("errors", "infos", "warnings")

    def __init__(self):
        self.infos = []
        self.warnings = []
        self.errors = []

    def info(self, msg):
        self.infos.append(msg)

    def warn(self, msg):
        self.warnings.append(msg)

    def error(self, msg):
        self.errors.append(msg)


class _FakeNode:
    __slots__ = ("destroyed", "name", "parameters", "publishers", "logger")

    def __init__(self, name):
        self.name = name
        self.parameters = {}
        self.publishers = []
        self.logger = _Logger()
        self.destroyed = False

    def declare_parameter(self, name, value):
        self.parameters[name] = 0 if name == "port" else value

    def get_parameter(self, name):
        return SimpleNamespace(value=self.parameters[name])

    def create_publisher(self, msg_type, topic, qos):
        publisher = _Publisher(msg_type=msg_type, topic=topic, qos=qos)
        self.publishers.append(publisher)
        return publisher

    def get_logger(self):
        return self.logger

    def destroy_node(self):
        self.destroyed = True


class _Header:
    __slots__ = ("frame_id", "stamp")

    def __init__(self, stamp=None, frame_id=""):
        self.stamp = stamp
        self.frame_id = frame_id


class _Time:
    __slots__ = ("nanosec", "sec")

    def __init__(self, sec=0, nanosec=0):
        self.sec = sec
        self.nanosec = nanosec


class _GeoPoint:
    __slots__ = ("altitude", "latitude", "longitude")

    def __init__(self, latitude=0.0, longitude=0.0, altitude=0.0):
        self.latitude = latitude
        self.longitude = longitude
        self.altitude = altitude


class _Quaternion:
    __slots__ = ("w", "x", "y", "z")

    def __init__(self):
        self.x = 0.0
        self.y = 0.0
        self.z = 0.0
        self.w = 1.0


class _GeoPose:
    __slots__ = ("orientation", "position")

    def __init__(self):
        self.position = _GeoPoint()
        self.orientation = _Quaternion()


class _GeoPoseStamped:
    __slots__ = ("header", "pose")

    def __init__(self):
        self.header = None
        self.pose = _GeoPose()


class _GeoPath:
    __slots__ = ("header", "poses")

    def __init__(self):
        self.header = None
        self.poses = []


def _message_type(defaults):
    def __init__(self):
        for key, value in defaults.items():
            setattr(self, key, value() if callable(value) else value)

    return type("FakeMsg", (), {"__slots__": tuple(defaults), "__init__": __init__})


def _install_fake_ros_modules(monkeypatch):
    rclpy = types.ModuleType("rclpy")
    rclpy.init = lambda *args, **kwargs: None
    rclpy.spin = lambda *args, **kwargs: None
    rclpy.shutdown = lambda *args, **kwargs: None
    rclpy.node = types.ModuleType("rclpy.node")
    rclpy.node.Node = _FakeNode
    rclpy.qos = types.ModuleType("rclpy.qos")
    rclpy.qos.QoSProfile = lambda **kwargs: kwargs
    rclpy.qos.QoSReliabilityPolicy = SimpleNamespace(RELIABLE=1)
    rclpy.qos.QoSDurabilityPolicy = SimpleNamespace(TRANSIENT_LOCAL=1)
    rclpy.qos.QoSHistoryPolicy = SimpleNamespace(KEEP_LAST=1)

    builtin_interfaces = types.ModuleType("builtin_interfaces")
    builtin_interfaces.msg = types.ModuleType("builtin_interfaces.msg")
    builtin_interfaces.msg.Time = _Time

    std_msgs = types.ModuleType("std_msgs")
    std_msgs.msg = types.ModuleType("std_msgs.msg")
    std_msgs.msg.Header = _Header

    geographic_msgs = types.ModuleType("geographic_msgs")
    geographic_msgs.msg = types.ModuleType("geographic_msgs.msg")
    geographic_msgs.msg.GeoPoint = _GeoPoint
    geographic_msgs.msg.GeoPath = _GeoPath
    geographic_msgs.msg.GeoPoseStamped = _GeoPoseStamped

    l3_msgs = types.ModuleType("l3_msgs")
    l3_msgs.msg = types.ModuleType("l3_msgs.msg")
    l3_msgs.msg.EncounterClassification = _message_type(
        {
            "schema_version": 0,
            "stamp": lambda: _Time(),
            "confidence": 0.0,
            "rationale": "",
            "encounter_type": 0,
            "relative_bearing_deg": 0.0,
            "aspect_angle_deg": 0.0,
            "is_giveway": False,
        }
    )
    l3_msgs.msg.TrackedTarget = _message_type(
        {
            "schema_version": 0,
            "stamp": lambda: _Time(),
            "target_id": 0,
            "position": lambda: _GeoPoint(),
            "sog_kn": 0.0,
            "cog_deg": 0.0,
            "heading_deg": 0.0,
            "covariance": lambda: [0.0] * 9,
            "classification": "",
            "classification_confidence": 0.0,
            "cpa_m": 0.0,
            "tcpa_s": 0.0,
            "encounter": None,
            "confidence": 0.0,
            "rationale": "",
            "source_sensor": "",
            "cpa_covariance_m2": 0.0,
            "tcpa_covariance_s2": 0.0,
            "intent_confidence": 0.0,
            "brg_deg": 0.0,
            "rng_m": 0.0,
        }
    )

    l3_external_msgs = types.ModuleType("l3_external_msgs")
    l3_external_msgs.msg = types.ModuleType("l3_external_msgs.msg")
    l3_external_msgs.msg.TrackedTargetArray = _message_type(
        {
            "schema_version": 0,
            "stamp": lambda: _Time(),
            "targets": list,
            "confidence": 0.0,
            "rationale": "",
        }
    )
    l3_external_msgs.msg.FilteredOwnShipState = _message_type(
        {
            "schema_version": 0,
            "stamp": lambda: _Time(),
            "position": lambda: _GeoPoint(),
            "sog_kn": 0.0,
            "cog_deg": 0.0,
            "heading_deg": 0.0,
            "u_water": 0.0,
            "v_water": 0.0,
            "r_dot_deg_s": 0.0,
            "current_speed_kn": 0.0,
            "current_direction_deg": 0.0,
            "roll_deg": 0.0,
            "pitch_deg": 0.0,
            "covariance": lambda: [0.0] * 36,
            "nav_mode": "",
            "confidence": 0.0,
            "rationale": "",
        }
    )
    l3_external_msgs.msg.EnvironmentState = _message_type(
        {
            "schema_version": 0,
            "stamp": lambda: _Time(),
            "wind_speed_kn": 0.0,
            "wind_direction_deg": 0.0,
            "wave_height_m": 0.0,
            "wave_direction_deg": 0.0,
            "current_speed_kn": 0.0,
            "current_direction_deg": 0.0,
            "visibility_range_nm": 0.0,
            "weather_source": "",
            "confidence": 0.0,
            "rationale": "",
        }
    )
    l3_external_msgs.msg.PlannedRoute = _message_type(
        {
            "schema_version": 0,
            "stamp": lambda: _Time(),
            "route_id": 0,
            "route": lambda: _GeoPath(),
            "total_distance_nm": 0.0,
            "estimated_duration_s": 0.0,
            "speed_profile_kn": list,
            "safety_zone": "",
            "confidence": 0.0,
            "rationale": "",
        }
    )

    for module in (
        rclpy,
        rclpy.node,
        rclpy.qos,
        builtin_interfaces,
        builtin_interfaces.msg,
        std_msgs,
        std_msgs.msg,
        geographic_msgs,
        geographic_msgs.msg,
        l3_msgs,
        l3_msgs.msg,
        l3_external_msgs,
        l3_external_msgs.msg,
    ):
        monkeypatch.setitem(sys.modules, module.__name__, module)


def _load_module(monkeypatch):
    _install_fake_ros_modules(monkeypatch)
    sys.modules.pop("external_adapters.tdl_ingress_node", None)
    return importlib.import_module("external_adapters.tdl_ingress_node")


def _stamp(sec=12, nanosec=345):
    return {"sec": sec, "nanosec": nanosec}


def test_tracked_target_array_maps_nested_target_fields(monkeypatch):
    module = _load_module(monkeypatch)
    payload = {
        "kind": "targets",
        "schema_version": 112,
        "stamp": _stamp(),
        "confidence": 0.7,
        "rationale": "set",
        "targets": [
            {
                "schema_version": 112,
                "stamp": _stamp(),
                "target_id": 7,
                "position": {"latitude": 31.1, "longitude": 121.2, "altitude": 0.0},
                "sog_kn": 12.5,
                "cog_deg": 83.0,
                "heading_deg": 84.0,
                "covariance": [1.0] * 9,
                "classification": "vessel",
                "classification_confidence": 0.9,
                "cpa_m": 50.0,
                "tcpa_s": 80.0,
                "encounter": {
                    "schema_version": 112,
                    "stamp": _stamp(),
                    "confidence": 0.6,
                    "rationale": "crossing",
                    "encounter_type": 3,
                    "relative_bearing_deg": 15.0,
                    "aspect_angle_deg": 120.0,
                    "is_giveway": True,
                },
                "confidence": 0.8,
                "rationale": "target",
                "source_sensor": "ais",
                "cpa_covariance_m2": 1.5,
                "tcpa_covariance_s2": 2.5,
                "intent_confidence": 0.4,
                "brg_deg": 20.0,
                "rng_m": 300.0,
            }
        ],
    }

    msg = module._tracked_target_array(payload)

    assert msg.schema_version == 112
    assert msg.stamp.sec == 12
    assert msg.stamp.nanosec == 345
    assert msg.confidence == 0.7
    assert msg.rationale == "set"
    target = msg.targets[0]
    assert target.schema_version == 112
    assert target.stamp.sec == 12
    assert target.stamp.nanosec == 345
    assert target.target_id == 7
    assert target.position.latitude == 31.1
    assert target.position.longitude == 121.2
    assert target.position.altitude == 0.0
    assert target.sog_kn == 12.5
    assert target.cog_deg == 83.0
    assert target.heading_deg == 84.0
    assert target.covariance == [1.0] * 9
    assert target.classification == "vessel"
    assert target.classification_confidence == 0.9
    assert target.cpa_m == 50.0
    assert target.tcpa_s == 80.0
    assert target.encounter.schema_version == 112
    assert target.encounter.stamp.sec == 12
    assert target.encounter.stamp.nanosec == 345
    assert target.encounter.confidence == 0.6
    assert target.encounter.rationale == "crossing"
    assert target.encounter.encounter_type == 3
    assert target.encounter.relative_bearing_deg == 15.0
    assert target.encounter.aspect_angle_deg == 120.0
    assert target.encounter.is_giveway is True
    assert target.confidence == 0.8
    assert target.rationale == "target"
    assert target.source_sensor == "ais"
    assert target.cpa_covariance_m2 == 1.5
    assert target.tcpa_covariance_s2 == 2.5
    assert target.intent_confidence == 0.4
    assert target.brg_deg == 20.0
    assert target.rng_m == 300.0


def test_ownship_maps_flat_fields_and_covariance(monkeypatch):
    module = _load_module(monkeypatch)
    payload = {
        "kind": "ownship",
        "schema_version": 112,
        "stamp": _stamp(),
        "position": {"latitude": 30.5, "longitude": 122.5, "altitude": 0.0},
        "sog_kn": 10.0,
        "cog_deg": 45.0,
        "heading_deg": 47.0,
        "u_water": 1.2,
        "v_water": -0.4,
        "r_dot_deg_s": 0.03,
        "current_speed_kn": 1.5,
        "current_direction_deg": 210.0,
        "roll_deg": 0.1,
        "pitch_deg": -0.2,
        "covariance": list(range(36)),
        "nav_mode": "OPTIMAL",
        "confidence": 0.91,
        "rationale": "ownship",
    }

    msg = module._ownship(payload)

    assert msg.schema_version == 112
    assert msg.stamp.sec == 12
    assert msg.stamp.nanosec == 345
    assert msg.position.latitude == 30.5
    assert msg.position.longitude == 122.5
    assert msg.position.altitude == 0.0
    assert msg.sog_kn == 10.0
    assert msg.cog_deg == 45.0
    assert msg.heading_deg == 47.0
    assert msg.u_water == 1.2
    assert msg.v_water == -0.4
    assert msg.r_dot_deg_s == 0.03
    assert msg.current_speed_kn == 1.5
    assert msg.current_direction_deg == 210.0
    assert msg.roll_deg == 0.1
    assert msg.pitch_deg == -0.2
    assert msg.covariance == list(range(36))
    assert len(msg.covariance) == 36
    assert msg.nav_mode == "OPTIMAL"
    assert msg.confidence == 0.91
    assert msg.rationale == "ownship"


def test_environment_maps_weather_and_current_fields(monkeypatch):
    module = _load_module(monkeypatch)
    payload = {
        "kind": "environment",
        "schema_version": 112,
        "stamp": _stamp(),
        "wind_speed_kn": 18.0,
        "wind_direction_deg": 125.0,
        "wave_height_m": 1.2,
        "wave_direction_deg": 100.0,
        "current_speed_kn": 2.5,
        "current_direction_deg": 80.0,
        "visibility_range_nm": 5.5,
        "weather_source": "sensor",
        "confidence": 0.72,
        "rationale": "environment",
    }

    msg = module._environment(payload)

    assert msg.schema_version == 112
    assert msg.stamp.sec == 12
    assert msg.stamp.nanosec == 345
    assert msg.wind_speed_kn == 18.0
    assert msg.wind_direction_deg == 125.0
    assert msg.wave_height_m == 1.2
    assert msg.wave_direction_deg == 100.0
    assert msg.current_speed_kn == 2.5
    assert msg.current_direction_deg == 80.0
    assert msg.visibility_range_nm == 5.5
    assert msg.weather_source == "sensor"
    assert msg.confidence == 0.72
    assert msg.rationale == "environment"


def test_planned_route_maps_geopath_pose_position(monkeypatch):
    module = _load_module(monkeypatch)
    payload = {
        "kind": "route_in",
        "schema_version": 112,
        "stamp": _stamp(),
        "route_id": 9,
        "route": {
            "header": {"stamp": _stamp(), "frame_id": "WGS84"},
            "poses": [
                {
                    "header": {"stamp": _stamp(), "frame_id": "WGS84"},
                    "pose": {
                        "position": {"latitude": 1.0, "longitude": 2.0, "altitude": 0.0},
                        "orientation": {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0},
                    },
                }
            ],
        },
        "total_distance_nm": 10.0,
        "estimated_duration_s": 20.0,
        "speed_profile_kn": [8.0],
        "safety_zone": "default",
        "confidence": 1.0,
        "rationale": "route",
    }

    msg = module._planned_route(payload)

    assert msg.schema_version == 112
    assert msg.stamp.sec == 12
    assert msg.stamp.nanosec == 345
    assert msg.route_id == 9
    assert msg.route.header.stamp.sec == 12
    assert msg.route.header.stamp.nanosec == 345
    assert msg.route.header.frame_id == "WGS84"
    assert msg.route.poses[0].header.stamp.sec == 12
    assert msg.route.poses[0].header.stamp.nanosec == 345
    assert msg.route.poses[0].header.frame_id == "WGS84"
    assert msg.route.poses[0].pose.position.latitude == 1.0
    assert msg.route.poses[0].pose.position.longitude == 2.0
    assert msg.route.poses[0].pose.position.altitude == 0.0
    assert msg.route.poses[0].pose.orientation.x == 0.0
    assert msg.route.poses[0].pose.orientation.y == 0.0
    assert msg.route.poses[0].pose.orientation.z == 0.0
    assert msg.route.poses[0].pose.orientation.w == 1.0
    assert msg.total_distance_nm == 10.0
    assert msg.estimated_duration_s == 20.0
    assert msg.speed_profile_kn == [8.0]
    assert msg.safety_zone == "default"
    assert msg.confidence == 1.0
    assert msg.rationale == "route"


def test_fake_messages_reject_unknown_fields(monkeypatch):
    module = _load_module(monkeypatch)

    msg = module._ownship(
        {
            "kind": "ownship",
            "schema_version": 112,
            "stamp": _stamp(),
            "position": {"latitude": 30.5, "longitude": 122.5, "altitude": 0.0},
        }
    )

    try:
        msg.unknown_field = 1
    except AttributeError:
        pass
    else:
        raise AssertionError("fake messages must reject unknown fields")


def test_node_lifecycle_creates_publishers_routes_payloads_and_destroys(monkeypatch):
    module = _load_module(monkeypatch)
    node = module.ExternalTdlIngressNode()
    try:
        publishers = {publisher.topic: publisher for publisher in node.publishers}
        assert set(publishers) == {
            "/fusion/tracked_targets",
            "/fusion/own_ship_state",
            "/fusion/environment_state",
            "/l2/planned_route",
        }
        assert publishers["/fusion/tracked_targets"].msg_type is module.TrackedTargetArray
        assert publishers["/fusion/own_ship_state"].msg_type is module.FilteredOwnShipState
        assert publishers["/fusion/environment_state"].msg_type is module.EnvironmentState
        assert publishers["/l2/planned_route"].msg_type is module.PlannedRoute
        assert publishers["/fusion/tracked_targets"].qos == {
            "history": 1,
            "depth": 1,
            "reliability": 1,
            "durability": 1,
        }

        node._handle_payload(
            {
                "kind": "targets",
                "schema_version": 112,
                "stamp": _stamp(),
                "targets": [],
                "confidence": 1.0,
                "rationale": "empty",
            }
        )
        node._handle_payload(
            {
                "kind": "route_in",
                "schema_version": 112,
                "stamp": _stamp(),
                "route_id": 1,
                "route": {"poses": []},
                "speed_profile_kn": [],
            }
        )

        assert len(publishers["/fusion/tracked_targets"].messages) == 1
        assert len(publishers["/l2/planned_route"].messages) == 1
        assert publishers["/fusion/own_ship_state"].messages == []
        assert publishers["/fusion/environment_state"].messages == []
    finally:
        node.destroy_node()

    assert node.destroyed is True
    assert node._server.fileno() == -1
