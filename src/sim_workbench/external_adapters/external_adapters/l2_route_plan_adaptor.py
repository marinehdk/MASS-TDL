from __future__ import annotations

import hashlib
import json
import math
import os
import socket
from typing import Any

from external_adapters.converters import route_points_to_planned_route_dict
from external_adapters.ipc import encode_payload
from external_adapters.neutral import NeutralRoutePoint

try:
    import rclpy
    from rclpy.node import Node
    from rclpy.qos import QoSDurabilityPolicy, QoSHistoryPolicy, QoSProfile, QoSReliabilityPolicy
except ImportError:
    rclpy = None
    Node = object
    QoSDurabilityPolicy = None
    QoSHistoryPolicy = None
    QoSProfile = None
    QoSReliabilityPolicy = None

try:
    from ship_interfaces.msg import RoutePlan
except ImportError:
    RoutePlan = None

try:
    from sil_msgs.msg import LifecycleStatus
except ImportError:
    LifecycleStatus = None


MPS_PER_KNOT = 0.514444
DEFAULT_SPEED_KN = 10.0
ACTIVE_STATE = 3


class RoutePlanValidationError(ValueError):
    pass


def stable_route_id_from_string(route_id: str) -> int:
    digest = hashlib.sha256(str(route_id).encode("utf-8")).digest()
    return int.from_bytes(digest[:4], "big") or 1


def _stamp_from_msg(msg) -> tuple[int, int]:
    stamp = getattr(getattr(msg, "header", None), "stamp", None)
    return int(getattr(stamp, "sec", 0)), int(getattr(stamp, "nanosec", 0))


def route_plan_to_neutral_points(
    msg,
    default_speed_kn: float = DEFAULT_SPEED_KN,
) -> list[NeutralRoutePoint]:
    default_speed = _validate_default_speed(default_speed_kn)
    latitudes, longitudes, speeds, _ = _validated_route_fields(msg, default_speed)
    return [
        NeutralRoutePoint(
            lat=lat,
            lon=lon,
            speed_kn=_speed_mps_to_kn(speeds[index], default_speed)
            if speeds
            else default_speed,
        )
        for index, (lat, lon) in enumerate(zip(latitudes, longitudes))
    ]


def route_plan_signature(msg, default_speed_kn: float = DEFAULT_SPEED_KN) -> str:
    points = route_plan_to_neutral_points(msg, default_speed_kn)
    payload = {
        "route_id": _external_route_id(msg),
        "route_type": _route_type(msg),
        "points": [
            {"lat": point.lat, "lon": point.lon, "speed_kn": point.speed_kn}
            for point in points
        ],
    }
    encoded = json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def should_forward_route(last_signature: str | None, new_signature: str) -> bool:
    return last_signature != new_signature


def route_plan_to_payload(msg, default_speed_kn: float = DEFAULT_SPEED_KN) -> dict[str, Any]:
    stamp_sec, stamp_nanosec = _stamp_from_msg(msg)
    points = route_plan_to_neutral_points(msg, default_speed_kn)
    payload = route_points_to_planned_route_dict(stamp_sec, stamp_nanosec, points)

    route_id = _external_route_id(msg)
    route_ref = "waypoint-signature"
    if route_id:
        payload["route_id"] = stable_route_id_from_string(route_id)
        route_ref = route_id

    _, _, _, navigation_modes = _validated_route_fields(
        msg,
        _validate_default_speed(default_speed_kn),
    )
    payload["rationale"] = (
        "external L2 route plan converted to planned route; "
        f"route_id={route_ref}; "
        f"route_type={_route_type(msg) or 'none'}; "
        f"navigation_modes={_navigation_modes_label(navigation_modes)}"
    )
    return payload


