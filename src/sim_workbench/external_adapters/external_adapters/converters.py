from __future__ import annotations

import hashlib
import math
from typing import Iterable

from external_adapters.neutral import (
    NeutralEnvironment,
    NeutralOwnship,
    NeutralRoutePoint,
    NeutralTarget,
)

SCHEMA_VERSION = 112
EARTH_RADIUS_NM = 3440.065
MIN_SEGMENT_SPEED_KN = 0.1


def _stamp(sec: int, nanosec: int) -> dict[str, int]:
    return {"sec": int(sec), "nanosec": int(nanosec)}


def _haversine_nm(a: NeutralRoutePoint, b: NeutralRoutePoint) -> float:
    lat1 = math.radians(a.lat)
    lat2 = math.radians(b.lat)
    dlat = lat2 - lat1
    dlon = math.radians(b.lon - a.lon)
    sin_dlat = math.sin(dlat / 2.0)
    sin_dlon = math.sin(dlon / 2.0)
    h = sin_dlat * sin_dlat + math.cos(lat1) * math.cos(lat2) * sin_dlon * sin_dlon
    return 2.0 * EARTH_RADIUS_NM * math.asin(math.sqrt(h))


def neutral_targets_to_canonical_dict(
    stamp_sec: int,
    stamp_nanosec: int,
    targets: Iterable[NeutralTarget],
) -> dict:
    target_rows = [
        {
            "schema_version": SCHEMA_VERSION,
            "stamp": _stamp(stamp_sec, stamp_nanosec),
            "target_id": target.target_id,
            "position": {"latitude": target.lat, "longitude": target.lon, "altitude": 0.0},
            "sog_kn": target.sog_kn,
            "cog_deg": target.cog_deg,
            "heading_deg": target.heading_deg,
            "covariance": [0.0] * 9,
            "classification": "vessel",
            "classification_confidence": 0.0,
            "cpa_m": 0.0,
            "tcpa_s": 0.0,
            "encounter": {
                "schema_version": SCHEMA_VERSION,
                "stamp": _stamp(stamp_sec, stamp_nanosec),
                "confidence": 0.0,
                "rationale": "M2 owns classification",
                "encounter_type": 0,
                "relative_bearing_deg": 0.0,
                "aspect_angle_deg": 0.0,
                "is_giveway": False,
            },
            "confidence": target.confidence,
            "rationale": "external neutral target converted to canonical l3 target",
            "source_sensor": target.source_sensor,
            "cpa_covariance_m2": 0.0,
            "tcpa_covariance_s2": 0.0,
            "intent_confidence": 0.0,
            "brg_deg": 0.0,
            "rng_m": 0.0,
        }
        for target in targets
    ]
    confidence = min((target["confidence"] for target in target_rows), default=0.0)
    return {
        "kind": "targets",
        "schema_version": SCHEMA_VERSION,
        "stamp": _stamp(stamp_sec, stamp_nanosec),
        "targets": target_rows,
        "confidence": confidence,
        "rationale": "external neutral targets converted to canonical target set",
    }


def neutral_ownship_to_canonical_dict(ownship: NeutralOwnship) -> dict:
    return {
        "kind": "ownship",
        "schema_version": SCHEMA_VERSION,
        "stamp": _stamp(ownship.stamp_sec, ownship.stamp_nanosec),
        "position": {"latitude": ownship.lat, "longitude": ownship.lon, "altitude": 0.0},
        "sog_kn": ownship.sog_kn,
        "cog_deg": ownship.cog_deg,
        "heading_deg": ownship.heading_deg,
        "u_water": ownship.u_water,
        "v_water": ownship.v_water,
        "r_dot_deg_s": ownship.r_dot_deg_s,
        "current_speed_kn": ownship.current_speed_kn,
        "current_direction_deg": ownship.current_direction_deg,
        "roll_deg": 0.0,
        "pitch_deg": 0.0,
        "covariance": [0.0] * 36,
        "nav_mode": ownship.nav_mode,
        "confidence": ownship.confidence,
        "rationale": "external neutral ownship converted to canonical ownship state",
    }


def neutral_environment_to_canonical_dict(environment: NeutralEnvironment) -> dict:
    return {
        "kind": "environment",
        "schema_version": SCHEMA_VERSION,
        "stamp": _stamp(environment.stamp_sec, environment.stamp_nanosec),
        "wind_speed_kn": environment.wind_speed_kn,
        "wind_direction_deg": environment.wind_direction_deg,
        "wave_height_m": 0.0,
        "wave_direction_deg": 0.0,
        "current_speed_kn": environment.current_speed_kn,
        "current_direction_deg": environment.current_direction_deg,
        "visibility_range_nm": environment.visibility_range_nm,
        "weather_source": "sensor",
        "confidence": environment.confidence,
        "rationale": "external neutral environment converted to canonical environment",
    }


def route_points_to_planned_route_dict(
    stamp_sec: int,
    stamp_nanosec: int,
    points: Iterable[NeutralRoutePoint],
) -> dict:
    point_rows = list(points)
    total_distance_nm = sum(_haversine_nm(a, b) for a, b in zip(point_rows, point_rows[1:]))
    estimated_duration_s = sum(
        (_haversine_nm(a, b) / max(a.speed_kn, MIN_SEGMENT_SPEED_KN)) * 3600.0
        for a, b in zip(point_rows, point_rows[1:])
    )
    route_id = _stable_route_id(point_rows)
    stamp = _stamp(stamp_sec, stamp_nanosec)
    return {
        "kind": "route_in",
        "schema_version": SCHEMA_VERSION,
        "stamp": stamp,
        "route_id": route_id,
        "route": {
            "header": {"stamp": stamp, "frame_id": "WGS84"},
            "poses": [
                {
                    "header": {"stamp": stamp, "frame_id": "WGS84"},
                    "pose": {
                        "position": {
                            "latitude": point.lat,
                            "longitude": point.lon,
                            "altitude": 0.0,
                        },
                        "orientation": {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0},
                    },
                }
                for point in point_rows
            ],
        },
        "total_distance_nm": total_distance_nm,
        "estimated_duration_s": estimated_duration_s,
        "speed_profile_kn": _segment_speed_profile(point_rows),
        "safety_zone": "default",
        "confidence": 1.0,
        "rationale": "external neutral route converted to planned route",
    }


def avoidance_plan_to_path_payload(plan) -> dict:
    stamp = getattr(getattr(plan, "header"), "stamp")
    return {
        "kind": "route_out_path",
        "stamp": _stamp(stamp.sec, stamp.nanosec),
        "confidence": plan.confidence,
        "rationale": plan.rationale,
        "points": [
            {
                "lat": waypoint.position.latitude,
                "lon": waypoint.position.longitude,
                "speed_kn": waypoint.target_speed_kn,
            }
            for waypoint in plan.waypoints
        ],
    }


def _stable_route_id(points: list[NeutralRoutePoint]) -> int:
    payload = "|".join(
        f"{point.lat:.9f},{point.lon:.9f},{point.speed_kn:.6f}" for point in points
    )
    digest = hashlib.sha256(payload.encode("ascii")).digest()
    return int.from_bytes(digest[:4], "big") or 1


def _segment_speed_profile(points: list[NeutralRoutePoint]) -> list[float]:
    if not points:
        return []
    segment_count = max(len(points) - 1, 1)
    return [max(points[index].speed_kn, MIN_SEGMENT_SPEED_KN) for index in range(segment_count)]
