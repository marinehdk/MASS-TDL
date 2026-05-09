"""Serialize parsed EncPolygons to JSON consumable by C++ EncLoader."""

from __future__ import annotations

import json
from pathlib import Path

from enc_loader.s57_parser import ChartBounds, EncPolygon


def serialize(
    chart_name: str,
    bounds: ChartBounds,
    polygons: list[EncPolygon],
    out_path: Path,
) -> None:
    """Write Boost.Geometry-compatible JSON.

    Schema:
    {
      "name": str,
      "bounds": {lat_min, lat_max, lon_min, lon_max},
      "hazards":  [[[lon, lat], ...], ...],
      "tss_lanes": [[[lon, lat], ...], ...]
    }
    """
    hazards = [
        [list(pt) for pt in poly.coordinates]
        for poly in polygons
        if poly.feature_class in {"OBSTRN", "DEPCNT", "DEPARE"}
    ]
    tss_lanes = [
        [list(pt) for pt in poly.coordinates]
        for poly in polygons
        if poly.feature_class in {"TSSLPT", "TSSBND"}
    ]

    payload = {
        "name": chart_name,
        "bounds": {
            "lat_min": bounds.lat_min,
            "lat_max": bounds.lat_max,
            "lon_min": bounds.lon_min,
            "lon_max": bounds.lon_max,
        },
        "hazards": hazards,
        "tss_lanes": tss_lanes,
    }
    out_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
