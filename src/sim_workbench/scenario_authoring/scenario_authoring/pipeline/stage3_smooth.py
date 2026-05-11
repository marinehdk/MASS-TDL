"""Stage 3: Savitzky-Golay smoothing of SOG and COG."""
from __future__ import annotations

import numpy as np
from scipy.signal import savgol_filter

from scenario_authoring.pipeline.stage2_resample import TrajectorySegment

SG_WINDOW = 21
SG_ORDER = 3


def smooth_segment(seg: TrajectorySegment) -> TrajectorySegment:
    """Apply Savitzky-Golay filter to SOG and COG. Position is pass-through."""
    n = len(seg.t)
    window = min(SG_WINDOW, n - (n % 2 == 0))  # must be odd, <= n
    if window < 3:
        return seg  # too short to smooth

    sog_smoothed = savgol_filter(seg.sog_kn, window, SG_ORDER)

    cog_rad = np.radians(seg.cog_deg)
    cog_unwrapped = np.unwrap(cog_rad)
    cog_smoothed_rad = savgol_filter(cog_unwrapped, window, SG_ORDER)
    cog_smoothed = np.degrees(cog_smoothed_rad) % 360.0

    return TrajectorySegment(
        mmsi=seg.mmsi, t_start=seg.t_start, t_end=seg.t_end,
        t=seg.t, lat=seg.lat, lon=seg.lon,
        sog_kn=sog_smoothed, cog_deg=cog_smoothed, heading_deg=seg.heading_deg,
    )
