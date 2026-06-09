import math
import pytest
from sil_orchestrator.encounter_geometry import (
    OwnState, TargetSpawn, generate_encounter,
)

OWN = OwnState(lat=2.500, lon=101.000, heading_deg=0.0, sog_kn=12.0)  # 向正北


def _dcpa_m(own: OwnState, sp: TargetSpawn) -> float:
    """初始最近会遇距离(米)，用于断言构造几何。"""
    kn = 0.514444
    de = (sp.lon - own.lon) * math.cos(math.radians(own.lat)) * 111120.0
    dn = (sp.lat - own.lat) * 111120.0
    vo = (own.sog_kn*kn*math.sin(math.radians(own.heading_deg)),
          own.sog_kn*kn*math.cos(math.radians(own.heading_deg)))
    vt = (sp.sog_kn*kn*math.sin(math.radians(sp.course_deg)),
          sp.sog_kn*kn*math.cos(math.radians(sp.course_deg)))
    vr = (vt[0]-vo[0], vt[1]-vo[1])
    m = math.hypot(*vr)
    if m < 1e-6:
        return abs(de)
    return abs(de*vr[1] - dn*vr[0]) / m


def _spawn_east_m(own: OwnState, sp: TargetSpawn) -> float:
    return (sp.lon - own.lon) * math.cos(math.radians(own.lat)) * 111120.0


def test_head_on_reciprocal_collision():
    sp = generate_encounter("head_on", OWN)
    assert 175.0 <= sp.course_deg <= 185.0      # reciprocal of 0°
    assert 11.0 <= sp.sog_kn <= 13.0            # ≈ own speed
    assert _dcpa_m(OWN, sp) < 60.0              # 碰撞航线


def test_crossing_giveway_starboard():
    sp = generate_encounter("crossing_giveway", OWN)
    assert _spawn_east_m(OWN, sp) > 0.0         # 目标在右舷
    assert 260.0 <= sp.course_deg <= 280.0      # 自右向左横越
    assert 250.0 <= _dcpa_m(OWN, sp) <= 350.0   # ≈ construct_cpa 300


def test_crossing_standon_port():
    sp = generate_encounter("crossing_standon", OWN)
    assert _spawn_east_m(OWN, sp) < 0.0         # 目标在左舷
    assert 80.0 <= sp.course_deg <= 100.0       # 自左向右横越
    assert 250.0 <= _dcpa_m(OWN, sp) <= 350.0


def test_overtaking_slower_ahead_same_course():
    sp = generate_encounter("overtaking", OWN)
    assert (sp.course_deg <= 5.0 or sp.course_deg >= 355.0)  # 同向
    assert 5.5 <= sp.sog_kn <= 6.5              # 0.5 × own
    assert 150.0 <= _dcpa_m(OWN, sp) <= 250.0


def test_unknown_rule_raises():
    with pytest.raises(ValueError):
        generate_encounter("rule_42", OWN)


def test_override_range_and_cpa():
    sp = generate_encounter("crossing_giveway", OWN, range_nm=3.0, construct_cpa_m=500.0)
    assert 420.0 <= _dcpa_m(OWN, sp) <= 580.0
