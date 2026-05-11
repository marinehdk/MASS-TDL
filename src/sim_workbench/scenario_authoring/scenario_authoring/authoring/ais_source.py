"""AIS data source adapters and coordinate transforms."""
from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
import pandas as pd
import pyproj


@dataclass
class TimedAISRecord:
    """AIS position report with parsed timestamp."""
    mmsi: int
    lat: float
    lon: float
    sog_kn: float
    cog_deg: float
    heading_deg: float
    ship_type: int
    timestamp: float  # Unix epoch seconds


_WGS84 = "EPSG:4326"


def latlon_to_ne(
    lat: np.ndarray, lon: np.ndarray, utm_zone: int = 33
) -> tuple[np.ndarray, np.ndarray]:
    """WGS84 -> NE Cartesian (UTM projection)."""
    proj_str = f"+proj=utm +zone={utm_zone} +datum=WGS84 +units=m"
    transformer = pyproj.Transformer.from_crs(_WGS84, proj_str, always_xy=True)
    e, n = transformer.transform(lon, lat)
    return n, e


def ne_to_latlon(
    geo_origin_lat: float, geo_origin_lon: float,
    n_m: float, e_m: float, utm_zone: int = 33,
) -> tuple[float, float]:
    """NE Cartesian -> WGS84 (direct inverse UTM projection).

    The *geo_origin* arguments are reserved for future origin-relative usage;
    currently the transform operates on absolute UTM (n, e) values.
    """
    proj_str = f"+proj=utm +zone={utm_zone} +datum=WGS84 +units=m"
    transformer = pyproj.Transformer.from_crs(proj_str, _WGS84, always_xy=True)
    lon, lat = transformer.transform(e_m, n_m)
    return float(lat), float(lon)


def _parse_noaa_timestamp(dt_str: str) -> float:
    """Parse NOAA BaseDateTime to Unix epoch."""
    dt = datetime.fromisoformat(dt_str)
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    return dt.timestamp()


def load_ais_dataset(data_dir: Path) -> list[TimedAISRecord]:
    """Load all AIS data from a directory (CSV files)."""
    records: list[TimedAISRecord] = []
    for csv_file in sorted(data_dir.glob("*.csv")):
        df = pd.read_csv(csv_file)
        for _, row in df.iterrows():
            try:
                ts = _parse_noaa_timestamp(str(row.get("BaseDateTime", "")))
            except (ValueError, KeyError):
                try:
                    ts = float(row["timestamp"])
                except (ValueError, KeyError):
                    continue

            try:
                rec = TimedAISRecord(
                    mmsi=int(row["MMSI"]),
                    lat=float(row["LAT"]),
                    lon=float(row["LON"]),
                    sog_kn=float(row.get("SOG", 0) or 0),
                    cog_deg=float(row.get("COG", 0) or 0),
                    heading_deg=float(row.get("Heading", 511) or 0),
                    ship_type=int(float(row.get("VesselType", 0) or 0)),
                    timestamp=ts,
                )
            except (ValueError, KeyError):
                continue

            if rec.heading_deg >= 511.0:
                rec.heading_deg = rec.cog_deg
            records.append(rec)

    return records
