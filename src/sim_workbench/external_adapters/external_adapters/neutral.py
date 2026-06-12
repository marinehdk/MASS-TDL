from dataclasses import dataclass


@dataclass(frozen=True)
class NeutralTarget:
    target_id: int
    lat: float
    lon: float
    sog_kn: float
    cog_deg: float
    heading_deg: float
    source_sensor: str
    confidence: float


@dataclass(frozen=True)
class NeutralOwnship:
    stamp_sec: int
    stamp_nanosec: int
    lat: float
    lon: float
    sog_kn: float
    cog_deg: float
    heading_deg: float
    u_water: float
    v_water: float
    r_dot_deg_s: float
    current_speed_kn: float
    current_direction_deg: float
    confidence: float
    nav_mode: str


@dataclass(frozen=True)
class NeutralEnvironment:
    stamp_sec: int
    stamp_nanosec: int
    wind_speed_kn: float
    wind_direction_deg: float
    current_speed_kn: float
    current_direction_deg: float
    visibility_range_nm: float
    confidence: float


@dataclass(frozen=True)
class NeutralRoutePoint:
    lat: float
    lon: float
    speed_kn: float
