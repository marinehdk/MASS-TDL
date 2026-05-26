from __future__ import annotations

import math
from dataclasses import dataclass, field
from enum import Enum


class AvoidancePhase(Enum):
    STEADY = "steady"
    TURNING = "turning"
    CPA_CLEARED = "cpa_cleared"
    RETURNING = "returning"


@dataclass
class TargetState:
    lat: float
    lon: float
    heading_rad: float
    sog_ms: float
    mmsi: int = 0


@dataclass
class AvoidanceState:
    own_lat: float
    own_lon: float
    own_heading_rad: float
    own_sog_ms: float
    own_cog_rad: float
    original_heading_rad: float
    targets: list[TargetState] = field(default_factory=list)
    phase: AvoidancePhase = AvoidancePhase.STEADY
    heading_offset_rad: float = 0.0
    rot_rad_s: float = 0.0
    sim_time: float = 0.0


_NM_TO_M = 1852.0
_M_TO_NM = 1.0 / _NM_TO_M
_DEG_TO_RAD = math.pi / 180.0
_RAD_TO_DEG = 180.0 / math.pi
_TURN_RATE_RAD_S = 3.0 * _DEG_TO_RAD / 60.0
_MAX_OFFSET_RAD = 35.0 * _DEG_TO_RAD
_RETURN_RATE_FACTOR = 0.5
_TCPA_THRESHOLD_S = 300.0
_DIST_THRESHOLD_NM = 4.0
_DCPA_CLEAR_NM = 0.5
_TCPA_CLEAR_S = 10.0


def _haversine_nm(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    rlat1 = math.radians(lat1)
    rlat2 = math.radians(lat2)
    dlat = rlat2 - rlat1
    dlon = math.radians(lon2 - lon1)
    a = math.sin(dlat / 2) ** 2 + math.cos(rlat1) * math.cos(rlat2) * math.sin(dlon / 2) ** 2
    c = 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))
    return 3440.065 * c