class L2RoutePlanAdaptorNode(Node):
    def __init__(self) -> None:
        super().__init__("l2_route_plan_adaptor")
        self.declare_parameter("host", os.environ.get("TDL_INGRESS_HOST", "127.0.0.1"))
        self.declare_parameter("port", int(os.environ.get("TDL_INGRESS_PORT", "8765")))
        self.declare_parameter(
            "default_speed_kn",
            float(os.environ.get("L2_DEFAULT_SPEED_KN", str(DEFAULT_SPEED_KN))),
        )
        self.declare_parameter(
            "strict_active",
            _env_bool("L2_ROUTE_STRICT_ACTIVE", default=True),
        )
        self.declare_parameter(
            "startup_timeout_s",
            float(os.environ.get("L2_ROUTE_STARTUP_TIMEOUT_S", "30.0")),
        )
        self.declare_parameter(
            "retry_interval_s",
            float(os.environ.get("L2_ROUTE_RETRY_INTERVAL_S", "1.0")),
        )

        self._host = str(self.get_parameter("host").value)
        self._port = int(self.get_parameter("port").value)
        self._default_speed_kn = float(self.get_parameter("default_speed_kn").value)
        self._strict_active = bool(self.get_parameter("strict_active").value)
        self._startup_timeout_s = float(self.get_parameter("startup_timeout_s").value)
        self._retry_interval_s = float(self.get_parameter("retry_interval_s").value)
        self._active = not self._strict_active
        self._last_signature: str | None = None
        self._pending_route = None
        self._pending_signature: str | None = None
        self._forwarded_once = False
        self._seen_valid_route = False

        qos = _reliable_transient_qos(depth=10)
        self.create_subscription(
            RoutePlan,
            "/route_planning/route_plan",
            self._on_route_plan,
            qos,
        )
        self.create_subscription(
            LifecycleStatus,
            "/sil/lifecycle_status",
            self._on_lifecycle_status,
            qos,
        )
        if self._strict_active and self._startup_timeout_s > 0.0:
            self.create_timer(self._startup_timeout_s, self._on_startup_timeout)
        if self._retry_interval_s > 0.0:
            self.create_timer(self._retry_interval_s, self._on_retry_timer)

    def _on_route_plan(self, msg) -> None:
        try:
            signature = route_plan_signature(msg, self._default_speed_kn)
        except RoutePlanValidationError as exc:
            self.get_logger().warn(f"ignored invalid L2 route plan: {exc}")
            return

        self._seen_valid_route = True
        if self._strict_active and not self._active:
            self._pending_route = msg
            self._pending_signature = signature
            self.get_logger().info("cached L2 route plan until lifecycle ACTIVE")
            return

        self._forward_if_new(msg, signature)

    def _on_lifecycle_status(self, msg) -> None:
        was_active = self._active
        self._active = getattr(msg, "current_state", 0) == ACTIVE_STATE
        if (
            self._strict_active
            and self._active
            and not was_active
            and self._pending_route is not None
        ):
            self._forward_if_new(self._pending_route, self._pending_signature)

    def _forward_if_new(self, msg, signature: str | None) -> None:
        if signature is None:
            return
        if not should_forward_route(self._last_signature, signature):
            if self._pending_signature is not None and self._pending_signature != signature:
                self._pending_route = None
                self._pending_signature = None
            return
        try:
            payload = route_plan_to_payload(msg, self._default_speed_kn)
            self._send_payload(payload)
        except RoutePlanValidationError as exc:
            self.get_logger().warn(f"ignored invalid L2 route plan: {exc}")
            return
        except OSError as exc:
            self._pending_route = msg
            self._pending_signature = signature
            self.get_logger().warn(f"failed to forward L2 route plan: {exc}")
            return
        self._last_signature = signature
        self._forwarded_once = True
        self._pending_route = None
        self._pending_signature = None
        self.get_logger().info(f"forwarded L2 route plan route_id={payload.get('route_id')}")

    def _send_payload(self, payload: dict[str, Any]) -> None:
        with socket.create_connection((self._host, self._port), timeout=1.0) as sock:
            sock.sendall(encode_payload(payload))

    def _on_startup_timeout(self) -> None:
        if self._strict_active and not self._seen_valid_route:
            self.get_logger().error(
                "no valid L2 route plan seen before strict-active startup timeout"
            )
            os._exit(2)

    def _on_retry_timer(self) -> None:
        if self._pending_route is None:
            return
        if self._strict_active and not self._active:
            return
        self._forward_if_new(self._pending_route, self._pending_signature)


