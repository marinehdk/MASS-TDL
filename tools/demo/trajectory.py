import math

# ── Constants ──────────────────────────────────────────────────────────────────
DT = 0.1             # seconds per frame (10 Hz)
DURATION_S = 700.0
N_FRAMES = int(DURATION_S / DT)  # 7000

SOG_KN = 10.0
SOG_MS = SOG_KN * 1852.0 / 3600.0  # 5.1444 m/s

LAT_DEG_PER_M = 1.0 / 111_320.0
# lon deg per meter at 63°N:
LON_DEG_PER_M = 1.0 / (111_320.0 * math.cos(math.radians(63.0)))

# Initial positions (from imazu-01-ho-v1.0.yaml)
OS_LAT0, OS_LON0, OS_HDG0_DEG = 63.0, 5.0, 0.0
TS_LAT0, TS_LON0, TS_HDG0_DEG = 63.117451, 5.0, 180.0

# Avoidance parameters (from yaml: avoidance_time_s=300, avoidance_delta_rad=0.6109, avoidance_duration_s=90)
AVOID_START_S  = 300.0
AVOID_DELTA_DEG = math.degrees(0.6109)  # ≈ 35.0°
AVOID_DUR_S    = 90.0
AVOID_RATE_DPS = AVOID_DELTA_DEG / AVOID_DUR_S   # deg/s during turn

HOLD_DUR_S   = 60.0   # hold at max turn for 60s
RETURN_START_S = AVOID_START_S + AVOID_DUR_S + HOLD_DUR_S  # T=450s
RETURN_DUR_S   = 90.0

# ASDR events broadcast by WS server at these sim-time seconds
ASDR_EVENTS = [
    {'t':   0.0, 'event_type': 'scenario_start',  'severity': 'INFO',
     'decision': 'SCENARIO_START',         'rationale': 'Imazu-01 HO · Rule 14'},
    {'t': 120.0, 'event_type': 'target_detected',  'severity': 'INFO',
     'decision': 'TARGET_DETECTED',        'rationale': 'T01 range 7.0nm · bearing 000°'},
    {'t': 148.0, 'event_type': 'risk_assessed',   'severity': 'WARN',
     'decision': 'COLLISION_RISK',         'rationale': 'DCPA 0.08nm · TCPA 3.5min'},
    {'t': 150.0, 'event_type': 'avoidance_start',  'severity': 'ACTION',
     'decision': 'COLREG_R14_EXECUTE',     'rationale': 'Turn STBD +35° per Rule 14 Give-Way'},
    {'t': 355.0, 'event_type': 'sim_complete',     'severity': 'INFO',
     'decision': 'SIMULATION_END',         'rationale': 'Run complete · scoring sealed'},
]


def _os_heading_deg(t: float) -> float:
    """Return OS heading in degrees at sim-time t."""
    if t < AVOID_START_S:
        return OS_HDG0_DEG
    elif t < AVOID_START_S + AVOID_DUR_S:
        return OS_HDG0_DEG + AVOID_RATE_DPS * (t - AVOID_START_S)
    elif t < RETURN_START_S:
        return OS_HDG0_DEG + AVOID_DELTA_DEG
    elif t < RETURN_START_S + RETURN_DUR_S:
        progress = (t - RETURN_START_S) / RETURN_DUR_S
        return OS_HDG0_DEG + AVOID_DELTA_DEG * (1.0 - progress)
    else:
        return OS_HDG0_DEG


def compute_frames() -> list:
    """Pre-compute all 7000 frames. Returns list of frame dicts."""
    frames = []
    os_lat, os_lon = OS_LAT0, OS_LON0
    ts_lat, ts_lon = TS_LAT0, TS_LON0
    ts_hdg_rad = math.radians(TS_HDG0_DEG)
    prev_hdg = OS_HDG0_DEG

    for i in range(N_FRAMES):
        t = i * DT
        os_hdg_deg = _os_heading_deg(t)
        rot_dps = (os_hdg_deg - prev_hdg) / DT
        prev_hdg = os_hdg_deg
        os_hdg_rad = math.radians(os_hdg_deg)

        frames.append({
            't': round(t, 1),
            'os': {
                'lat': os_lat,
                'lon': os_lon,
                'hdg_rad': os_hdg_rad,
                'cog_rad': os_hdg_rad,
                'sog_ms': SOG_MS,
                'rot_rps': math.radians(rot_dps),
            },
            'ts': {
                'lat': ts_lat,
                'lon': ts_lon,
                'hdg_rad': ts_hdg_rad,
                'cog_rad': ts_hdg_rad,
                'sog_ms': SOG_MS,
                'rot_rps': 0.0,
            },
        })

        # Euler integration for next frame
        os_lat += SOG_MS * math.cos(os_hdg_rad) * DT * LAT_DEG_PER_M
        os_lon += SOG_MS * math.sin(os_hdg_rad) * DT * LON_DEG_PER_M
        ts_lat += SOG_MS * math.cos(ts_hdg_rad) * DT * LAT_DEG_PER_M
        ts_lon += SOG_MS * math.sin(ts_hdg_rad) * DT * LON_DEG_PER_M

    return frames


def _dist_nm(f: dict) -> float:
    """Great-circle approximation distance between OS and TS in nautical miles."""
    dlat_m = (f['os']['lat'] - f['ts']['lat']) * 111_320.0
    dlon_m = (f['os']['lon'] - f['ts']['lon']) * 111_320.0 * math.cos(math.radians(63.0))
    return math.hypot(dlat_m, dlon_m) / 1852.0


def compute_cpa(frames: list | None = None) -> float:
    """Return minimum distance (nm) observed across all frames.

    If frames are not provided, compute_frames() is called (expensive — cache externally).
    """
    if frames is None:
        frames = compute_frames()
    return min(_dist_nm(f) for f in frames)