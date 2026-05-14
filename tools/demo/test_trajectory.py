import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from trajectory import compute_frames, compute_cpa, ASDR_EVENTS

def test_frame_count():
    frames = compute_frames()
    assert len(frames) == 7000  # 700s × 10Hz

def test_os_initial_position():
    frames = compute_frames()
    f0 = frames[0]
    assert abs(f0['os']['lat'] - 63.0) < 1e-6
    assert abs(f0['os']['lon'] - 5.0) < 1e-6

def test_ts_initial_position():
    frames = compute_frames()
    f0 = frames[0]
    assert abs(f0['ts']['lat'] - 63.117451) < 1e-6
    assert abs(f0['ts']['lon'] - 5.0) < 1e-6

def test_os_heading_during_avoidance():
    frames = compute_frames()
    # At T=345s (frame 3450), OS should be mid-turn (heading ~17.5°)
    f = frames[3450]
    hdg_deg = f['os']['hdg_rad'] * 180 / 3.14159265
    assert 10.0 < hdg_deg < 30.0, f"Expected mid-turn heading, got {hdg_deg}"

def test_os_heading_after_full_turn():
    frames = compute_frames()
    # At T=395s (frame 3950), OS should be at ~35°
    f = frames[3950]
    hdg_deg = f['os']['hdg_rad'] * 180 / 3.14159265
    assert abs(hdg_deg - 35.0) < 2.0, f"Expected ~35°, got {hdg_deg}"

def test_os_returns_to_original():
    frames = compute_frames()
    # At T=695s (frame 6950), OS should be back near 0°
    f = frames[6950]
    hdg_deg = f['os']['hdg_rad'] * 180 / 3.14159265
    assert abs(hdg_deg) < 2.0 or abs(hdg_deg - 360) < 2.0

def test_cpa_range():
    cpa_nm = compute_cpa()
    assert 2.5 < cpa_nm < 4.0, f"CPA {cpa_nm}nm out of expected range (analytical: ~3.24nm, OS turns east before crossing TS path)"

def test_asdr_events_ordered():
    times = [e['t'] for e in ASDR_EVENTS]
    assert times == sorted(times)

def test_asdr_events_count():
    assert len(ASDR_EVENTS) == 5, f"Expected 5 ASDR events per spec, got {len(ASDR_EVENTS)}"

def test_ts_heading_constant():
    frames = compute_frames()
    import math
    for i in [0, 1000, 5000, 6999]:
        hdg = frames[i]['ts']['hdg_rad']
        assert abs(hdg - math.pi) < 1e-6, f"TS heading should be π (180°), got {hdg}"