"""Encounter geometry generator (D1.8).

Pure functions — NO ROS imports. Given own-ship state + a COLREGs rule,
produce a target spawn (position + course + speed) on a CBDR collision
construction, so the avoidance stack is forced to act. Reusable by both
the REST endpoint and headless test-bed scripts.
"""
from __future__ import annotations

import math
from dataclasses import dataclass

NM_M = 1852.0
KN_MS = 0.514444
M_PER_DEG_LAT = 111120.0


@dataclass(frozen=True)
class OwnState:
    lat: float
    lon: float
    heading_deg: float  # true, 0=N, CW (use COG)
    sog_kn: float


@dataclass(frozen=True)
class TargetSpawn:
    lat: float
    lon: float
    course_deg: float
    sog_kn: float


# rule → (b_rel deg [+=starboard], target speed factor, default range NM,
#         default construct CPA m, mode)
_RULE_DEFAULTS: dict[str, dict] = {
    "head_on":          dict(b_rel=0.0,   speed_factor=1.0, range_nm=1.5,
                             construct_cpa_m=0.0,   mode="intercept"),
    "crossing_giveway": dict(b_rel=45.0,  speed_factor=1.0, range_nm=2.0,
                             construct_cpa_m=300.0, mode="intercept"),
    "crossing_standon": dict(b_rel=-45.0, speed_factor=1.0, range_nm=2.0,
                             construct_cpa_m=300.0, mode="intercept"),
    "overtaking":       dict(b_rel=0.0,   speed_factor=0.5, range_nm=1.0,
                             construct_cpa_m=200.0, mode="overtake"),
}


def _enu_from_brg(range_m: float, brg_deg: float) -> tuple[float, float]:
    a = math.radians(brg_deg)
    return range_m * math.sin(a), range_m * math.cos(a)  # east, north


def _course_deg(east: float, north: float) -> float:
    return (math.degrees(math.atan2(east, north)) + 360.0) % 360.0


def _offset_to_latlon(lat0, lon0, east_m, north_m):
    dlat = north_m / M_PER_DEG_LAT
    dlon = east_m / (M_PER_DEG_LAT * math.cos(math.radians(lat0)))
    return lat0 + dlat, lon0 + dlon


def generate_encounter(rule, own, range_nm=None, construct_cpa_m=None,
                       approach_angle_deg=None):
    if rule not in _RULE_DEFAULTS:
        raise ValueError(f"unknown rule: {rule!r}")
    d = _RULE_DEFAULTS[rule]
    R = (range_nm if range_nm is not None else d["range_nm"]) * NM_M
    cpa = construct_cpa_m if construct_cpa_m is not None else d["construct_cpa_m"]
    b_rel = approach_angle_deg if approach_angle_deg is not None else d["b_rel"]

    psi = math.radians(own.heading_deg)
    v0 = own.sog_kn * KN_MS
    voe, von = v0 * math.sin(psi), v0 * math.cos(psi)          # own velocity ENU
    se, sn = _enu_from_brg(R, own.heading_deg + b_rel)          # spawn rel ENU
    st = d["speed_factor"] * v0

    if d["mode"] == "overtake":
        vte, vtn = st * math.sin(psi), st * math.cos(psi)      # same course, slower
    else:  # intercept: fixed target speed, solve course so they collide (DCPA=0)
        a = se * se + sn * sn
        vo_dot_s = voe * se + von * sn
        c = (voe * voe + von * von) - st * st
        disc = vo_dot_s * vo_dot_s - a * c
        if a <= 0.0 or disc < 0.0:
            raise ValueError("no intercept solution for given geometry")
        x = (vo_dot_s + math.sqrt(disc)) / a                   # x = 1/T, take T>0 (smallest T)
        if x <= 0.0:
            raise ValueError("no positive intercept time")
        vte, vtn = voe - se * x, von - sn * x

    # impose construct CPA: shift spawn ⊥ to relative velocity by `cpa`
    vrele, vreln = vte - voe, vtn - von
    vrel_mag = math.hypot(vrele, vreln)
    if vrel_mag > 1e-6 and cpa != 0.0:
        pe, pn = -vreln / vrel_mag, vrele / vrel_mag           # unit ⊥ to v_rel
        se, sn = se + cpa * pe, sn + cpa * pn

    lat, lon = _offset_to_latlon(own.lat, own.lon, se, sn)
    return TargetSpawn(lat=lat, lon=lon,
                       course_deg=_course_deg(vte, vtn),
                       sog_kn=math.hypot(vte, vtn) / KN_MS)