def _bearing_rad(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    rlat1 = math.radians(lat1)
    rlat2 = math.radians(lat2)
    dlon = math.radians(lon2 - lon1)
    x = math.sin(dlon) * math.cos(rlat2)
    y = math.cos(rlat1) * math.sin(rlat2) - math.sin(rlat1) * math.cos(rlat2) * math.cos(dlon)
    return math.atan2(x, y) % (2 * math.pi)


def _tcpa_s(
    own_lat: float,
    own_lon: float,
    own_hdg_rad: float,
    own_sog_ms: float,
    tgt_lat: float,
    tgt_lon: float,
    tgt_hdg_rad: float,
    tgt_sog_ms: float,
) -> float:
    own_vn = own_sog_ms * math.cos(own_hdg_rad)
    own_ve = own_sog_ms * math.sin(own_hdg_rad)
    tgt_vn = tgt_sog_ms * math.cos(tgt_hdg_rad)
    tgt_ve = tgt_sog_ms * math.sin(tgt_hdg_rad)
    dvn = tgt_vn - own_vn
    dve = tgt_ve - own_ve
    rel_v_sq = dvn * dvn + dve * dve
    if rel_v_sq < 1e-12:
        return 0.0
    dn_m = (tgt_lat - own_lat) * 111120.0
    de_m = (tgt_lon - own_lon) * 111120.0 * math.cos(math.radians(own_lat))
    tcpa = -(dn_m * dvn + de_m * dve) / rel_v_sq
    return max(tcpa, 0.0)


def _dcpa_nm(
    own_lat: float,
    own_lon: float,
    own_hdg_rad: float,
    own_sog_ms: float,
    tgt_lat: float,
    tgt_lon: float,
    tgt_hdg_rad: float,
    tgt_sog_ms: float,
) -> float:
    tcpa = _tcpa_s(own_lat, own_lon, own_hdg_rad, own_sog_ms, tgt_lat, tgt_lon, tgt_hdg_rad, tgt_sog_ms)
    own_vn = own_sog_ms * math.cos(own_hdg_rad)
    own_ve = own_sog_ms * math.sin(own_hdg_rad)
    tgt_vn = tgt_sog_ms * math.cos(tgt_hdg_rad)
    tgt_ve = tgt_sog_ms * math.sin(tgt_hdg_rad)
    dn_m = (tgt_lat - own_lat) * 111120.0
    de_m = (tgt_lon - own_lon) * 111120.0 * math.cos(math.radians(own_lat))
    dvn = tgt_vn - own_vn
    dve = tgt_ve - own_ve
    cpa_dn = dn_m + dvn * tcpa
    cpa_de = de_m + dve * tcpa
    cpa_m = math.sqrt(cpa_dn * cpa_dn + cpa_de * cpa_de)
    return cpa_m * _M_TO_NM


def _advance_pos(lat: float, lon: float, hdg_rad: float, sog_ms: float, dt: float) -> tuple[float, float]:
    lat_rad = math.radians(lat)
    dlat = sog_ms * math.cos(hdg_rad) * dt / 111120.0
    dlon = sog_ms * math.sin(hdg_rad) * dt / (111120.0 * math.cos(lat_rad))
    return lat + dlat, lon + dlon


def step_demo_avoidance(state: AvoidanceState, dt: float) -> None:
    min_tcpa = float("inf")
    min_dcpa = float("inf")
    for tgt in state.targets:
        dist = _haversine_nm(state.own_lat, state.own_lon, tgt.lat, tgt.lon)
        if dist > _DIST_THRESHOLD_NM * 2:
            continue
        tcpa = _tcpa_s(state.own_lat, state.own_lon, state.own_heading_rad, state.own_sog_ms, tgt.lat, tgt.lon, tgt.heading_rad, tgt.sog_ms)
        dcpa = _dcpa_nm(state.own_lat, state.own_lon, state.own_heading_rad, state.own_sog_ms, tgt.lat, tgt.lon, tgt.heading_rad, tgt.sog_ms)
        if tcpa < min_tcpa:
            min_tcpa = tcpa
        if dcpa < min_dcpa:
            min_dcpa = dcpa

    if state.phase == AvoidancePhase.STEADY:
        if min_tcpa < _TCPA_THRESHOLD_S and min_dcpa < _DIST_THRESHOLD_NM:
            state.phase = AvoidancePhase.TURNING

    elif state.phase == AvoidancePhase.TURNING:
        if state.heading_offset_rad < _MAX_OFFSET_RAD:
            delta = _TURN_RATE_RAD_S * dt
            state.heading_offset_rad = min(state.heading_offset_rad + delta, _MAX_OFFSET_RAD)
        if min_dcpa > _DCPA_CLEAR_NM and min_tcpa < _TCPA_CLEAR_S:
            state.phase = AvoidancePhase.CPA_CLEARED

    elif state.phase == AvoidancePhase.CPA_CLEARED:
        state.phase = AvoidancePhase.RETURNING

    elif state.phase == AvoidancePhase.RETURNING:
        if state.heading_offset_rad > 1e-6:
            delta = _TURN_RATE_RAD_S * _RETURN_RATE_FACTOR * dt
            state.heading_offset_rad = max(state.heading_offset_rad - delta, 0.0)
        else:
            state.heading_offset_rad = 0.0
            state.phase = AvoidancePhase.STEADY

    if state.phase == AvoidancePhase.TURNING:
        state.rot_rad_s = _TURN_RATE_RAD_S
    elif state.phase == AvoidancePhase.RETURNING:
        state.rot_rad_s = -_TURN_RATE_RAD_S * _RETURN_RATE_FACTOR
    else:
        state.rot_rad_s = 0.0

    state.own_heading_rad = state.original_heading_rad + state.heading_offset_rad
    state.own_cog_rad = state.own_heading_rad

    state.own_lat, state.own_lon = _advance_pos(
        state.own_lat, state.own_lon, state.own_heading_rad, state.own_sog_ms, dt
    )

    for tgt in state.targets:
        tgt.lat, tgt.lon = _advance_pos(tgt.lat, tgt.lon, tgt.heading_rad, tgt.sog_ms, dt)

    state.sim_time += dt
