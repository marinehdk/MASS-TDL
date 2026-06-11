from __future__ import annotations

from pathlib import Path
import math
import yaml

from ais_twin.model import BBox, RoutePoint

EARTH_RADIUS_NM = 3440.065


def load_route_points(path: Path) -> list[RoutePoint]:
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    nominal = data["ownShip"]["nominalRoute"]
    return [
        RoutePoint(
            lat=float(wp["latitude"]),
            lon=float(wp["longitude"]),
            target_sog_kn=float(wp.get("target_sog_kn", 10.0)),
        )
        for wp in nominal
    ]


def route_bbox(points: list[RoutePoint]) -> BBox:
    return BBox(
        lat_min=min(p.lat for p in points),
        lat_max=max(p.lat for p in points),
        lon_min=min(p.lon for p in points),
        lon_max=max(p.lon for p in points),
    )


def haversine_nm(a: RoutePoint, b: RoutePoint) -> float:
    lat1 = math.radians(a.lat)
    lat2 = math.radians(b.lat)
    dlat = lat2 - lat1
    dlon = math.radians(b.lon - a.lon)
    h = math.sin(dlat / 2.0) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin(dlon / 2.0) ** 2
    return 2.0 * EARTH_RADIUS_NM * math.asin(math.sqrt(h))


def route_duration_hours(points: list[RoutePoint]) -> float:
    total_h = 0.0
    for idx, (a, b) in enumerate(zip(points, points[1:])):
        if a.target_sog_kn <= 0:
            raise ValueError(f"target_sog_kn must be positive at segment {idx}: {a.target_sog_kn}")
        total_h += haversine_nm(a, b) / a.target_sog_kn
    return total_h