def _validated_route_fields(
    msg,
    default_speed_kn: float,
) -> tuple[list[float], list[float], list[float], list[str]]:
    _validate_default_speed(default_speed_kn)
    latitudes = _numeric_finite_list(getattr(msg, "latitude", []), "latitude")
    longitudes = _numeric_finite_list(getattr(msg, "longitude", []), "longitude")
    if len(latitudes) != len(longitudes):
        raise RoutePlanValidationError("latitude and longitude must be same length")
    if len(latitudes) < 2:
        raise RoutePlanValidationError("route plan must contain at least two waypoints")
    for index, latitude in enumerate(latitudes):
        if latitude < -90.0 or latitude > 90.0:
            raise RoutePlanValidationError(f"latitude[{index}] must be within [-90, 90]")
    for index, longitude in enumerate(longitudes):
        if longitude < -180.0 or longitude > 180.0:
            raise RoutePlanValidationError(f"longitude[{index}] must be within [-180, 180]")

    speeds = _numeric_finite_list(getattr(msg, "speed_limit_mps", []), "speed_limit_mps")
    navigation_modes = [str(mode).strip() for mode in getattr(msg, "navigation_mode", [])]
    speed_length_invalid = bool(speeds) and len(speeds) != len(latitudes)
    navigation_length_invalid = bool(navigation_modes) and len(navigation_modes) != len(latitudes)
    if navigation_length_invalid and not any(mode == "" for mode in navigation_modes):
        raise RoutePlanValidationError("navigation_mode must be empty or same length as latitude")
    if speed_length_invalid:
        raise RoutePlanValidationError("speed_limit_mps must be empty or same length as latitude")
    for index, speed_mps in enumerate(speeds):
        if speed_mps < 0.0:
            raise RoutePlanValidationError(f"speed_limit_mps[{index}] must be non-negative")
    if navigation_length_invalid:
        raise RoutePlanValidationError("navigation_mode must be empty or same length as latitude")
    return latitudes, longitudes, speeds, navigation_modes


def _numeric_finite_list(values, field_name: str) -> list[float]:
    try:
        rows = list(values)
    except TypeError as exc:
        raise RoutePlanValidationError(f"{field_name} must be an array") from exc
    numeric_rows: list[float] = []
    for index, value in enumerate(rows):
        try:
            numeric = float(value)
        except (TypeError, ValueError) as exc:
            raise RoutePlanValidationError(f"{field_name}[{index}] must be numeric") from exc
        if not math.isfinite(numeric):
            raise RoutePlanValidationError(f"{field_name}[{index}] must be finite")
        numeric_rows.append(numeric)
    return numeric_rows


def _validate_default_speed(default_speed_kn: float) -> float:
    try:
        speed = float(default_speed_kn)
    except (TypeError, ValueError) as exc:
        raise RoutePlanValidationError("default speed must be numeric") from exc
    if not math.isfinite(speed) or speed <= 0.0:
        raise RoutePlanValidationError("default speed must be finite and positive")
    return speed


def _speed_mps_to_kn(speed_mps: float, default_speed_kn: float) -> float:
    if speed_mps > 0.0:
        return speed_mps / MPS_PER_KNOT
    return default_speed_kn


def _external_route_id(msg) -> str:
    return str(getattr(msg, "route_id", "")).strip()


def _route_type(msg) -> str:
    return str(getattr(msg, "route_type", "")).strip()


def _navigation_modes_label(navigation_modes: list[str]) -> str:
    non_empty_modes = [mode for mode in navigation_modes if mode]
    if not non_empty_modes:
        return "none"
    return ",".join(non_empty_modes)


def _reliable_transient_qos(depth: int):
    return QoSProfile(
        history=QoSHistoryPolicy.KEEP_LAST,
        depth=depth,
        reliability=QoSReliabilityPolicy.RELIABLE,
        durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
    )


def _env_bool(name: str, default: bool) -> bool:
    raw = os.environ.get(name)
    if raw is None:
        return default
    return raw.strip().lower() not in {"0", "false", "no", "off"}


def main(args=None) -> None:
    if rclpy is None or RoutePlan is None or LifecycleStatus is None:
        raise RuntimeError(
            "rclpy, ship_interfaces, and sil_msgs are required to run l2_route_plan_adaptor"
        )
    rclpy.init(args=args)
    node = L2RoutePlanAdaptorNode()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()
