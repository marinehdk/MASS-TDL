from __future__ import annotations

import argparse
import json
import math
import os
from collections.abc import Mapping
from pathlib import Path
from typing import Any

import yaml

try:
    import rclpy
    from rclpy.node import Node
except ImportError:
    rclpy = None
    Node = object

try:
    from sil_msgs.msg import LifecycleStatus
except ImportError:
    LifecycleStatus = None


ACTIVE_STATE = 3
DEFAULT_SCENARIO_YAML = "/var/sil/scenarios/集成测试/safe_route.yaml"
DEFAULT_OUTPUT_PATH = "/var/lib/l2_route/gnc_bridge_route.json"
DEFAULT_SPEED_KN = 10.0


class RouteSeedError(ValueError):
    pass


def _finite_float(value, field_name: str) -> float:
    if isinstance(value, bool):
        raise RouteSeedError(f"{field_name} must be numeric, not boolean")
    try:
        numeric = float(value)
    except (TypeError, ValueError) as exc:
        raise RouteSeedError(f"{field_name} must be numeric") from exc
    if not math.isfinite(numeric):
        raise RouteSeedError(f"{field_name} must be finite")
    return numeric


def scenario_to_bridge_route(
    scenario_yaml: str | Path,
    default_speed_kn: float = DEFAULT_SPEED_KN,
) -> dict[str, Any]:
    path = Path(scenario_yaml)
    default_speed = _validate_default_speed(default_speed_kn)
    scenario = _load_scenario_mapping(path)
    route = _nominal_route(scenario)
    scenario_id = _scenario_id(scenario, path)

    sample_points = [
        _waypoint_to_sample_point(waypoint, index, default_speed)
        for index, waypoint in enumerate(route)
    ]
    return {
        "route_id": f"{scenario_id}-initial",
        "route_type": "transit",
        "selected_key": scenario_id,
        "sample_points": sample_points,
    }


