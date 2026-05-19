"""Geodetic utility functions for ENU coordinate transforms."""
from __future__ import annotations

import math

_R_EARTH_M = 6_371_000.0
_DEFAULT_GEO_ORIGIN = (63.0, 5.0)  # (lat_deg, lon_deg)


def latlon_to_enu(lat_origin_deg: float, lon_origin_deg: float, lat_deg: float, lon_deg: float) -> tuple[float, float]:
    """Convert geodetic (lat, lon) to ENU (north, east) in metres.

    Uses simple spherical approximation valid for short distances.
    """
    lat_origin_rad = math.radians(lat_origin_deg)
    north_m = (lat_deg - lat_origin_deg) * math.radians(1) * _R_EARTH_M
    east_m = (lon_deg - lon_origin_deg) * math.radians(1) * _R_EARTH_M * math.cos(lat_origin_rad)
    return north_m, east_m


def enu_to_latlon(
    lat_origin_deg: float,
    lon_origin_deg: float,
    north_m: float,
    east_m: float,
) -> tuple[float, float]:
    """Inverse of latlon_to_enu: return (lat_deg, lon_deg) from ENU offset.

    Salvaged from feat/d1.3b.1; valid for offsets < 50 km (error < 0.5 m).
    """
    lat_origin_rad = math.radians(lat_origin_deg)
    lat_deg = lat_origin_deg + math.degrees(north_m / _R_EARTH_M)
    lon_deg = lon_origin_deg + math.degrees(
        east_m / (_R_EARTH_M * math.cos(lat_origin_rad))
    )
    return lat_deg, lon_deg


def default_origin() -> tuple[float, float]:
    """Return default geo origin (lat, lon) as tuple."""
    return _DEFAULT_GEO_ORIGIN