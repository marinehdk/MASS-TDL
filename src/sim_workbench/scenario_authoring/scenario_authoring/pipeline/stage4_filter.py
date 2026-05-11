"""Stage 4: DCPA/TCPA filtering and COLREGs geometric classification."""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from scenario_authoring.authoring.ais_source import latlon_to_ne
from scenario_authoring.pipeline.stage2_resample import TrajectorySegment

DCPA_THRESHOLD_M = 500.0
TCPA_THRESHOLD_S = 1200.0  # 20 minutes


@dataclass
class Encounter:
    """A single COLREGs encounter extracted from AIS data."""
    own_mmsi: int
    target_mmsi: int
    encounter_type: str
    min_dcpa_m: float
    min_tcpa_s: float
    t_encounter_start: float
    t_encounter_end: float
    own_trajectory: np.ndarray   # (N, 5): t, lat, lon, sog, cog
    target_trajectory: np.ndarray


def compute_dcpa_tcpa_relative_motion(
    os_lat: np.ndarray, os_lon: np.ndarray,
    os_sog: np.ndarray, os_cog: np.ndarray,
    ts_lat: np.ndarray, ts_lon: np.ndarray,
    ts_sog: np.ndarray, ts_cog: np.ndarray,
) -> tuple[float, float]:
    """Compute min DCPA (meters) and TCPA at min DCPA (seconds).

    Uses relative motion method:
      v_r = v_ts - v_os,  p_r = p_ts - p_os
      TCPA = -(p_r·v_r) / |v_r|²
      DCPA = |p_r + TCPA * v_r|
    """
    os_n, os_e = latlon_to_ne(os_lat, os_lon)
    ts_n, ts_e = latlon_to_ne(ts_lat, ts_lon)

    os_vn = os_sog * np.cos(np.radians(os_cog))
    os_ve = os_sog * np.sin(np.radians(os_cog))
    ts_vn = ts_sog * np.cos(np.radians(ts_cog))
    ts_ve = ts_sog * np.sin(np.radians(ts_cog))

    p_n = ts_n - os_n
    p_e = ts_e - os_e
    v_n = ts_vn - os_vn
    v_e = ts_ve - os_ve

    dot_pv = p_n * v_n + p_e * v_e
    v_sq = v_n**2 + v_e**2

    tcpa = np.full_like(dot_pv, np.inf)
    mask = v_sq > 1e-6
    tcpa[mask] = -dot_pv[mask] / v_sq[mask]

    dcpa_n = p_n + tcpa * v_n
    dcpa_e = p_e + tcpa * v_e
    dcpa = np.sqrt(dcpa_n**2 + dcpa_e**2)

    valid = (tcpa > 0) & (tcpa < TCPA_THRESHOLD_S)
    if not valid.any():
        return np.inf, np.inf

    min_idx = int(np.argmin(dcpa[valid]))
    valid_indices = np.where(valid)[0]
    return float(dcpa[valid_indices[min_idx]]), float(tcpa[valid_indices[min_idx]])


def classify_colreg_encounter(
    bearing_deg: float, target_heading_deg: float, own_heading_deg: float,
) -> str:
    """COLREGs geometric pre-classification (same logic as M2 §6.3.1).

    bearing_deg: bearing of target from own-ship (0°=North).
    """
    # OVERTAKING: target in own-ship stern sector
    if 112.5 <= bearing_deg <= 247.5:
        return "OVERTAKING"

    # HEAD-ON: bearing near bow + opposing courses
    heading_diff = abs(target_heading_deg - own_heading_deg) % 360.0
    if heading_diff > 180.0:
        heading_diff = 360.0 - heading_diff
    if (bearing_deg < 6.0 or bearing_deg > 354.0) and heading_diff > 170.0:
        return "HEAD_ON"

    return "CROSSING"


def _compute_bearing(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    """Compute initial bearing from point 1 to point 2 (degrees, 0=North)."""
    from math import atan2, cos, degrees, radians, sin
    lat1_r, lat2_r = radians(lat1), radians(lat2)
    dlon = radians(lon2 - lon1)
    x = sin(dlon) * cos(lat2_r)
    y = cos(lat1_r) * sin(lat2_r) - sin(lat1_r) * cos(lat2_r) * cos(dlon)
    bearing = degrees(atan2(x, y))
    return (bearing + 360.0) % 360.0


def filter_encounters(
    own_segments: list[TrajectorySegment],
    target_segments: list[TrajectorySegment],
    dcpa_threshold_m: float = DCPA_THRESHOLD_M,
) -> list[Encounter]:
    """Find all qualifying COLREGs encounters between own and target segments.

    For each (own, target) segment pair with temporal overlap >= 60s:
      1. Align trajectories in overlapping window
      2. Compute min DCPA/TCPA
      3. Classify COLREGs encounter type if DCPA < threshold
    """
    encounters: list[Encounter] = []

    for os_seg in own_segments:
        for ts_seg in target_segments:
            if ts_seg.mmsi == os_seg.mmsi:
                continue

            t_start = max(os_seg.t_start, ts_seg.t_start)
            t_end = min(os_seg.t_end, ts_seg.t_end)
            if t_end - t_start < 60.0:
                continue

            os_mask = (os_seg.t >= t_start) & (os_seg.t <= t_end)
            ts_mask = (ts_seg.t >= t_start) & (ts_seg.t <= t_end)
            if os_mask.sum() < 10 or ts_mask.sum() < 10:
                continue

            min_dcpa, tcpa_at_min = compute_dcpa_tcpa_relative_motion(
                os_seg.lat[os_mask], os_seg.lon[os_mask],
                os_seg.sog_kn[os_mask], os_seg.cog_deg[os_mask],
                ts_seg.lat[ts_mask], ts_seg.lon[ts_mask],
                ts_seg.sog_kn[ts_mask], ts_seg.cog_deg[ts_mask],
            )

            if min_dcpa < dcpa_threshold_m and tcpa_at_min < TCPA_THRESHOLD_S:
                mean_bearing = _compute_bearing(
                    float(np.mean(os_seg.lat[os_mask])),
                    float(np.mean(os_seg.lon[os_mask])),
                    float(np.mean(ts_seg.lat[ts_mask])),
                    float(np.mean(ts_seg.lon[ts_mask])),
                )
                enc_type = classify_colreg_encounter(
                    mean_bearing,
                    float(np.mean(ts_seg.cog_deg[ts_mask])),
                    float(np.mean(os_seg.cog_deg[ts_mask])),
                )
                os_traj = np.column_stack([
                    os_seg.t[os_mask], os_seg.lat[os_mask], os_seg.lon[os_mask],
                    os_seg.sog_kn[os_mask], os_seg.cog_deg[os_mask],
                ])
                ts_traj = np.column_stack([
                    ts_seg.t[ts_mask], ts_seg.lat[ts_mask], ts_seg.lon[ts_mask],
                    ts_seg.sog_kn[ts_mask], ts_seg.cog_deg[ts_mask],
                ])
                encounters.append(Encounter(
                    own_mmsi=os_seg.mmsi, target_mmsi=ts_seg.mmsi,
                    encounter_type=enc_type,
                    min_dcpa_m=min_dcpa, min_tcpa_s=tcpa_at_min,
                    t_encounter_start=t_start, t_encounter_end=t_end,
                    own_trajectory=os_traj, target_trajectory=ts_traj,
                ))

    return encounters
