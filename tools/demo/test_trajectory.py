"""Tests for Head-On (Rule 14) trajectory generator."""

import math

from trajectory import compute_frames, compute_cpa, ASDR_EVENTS


def test_frame_count():
    frames = compute_frames()
    assert len(frames) == 7000


def test_os_initial_position():
    frames = compute_frames()
    f0 = frames[0]
    assert abs(f0['os']['lat'] - 63.0) < 0.001
    assert abs(f0['os']['lon'] - 5.0) < 0.001


def test_ts_initial_position():
    frames = compute_frames()
    f0 = frames[0]
    assert abs(f0['ts']['lat'] - 63.117451) < 0.001
    assert abs(f0['ts']['lon'] - 5.0) < 0.001


def test_os_heading_during_avoidance():
    frames = compute_frames()
    # T=345s → frame 3450
    f = frames[3450]
    hdg_deg = math.degrees(f['os']['hdg_rad']) % 360
    assert 10.0 < hdg_deg < 30.0


def test_os_heading_after_full_turn():
    frames = compute_frames()
    # T=395s → frame 3950, heading ≈ 35°
    f = frames[3950]
    hdg_deg = math.degrees(f['os']['hdg_rad']) % 360
    assert abs(hdg_deg - 35.0) < 0.5


def test_os_returns_to_original():
    frames = compute_frames()
    # T=695s → frame 6950, heading near 0°
    f = frames[6950]
    hdg_deg = math.degrees(f['os']['hdg_rad']) % 360
    assert abs(hdg_deg) < 0.5 or abs(hdg_deg - 360.0) < 0.5


def test_cpa_range():
    cpa_nm = compute_cpa()
    # With OS at 63.0 and TS at 63.117451 (7.05 nm apart), both on reciprocal
    # courses at 10 kn, the closest approach is ~3.2 nm (ships never fully cross
    # within 700 s). The CPA occurs near the end of the scenario.
    assert 2.0 < cpa_nm < 5.0


def test_asdr_events_ordered():
    times = [e['t'] for e in ASDR_EVENTS]
    assert times == sorted(times)


def test_ts_heading_constant():
    frames = compute_frames()
    for f in frames:
        assert abs(f['ts']['hdg_rad'] - math.pi) < 0.01