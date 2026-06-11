from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass, field
from datetime import datetime
from types import MappingProxyType
from typing import Any


def _freeze_json(value: Any) -> Any:
    if isinstance(value, Mapping):
        return MappingProxyType({str(k): _freeze_json(v) for k, v in value.items()})
    if isinstance(value, list):
        return tuple(_freeze_json(v) for v in value)
    return value


@dataclass(frozen=True)
class BBox:
    lat_min: float
    lat_max: float
    lon_min: float
    lon_max: float


@dataclass(frozen=True)
class RoutePoint:
    lat: float
    lon: float
    target_sog_kn: float


@dataclass(frozen=True)
class CanonicalAISRecord:
    provider: str
    received_at_utc: datetime
    ais_time_utc: datetime
    mmsi: int
    lat: float
    lon: float
    sog_kn: float | None
    cog_deg: float | None
    heading_deg: float | None
    nav_status: str | None
    ship_name: str | None
    ship_type: str | None
    raw_message_type: str
    raw_json: Mapping[str, Any] = field(compare=False, hash=False)
    quality_flags: frozenset[str]

    def __post_init__(self) -> None:
        object.__setattr__(self, "raw_json", _freeze_json(self.raw_json))


@dataclass(frozen=True)
class TrackPoint:
    t_s: float
    mmsi: int
    lat: float
    lon: float
    sog_kn: float
    cog_deg: float
    heading_deg: float | None


@dataclass(frozen=True)
class TrackSegment:
    mmsi: int
    points: tuple[TrackPoint, ...]


@dataclass(frozen=True)
class RankedTarget:
    mmsi: int
    point: TrackPoint
    score: float
    cpa_m: float
    tcpa_s: float
    distance_m: float
    rationale: str
