from __future__ import annotations

from dataclasses import dataclass
import math


M_PER_DEG_LAT = 111120.0


@dataclass(frozen=True)
class VesselKinematics:
    lat: float
    lon: float
    heading_deg: float
    sog_mps: float


@dataclass(frozen=True)
class CpaTcpa:
    dcpa_m: float
    tcpa_s: float
    range_m: float


def wrap_deg(angle: float) -> float:
    return angle % 360.0


def signed_delta_deg(target_deg: float, reference_deg: float) -> float:
    return (target_deg - reference_deg + 540.0) % 360.0 - 180.0


def _enu_delta_m(
    origin_lat: float, origin_lon: float, lat: float, lon: float
) -> tuple[float, float]:
    north = (lat - origin_lat) * M_PER_DEG_LAT
    east = (lon - origin_lon) * M_PER_DEG_LAT * math.cos(math.radians(origin_lat))
    return east, north


def _velocity_enu(v: VesselKinematics) -> tuple[float, float]:
    heading = math.radians(v.heading_deg)
    return v.sog_mps * math.sin(heading), v.sog_mps * math.cos(heading)


def _bearing_from_enu(east_m: float, north_m: float) -> float:
    return wrap_deg(math.degrees(math.atan2(east_m, north_m)))


def relative_bearing_deg(own: VesselKinematics, target: VesselKinematics) -> float:
    east, north = _enu_delta_m(own.lat, own.lon, target.lat, target.lon)
    bearing = _bearing_from_enu(east, north)
    return signed_delta_deg(bearing, own.heading_deg)


def aspect_from_target_deg(target: VesselKinematics, own: VesselKinematics) -> float:
    east, north = _enu_delta_m(target.lat, target.lon, own.lat, own.lon)
    bearing = _bearing_from_enu(east, north)
    return signed_delta_deg(bearing, target.heading_deg)


def compute_cpa_tcpa(own: VesselKinematics, target: VesselKinematics) -> CpaTcpa:
    rel_e, rel_n = _enu_delta_m(own.lat, own.lon, target.lat, target.lon)
    own_ve, own_vn = _velocity_enu(own)
    tgt_ve, tgt_vn = _velocity_enu(target)
    rel_ve = tgt_ve - own_ve
    rel_vn = tgt_vn - own_vn
    rel_speed_sq = rel_ve * rel_ve + rel_vn * rel_vn
    range_m = math.hypot(rel_e, rel_n)
    if rel_speed_sq <= 1e-9:
        return CpaTcpa(dcpa_m=range_m, tcpa_s=0.0, range_m=range_m)
    tcpa = -((rel_e * rel_ve) + (rel_n * rel_vn)) / rel_speed_sq
    cpa_e = rel_e + rel_ve * tcpa
    cpa_n = rel_n + rel_vn * tcpa
    return CpaTcpa(
        dcpa_m=math.hypot(cpa_e, cpa_n), tcpa_s=tcpa, range_m=range_m
    )


def apply_rot_limit(
    current_heading_deg: float,
    desired_heading_deg: float,
    rot_limit_deg_s: float,
    dt_s: float,
) -> tuple[float, float]:
    max_step = abs(rot_limit_deg_s) * max(dt_s, 0.0)
    delta = signed_delta_deg(desired_heading_deg, current_heading_deg)
    if abs(delta) <= max_step:
        step = delta
    else:
        step = math.copysign(max_step, delta)
    new_heading = wrap_deg(current_heading_deg + step)
    rot_deg_s = 0.0 if dt_s <= 0.0 else step / dt_s
    return new_heading, rot_deg_s
