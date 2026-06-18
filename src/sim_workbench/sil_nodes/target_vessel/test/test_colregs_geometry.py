from __future__ import annotations

from target_vessel.geometry import (
    VesselKinematics,
    apply_rot_limit,
    compute_cpa_tcpa,
    relative_bearing_deg,
    signed_delta_deg,
    wrap_deg,
)


def test_wrap_and_signed_delta():
    assert wrap_deg(370.0) == 10.0
    assert wrap_deg(-10.0) == 350.0
    assert signed_delta_deg(10.0, 350.0) == 20.0
    assert signed_delta_deg(350.0, 10.0) == -20.0


def test_relative_bearing_starboard_and_port():
    own = VesselKinematics(lat=63.0, lon=10.0, heading_deg=0.0, sog_mps=5.0)
    starboard = VesselKinematics(lat=63.0, lon=10.01, heading_deg=270.0, sog_mps=5.0)
    port = VesselKinematics(lat=63.0, lon=9.99, heading_deg=90.0, sog_mps=5.0)
    assert 80.0 < relative_bearing_deg(own, starboard) < 100.0
    assert -100.0 < relative_bearing_deg(own, port) < -80.0


def test_cpa_tcpa_for_head_on_collision_course():
    own = VesselKinematics(lat=63.0, lon=10.0, heading_deg=0.0, sog_mps=5.0)
    target = VesselKinematics(lat=63.01, lon=10.0, heading_deg=180.0, sog_mps=5.0)
    result = compute_cpa_tcpa(own, target)
    assert result.tcpa_s > 0.0
    assert result.dcpa_m < 5.0


def test_apply_rot_limit_turns_shortest_way():
    heading, rot = apply_rot_limit(
        current_heading_deg=0.0,
        desired_heading_deg=30.0,
        rot_limit_deg_s=3.0,
        dt_s=2.0,
    )
    assert heading == 6.0
    assert rot == 3.0


def test_apply_rot_limit_handles_wraparound():
    heading, rot = apply_rot_limit(
        current_heading_deg=350.0,
        desired_heading_deg=10.0,
        rot_limit_deg_s=5.0,
        dt_s=1.0,
    )
    assert heading == 355.0
    assert rot == 5.0