def write_bridge_route_file(route: dict[str, Any], output_path: str | Path) -> None:
    path = Path(output_path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = path.with_name(f"{path.name}.tmp")
    try:
        with tmp_path.open("w", encoding="utf-8") as handle:
            json.dump(route, handle, ensure_ascii=False, indent=2, sort_keys=True)
            handle.write("\n")
        os.replace(tmp_path, path)
    finally:
        if tmp_path.exists():
            tmp_path.unlink()


class RouteSeedOnActiveNode(Node):
    def __init__(
        self,
        scenario_yaml: str | None = None,
        output_path: str | None = None,
    ) -> None:
        if rclpy is None or LifecycleStatus is None:
            raise RuntimeError(
                "rclpy and sil_msgs are required to run l2_route_seed_on_active"
            )
        super().__init__("l2_route_seed_on_active")
        self.declare_parameter(
            "scenario_yaml",
            scenario_yaml or os.environ.get("L2_SCENARIO_YAML", DEFAULT_SCENARIO_YAML),
        )
        self.declare_parameter(
            "output_path",
            output_path or os.environ.get("L2_ROUTE_OUTPUT_PATH", DEFAULT_OUTPUT_PATH),
        )
        self.declare_parameter(
            "remove_on_start",
            _env_bool("L2_ROUTE_REMOVE_ON_START", default=True),
        )

        self._scenario_yaml = str(self.get_parameter("scenario_yaml").value)
        self._output_path = Path(str(self.get_parameter("output_path").value))
        self._written = False
        if bool(self.get_parameter("remove_on_start").value):
            self._remove_existing_output()

        self.create_subscription(
            LifecycleStatus,
            "/sil/lifecycle_status",
            self._on_lifecycle_status,
            10,
        )

    def _remove_existing_output(self) -> None:
        tmp_path = self._output_path.with_name(f"{self._output_path.name}.tmp")
        for path in (self._output_path, tmp_path):
            try:
                path.unlink()
            except FileNotFoundError:
                pass

    def _on_lifecycle_status(self, msg) -> None:
        if self._written or getattr(msg, "current_state", None) != ACTIVE_STATE:
            return
        try:
            route = scenario_to_bridge_route(self._scenario_yaml)
            write_bridge_route_file(route, self._output_path)
        except (OSError, RouteSeedError, yaml.YAMLError) as exc:
            self.get_logger().error(f"failed to seed L2 route: {exc}")
            return
        self._written = True
        self.get_logger().info(
            f"seeded L2 bridge route with {len(route['sample_points'])} waypoints"
        )


def _load_scenario_mapping(path: Path) -> Mapping[str, Any]:
    try:
        scenario = yaml.safe_load(path.read_text(encoding="utf-8"))
    except yaml.YAMLError as exc:
        raise RouteSeedError(f"scenario YAML is malformed: {exc}") from exc
    except OSError as exc:
        raise RouteSeedError(f"scenario YAML is not readable: {exc}") from exc
    if not isinstance(scenario, Mapping):
        raise RouteSeedError("scenario YAML must be a mapping")
    return scenario


def _nominal_route(scenario: Mapping[str, Any]) -> list[Any]:
    own_ship = scenario.get("ownShip")
    if not isinstance(own_ship, Mapping):
        raise RouteSeedError("ownShip must be a mapping")
    route = own_ship.get("nominalRoute")
    if not isinstance(route, list):
        raise RouteSeedError("ownShip.nominalRoute must be a list")
    if len(route) < 2:
        raise RouteSeedError("ownShip.nominalRoute must contain at least two waypoints")
    return route


def _scenario_id(scenario: Mapping[str, Any], path: Path) -> str:
    metadata = scenario.get("metadata")
    if isinstance(metadata, Mapping):
        raw_scenario_id = metadata.get("scenario_id")
        if raw_scenario_id is not None:
            scenario_id = str(raw_scenario_id).strip()
            if scenario_id:
                return scenario_id
    return path.stem


def _waypoint_to_sample_point(
    waypoint,
    index: int,
    default_speed_kn: float,
) -> dict[str, float]:
    if not isinstance(waypoint, Mapping):
        raise RouteSeedError(f"waypoint[{index}] must be a mapping")

    lat = _required_coordinate(waypoint, "latitude", index, -90.0, 90.0)
    lon = _required_coordinate(waypoint, "longitude", index, -180.0, 180.0)
    speed = _waypoint_speed_kn(waypoint, index, default_speed_kn)
    return {"lat": lat, "lon": lon, "speed_kn": speed}


def _required_coordinate(
    waypoint: Mapping[str, Any],
    field_name: str,
    index: int,
    minimum: float,
    maximum: float,
) -> float:
    if field_name not in waypoint:
        raise RouteSeedError(f"waypoint[{index}].{field_name} is required")
    value = _finite_float(waypoint[field_name], f"waypoint[{index}].{field_name}")
    if value < minimum or value > maximum:
        raise RouteSeedError(
            f"waypoint[{index}].{field_name} must be within [{minimum:g}, {maximum:g}]"
        )
    return value


def _waypoint_speed_kn(
    waypoint: Mapping[str, Any],
    index: int,
    default_speed_kn: float,
) -> float:
    if "target_sog_kn" in waypoint:
        speed = _finite_float(waypoint["target_sog_kn"], f"waypoint[{index}].speed")
    elif "speed_kn" in waypoint:
        speed = _finite_float(waypoint["speed_kn"], f"waypoint[{index}].speed")
    else:
        return default_speed_kn

    if speed < 0.0:
        raise RouteSeedError(f"waypoint[{index}].speed must be non-negative")
    if speed <= 0.0:
        return default_speed_kn
    return speed


def _validate_default_speed(default_speed_kn: float) -> float:
    default_speed = _finite_float(default_speed_kn, "default speed")
    if default_speed <= 0.0:
        raise RouteSeedError("default speed must be finite and positive")
    return default_speed


def _env_bool(name: str, default: bool) -> bool:
    raw = os.environ.get(name)
    if raw is None:
        return default
    return raw.strip().lower() not in {"0", "false", "no", "off", ""}


def main(args=None) -> None:
    parser = argparse.ArgumentParser(
        description="Seed the L2 bridge route after lifecycle ACTIVE."
    )
    parser.add_argument("--write-once", action="store_true", help="write route file without ROS2")
    parser.add_argument("--scenario-yaml")
    parser.add_argument("--output-path")
    parsed, remaining = parser.parse_known_args(args)

    if parsed.write_once:
        scenario_yaml = parsed.scenario_yaml or DEFAULT_SCENARIO_YAML
        output_path = parsed.output_path or DEFAULT_OUTPUT_PATH
        route = scenario_to_bridge_route(scenario_yaml)
        write_bridge_route_file(route, output_path)
        return

    if rclpy is None or LifecycleStatus is None:
        raise RuntimeError(
            "rclpy and sil_msgs are required to run l2_route_seed_on_active"
        )
    rclpy.init(args=remaining)
    node = RouteSeedOnActiveNode(
        scenario_yaml=parsed.scenario_yaml,
        output_path=parsed.output_path,
    )
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
