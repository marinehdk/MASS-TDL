from __future__ import annotations

import math

EARTH_RADIUS_M = 6371000.0


def distance_m(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    dlat = math.radians(lat2 - lat1)
    dlon = math.radians(lon2 - lon1)
    a = (
        math.sin(dlat / 2) ** 2
        + math.cos(math.radians(lat1)) * math.cos(math.radians(lat2)) * math.sin(dlon / 2) ** 2
    )
    return 2 * EARTH_RADIUS_M * math.asin(math.sqrt(a))


def heading_delta_deg(a: float, b: float) -> float:
    return abs((a - b + 180.0) % 360.0 - 180.0)


def bearing_deg(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    y = math.radians(lon2 - lon1) * math.cos(math.radians((lat1 + lat2) / 2.0))
    x = math.radians(lat2 - lat1)
    return (math.degrees(math.atan2(y, x)) + 360.0) % 360.0


def destination_along_segment(
    a_lat: float,
    a_lon: float,
    b_lat: float,
    b_lon: float,
    fraction: float,
) -> tuple[float, float]:
    clamped = max(0.0, min(1.0, fraction))
    return (
        a_lat + (b_lat - a_lat) * clamped,
        a_lon + (b_lon - a_lon) * clamped,
    )


def latlon_to_local_m(lat: float, lon: float, origin_lat: float, origin_lon: float) -> tuple[float, float]:
    x = math.radians(lon - origin_lon) * math.cos(math.radians(origin_lat)) * EARTH_RADIUS_M
    y = math.radians(lat - origin_lat) * EARTH_RADIUS_M
    return x, y


def velocity_components_mps(sog_kn: float, cog_deg: float) -> tuple[float, float]:
    speed_mps = sog_kn * 0.514444
    cog_rad = math.radians(cog_deg)
    return speed_mps * math.sin(cog_rad), speed_mps * math.cos(cog_rad)


def cpa_tcpa_m(px: float, py: float, vx: float, vy: float) -> tuple[float, float]:
    speed_sq = vx * vx + vy * vy
    if speed_sq < 1e-6:
        return math.hypot(px, py), float("inf")
    tcpa_s = max(0.0, -((px * vx) + (py * vy)) / speed_sq)
    cpa_x = px + vx * tcpa_s
    cpa_y = py + vy * tcpa_s
    return math.hypot(cpa_x, cpa_y), tcpa_s
