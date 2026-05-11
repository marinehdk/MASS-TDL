"""Stage 2: Gap-based segment splitting and uniform resampling to Δt=0.5s."""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from scenario_authoring.authoring.ais_source import TimedAISRecord

GAP_THRESHOLD_S = 300.0
MIN_SEGMENT_DURATION_S = 120.0
RESAMPLE_DT = 0.5


@dataclass
class TrajectorySegment:
    """Continuous vessel trajectory segment at uniform 0.5s resolution."""
    mmsi: int
    t_start: float
    t_end: float
    t: np.ndarray
    lat: np.ndarray
    lon: np.ndarray
    sog_kn: np.ndarray
    cog_deg: np.ndarray
    heading_deg: np.ndarray


def split_into_segments(
    records: list[TimedAISRecord],
    gap_threshold_s: float = GAP_THRESHOLD_S,
    min_duration_s: float = MIN_SEGMENT_DURATION_S,
) -> list[TrajectorySegment]:
    """Split vessel track into TrajectorySegments at temporal gaps >5 min.
    Discard segments <2 min. Each segment resampled to uniform Δt=0.5s.
    """
    if len(records) < 2:
        return []

    segments: list[TrajectorySegment] = []
    current: list[TimedAISRecord] = [records[0]]

    for i in range(1, len(records)):
        gap = records[i].timestamp - records[i - 1].timestamp
        if gap > gap_threshold_s:
            seg = _maybe_resample(current, min_duration_s)
            if seg is not None:
                segments.append(seg)
            current = [records[i]]
        else:
            current.append(records[i])

    seg = _maybe_resample(current, min_duration_s)
    if seg is not None:
        segments.append(seg)

    return segments


def _maybe_resample(
    records: list[TimedAISRecord], min_duration_s: float,
) -> TrajectorySegment | None:
    """Resample if segment duration >= min, otherwise return None."""
    duration = records[-1].timestamp - records[0].timestamp
    if duration < min_duration_s:
        return None
    return _resample_segment(records)


def _resample_segment(records: list[TimedAISRecord]) -> TrajectorySegment:
    """Resample to uniform Δt=0.5s with linear interpolation. COG unwrapped."""
    t_orig = np.array([r.timestamp for r in records])
    t_new = np.arange(t_orig[0], t_orig[-1] + RESAMPLE_DT, RESAMPLE_DT)

    lat_new = np.interp(t_new, t_orig, np.array([r.lat for r in records]))
    lon_new = np.interp(t_new, t_orig, np.array([r.lon for r in records]))
    sog_new = np.interp(t_new, t_orig, np.array([r.sog_kn for r in records]))

    cog_rad = np.radians(np.array([r.cog_deg for r in records]))
    cog_unwrapped = np.unwrap(cog_rad)
    cog_interp_rad = np.interp(t_new, t_orig, cog_unwrapped)
    cog_new = np.degrees(cog_interp_rad) % 360.0

    hdg_rad = np.radians(np.array([r.heading_deg for r in records]))
    hdg_unwrapped = np.unwrap(hdg_rad)
    hdg_interp_rad = np.interp(t_new, t_orig, hdg_unwrapped)
    hdg_new = np.degrees(hdg_interp_rad) % 360.0

    return TrajectorySegment(
        mmsi=records[0].mmsi,
        t_start=float(t_new[0]), t_end=float(t_new[-1]),
        t=t_new, lat=lat_new, lon=lon_new,
        sog_kn=sog_new, cog_deg=cog_new, heading_deg=hdg_new,
    )
