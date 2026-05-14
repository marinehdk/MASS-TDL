"""Head-On (Rule 14) analytical trajectory generator.

Pre-computes OS and TS trajectories for the Imazu-01 scenario:
- OS and TS on reciprocal courses (head-on)
- OS turns starboard 35 deg at T=300s to avoid
- Holds 60s, then returns to original heading T=450-540s
- Duration 700s at 10 Hz = 7000 frames
"""

import math
from typing import List, Dict, Optional

# ── Timing constants ──
DT = 0.1               # 10 Hz
DURATION_S = 700.0
N_FRAMES = 7000

# ── Speed constants ──
SOG_KN = 10.0
SOG_MS = SOG_KN * 1852.0 / 3600.0  # ≈ 5.1444 m/s

# ── Geographic constants ──
LAT_DEG_PER_M = 1.0 / 111320.0
LON_DEG_PER_M = 1.0 / (111320.0 * math.cos(math.radians(63.0)))

# ── Initial positions ──
OS_LAT0 = 63.0
OS_LON0 = 5.0
TS_LAT0 = 63.117451
TS_LON0 = 5.0

# ── Headings (radians) ──
OS_HDG0_RAD = math.radians(0.0)      # North
TS_HDG0_RAD = math.radians(180.0)    # South (π)

# ── Avoidance manoeuvre parameters ──
AVOID_START_S = 300.0
AVOID_DELTA_DEG = 35.0
AVOID_DURATION_S = 90.0    # time to reach delta
AVOID_HOLD_S = 60.0
AVOID_RETURN_START_S = 450.0
AVOID_RETURN_DURATION_S = 90.0
AVOID_RETURN_END_S = AVOID_RETURN_START_S + AVOID_RETURN_DURATION_S  # 540s

# ── ASDR events ──
ASDR_EVENTS: List[Dict] = [
    {
        't': 280.0,
        'event_type': 'COLREGS_RULE_MATCH',
        'severity': 'INFO',
        'decision': 'RULE_14_HEAD_ON',
        'rationale': 'TS on reciprocal course within 22.5 deg; Rule 14 applies',
    },
    {
        't': 290.0,
        'event_type': 'COLLISION_RISK_ASSESSMENT',
        'severity': 'WARNING',
        'decision': 'CPA_DECREASING',
        'rationale': 'CPA decreasing below 2 nm; risk assess triggered',
    },
    {
        't': 300.0,
        'event_type': 'AVOIDANCE_ACTION',
        'severity': 'ACTION',
        'decision': 'STARBOARD_35',
        'rationale': 'Rule 14: alter course to starboard; delta=35 deg',
    },
    {
        't': 300.0,
        'event_type': 'ROT_COMMAND',
        'severity': 'INFO',
        'decision': 'ROT_POSITIVE',
        'rationale': 'ROT set positive (starboard turn) at 35/90 deg/s',
    },
    {
        't': 390.0,
        'event_type': 'HOLD_COURSE',
        'severity': 'INFO',
        'decision': 'HOLD_STARBOARD_35',
        'rationale': 'Holding altered course 35 deg starboard for 60s',
    },
    {
        't': 450.0,
        'event_type': 'AVOIDANCE_ACTION',
        'severity': 'ACTION',
        'decision': 'RETURN_ORIGINAL',
        'rationale': 'Returning to original heading over 90s',
    },
    {
        't': 540.0,
        'event_type': 'MANOEUVRE_COMPLETE',
        'severity': 'INFO',
        'decision': 'COURSE_RESUMED',
        'rationale': 'Returned to original heading; Rule 14 manoeuvre complete',
    },
]


def _os_heading_deg(t: float) -> float:
    """OS heading in degrees at time t."""
    if t < AVOID_START_S:
        return 0.0
    elif t < AVOID_START_S + AVOID_DURATION_S:
        # Linear ramp 0 -> 35 deg over 90s
        frac = (t - AVOID_START_S) / AVOID_DURATION_S
        return AVOID_DELTA_DEG * frac
    elif t < AVOID_RETURN_START_S:
        # Hold at 35 deg
        return AVOID_DELTA_DEG
    elif t < AVOID_RETURN_END_S:
        # Linear ramp 35 -> 0 deg over 90s
        frac = (t - AVOID_RETURN_START_S) / AVOID_RETURN_DURATION_S
        return AVOID_DELTA_DEG * (1.0 - frac)
    else:
        return 0.0


