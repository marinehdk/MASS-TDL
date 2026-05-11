import numpy as np
from scenario_authoring.authoring.ais_source import TimedAISRecord
from scenario_authoring.pipeline.stage2_resample import split_into_segments


def _make_records(mmsi: int, timestamps: list[float], lat: float = 63.0, lon: float = 5.0):
    """Helper: create TimedAISRecords for given timestamps."""
    return [
        TimedAISRecord(
            mmsi=mmsi, lat=lat + i*0.001, lon=lon + i*0.001,
            sog_kn=10.0, cog_deg=float(i*10 % 360), heading_deg=float(i*10 % 360),
            ship_type=70, timestamp=ts,
        )
        for i, ts in enumerate(timestamps)
    ]


def test_split_at_gap_creates_two_segments():
    """Gap >300s splits into 2 segments, each >= 120s."""
    base = 1716150000.0
    # 10 records 30s apart = 270s continuous, then 400s gap, then 10 more records
    t1 = [base + i*30 for i in range(10)]        # 270s segment
    t2 = [base + 700 + i*30 for i in range(10)]  # 270s segment (gap=400s)
    records = _make_records(111111111, t1 + t2)

    segments = split_into_segments(records)
    assert len(segments) == 2
    assert segments[0].t_start == t1[0]
    assert segments[0].t_end == t1[-1]


def test_short_segment_discarded():
    """Segment <120s is discarded."""
    base = 1716150000.0
    t_short = [base + i*30 for i in range(3)]     # 60s — too short
    records = _make_records(111111111, t_short)

    segments = split_into_segments(records)
    assert len(segments) == 0


def test_resample_output_is_uniform_dt():
    """Resampled segment has dt=0.5s."""
    base = 1716150000.0
    t1 = [base + i*30 for i in range(10)]  # 270s
    records = _make_records(111111111, t1)

    segments = split_into_segments(records)
    assert len(segments) == 1
    dt_vals = np.diff(segments[0].t)
    assert np.allclose(dt_vals, 0.5, atol=0.001)


def test_cog_unwrap_across_360():
    """COG crossing 0°/360° does not produce interpolation spike."""
    base = 1716150000.0
    recs = [
        TimedAISRecord(mmsi=111111111, lat=63.0, lon=5.0, sog_kn=10.0, cog_deg=350.0, heading_deg=350.0, ship_type=70, timestamp=base),
        TimedAISRecord(mmsi=111111111, lat=63.001, lon=5.001, sog_kn=10.0, cog_deg=10.0, heading_deg=10.0, ship_type=70, timestamp=base+60),
        TimedAISRecord(mmsi=111111111, lat=63.002, lon=5.002, sog_kn=10.0, cog_deg=20.0, heading_deg=20.0, ship_type=70, timestamp=base+120),
        TimedAISRecord(mmsi=111111111, lat=63.003, lon=5.003, sog_kn=10.0, cog_deg=350.0, heading_deg=350.0, ship_type=70, timestamp=base+180),
        TimedAISRecord(mmsi=111111111, lat=63.004, lon=5.004, sog_kn=10.0, cog_deg=10.0, heading_deg=10.0, ship_type=70, timestamp=base+240),
    ]
    segments = split_into_segments(recs)
    assert len(segments) == 1
    # All resampled COG values should be in [0, 360)
    assert np.all(segments[0].cog_deg >= 0)
    assert np.all(segments[0].cog_deg < 360)
    # No spike: shortest-path angular diff should be < 180° (no slerp jump)
    raw_diff = np.diff(segments[0].cog_deg)
    angle_diff = np.minimum(np.abs(raw_diff), 360 - np.abs(raw_diff))
    assert np.all(angle_diff < 180.0)