def _os_rot_rps(t: float) -> float:
    """OS rate of turn in radians per second."""
    if t < AVOID_START_S:
        return 0.0
    elif t < AVOID_START_S + AVOID_DURATION_S:
        # rate = 35/90 deg/s converted to rad/s
        return math.radians(AVOID_DELTA_DEG / AVOID_DURATION_S)
    elif t < AVOID_RETURN_START_S:
        return 0.0
    elif t < AVOID_RETURN_END_S:
        # rate = -35/90 deg/s converted to rad/s
        return -math.radians(AVOID_DELTA_DEG / AVOID_DURATION_S)
    else:
        return 0.0


def compute_frames() -> List[Dict]:
    """Compute all 7000 trajectory frames.

    Returns a list of 7000 frame dicts, each containing:
      t, os{lat, lon, hdg_rad, cog_rad, sog_ms, rot_rps},
         ts{lat, lon, hdg_rad, cog_rad, sog_ms, rot_rps}
    """
    frames: List[Dict] = []

    # OS state
    os_lat = OS_LAT0
    os_lon = OS_LON0

    # TS state — constant heading pi, same speed
    ts_lat = TS_LAT0
    ts_lon = TS_LON0
    ts_hdg_rad = TS_HDG0_RAD

    for i in range(N_FRAMES):
        t = i * DT

        # OS heading
        os_hdg_deg = _os_heading_deg(t)
        os_hdg_rad = math.radians(os_hdg_deg)
        os_cog_rad = os_hdg_rad  # COG = heading in calm water
        os_rot_rps = _os_rot_rps(t)

        # TS heading (constant)
        ts_cog_rad = ts_hdg_rad
        ts_rot_rps = 0.0

        # Record frame
        frames.append({
            't': t,
            'os': {
                'lat': os_lat,
                'lon': os_lon,
                'hdg_rad': os_hdg_rad,
                'cog_rad': os_cog_rad,
                'sog_ms': SOG_MS,
                'rot_rps': os_rot_rps,
            },
            'ts': {
                'lat': ts_lat,
                'lon': ts_lon,
                'hdg_rad': ts_hdg_rad,
                'cog_rad': ts_cog_rad,
                'sog_ms': SOG_MS,
                'rot_rps': ts_rot_rps,
            },
        })

        # Euler integration for next step
        os_lat += SOG_MS * math.cos(os_hdg_rad) * DT * LAT_DEG_PER_M
        os_lon += SOG_MS * math.sin(os_hdg_rad) * DT * LON_DEG_PER_M

        ts_lat += SOG_MS * math.cos(ts_hdg_rad) * DT * LAT_DEG_PER_M
        ts_lon += SOG_MS * math.sin(ts_hdg_rad) * DT * LON_DEG_PER_M

    return frames


def _haversine_nm(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    """Haversine distance in nautical miles between two (lat, lon) in degrees."""
    lat1_r, lon1_r = math.radians(lat1), math.radians(lon1)
    lat2_r, lon2_r = math.radians(lat2), math.radians(lon2)

    dlat = lat2_r - lat1_r
    dlon = lon2_r - lon1_r

    a = math.sin(dlat / 2.0) ** 2 + math.cos(lat1_r) * math.cos(lat2_r) * math.sin(dlon / 2.0) ** 2
    c = 2.0 * math.atan2(math.sqrt(a), math.sqrt(1.0 - a))

    # Earth radius in nm: 3440.065
    return c * 3440.065


def compute_cpa(frames: Optional[List[Dict]] = None) -> float:
    """Compute minimum CPA (Closest Point of Approach) in nautical miles.

    Returns the minimum distance between OS and TS across all frames.
    """
    if frames is None:
        frames = compute_frames()

    min_dist = float('inf')
    for f in frames:
        d = _haversine_nm(
            f['os']['lat'], f['os']['lon'],
            f['ts']['lat'], f['ts']['lon'],
        )
        if d < min_dist:
            min_dist = d

    return min_dist


if __name__ == '__main__':
    frames = compute_frames()
    print(f"Frames: {len(frames)}")
    print(f"First frame: t={frames[0]['t']}, OS=({frames[0]['os']['lat']:.6f}, {frames[0]['os']['lon']:.6f})")
    print(f"Last frame:  t={frames[-1]['t']}, OS=({frames[-1]['os']['lat']:.6f}, {frames[-1]['os']['lon']:.6f})")
    cpa = compute_cpa(frames)
    print(f"CPA: {cpa:.4f} nm")