#!/usr/bin/env python3
"""
generate_scenarios.py — COLREGs HIL scenario generator for MASS L3 Tactical Layer.

Generates parameterised COLREGs encounter scenario JSON files covering
Rules 13, 14, 15, 17, 18, 19 plus fault-injection scenarios.

Usage:
    python generate_scenarios.py [--count N] [--output-dir DIR]

Defaults:
    --count      1000  (minimum total; actual may be higher depending on grid)
    --output-dir tests/hil/scenarios/

Outputs:
    <output-dir>/scenarios_all.json   — full scenario array
    <output-dir>/scenarios_100.json   — 100-scenario curated representative subset
"""

from __future__ import annotations

import argparse
import json
import math
import os
import random
import sys
from pathlib import Path
from typing import List, Tuple

# Allow running as a standalone script from any working directory.
_THIS_DIR = Path(__file__).parent.resolve()
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from scenario_schema import (
    ExpectedOutcome,
    OwnShipInit,
    Scenario,
    TargetShipInit,
    scenarios_to_json,
)

# ---------------------------------------------------------------------------
# Reproducible RNG
# ---------------------------------------------------------------------------
random.seed(42)

# ---------------------------------------------------------------------------
# Geometry helpers
# ---------------------------------------------------------------------------
# Coordinate conventions:
#   OwnShip heading (psi_deg): math convention — 0=East, 90=North
#   Target COG (cog_deg): meteorological convention — 0=North, 90=East
#
# We fix own ship at origin (0, 0) heading North (psi = 90° math = 0° met).
# Target positions are expressed in ENU metres relative to own ship.

_BASE_MMSI_START = 123456000
_mmsi_counter = _BASE_MMSI_START


def _next_mmsi() -> int:
    global _mmsi_counter
    _mmsi_counter += 1
    return _mmsi_counter


def _met_to_math_deg(met_deg: float) -> float:
    """Convert meteorological bearing (0=N, 90=E) to math angle (0=E, 90=N)."""
    return (90.0 - met_deg) % 360.0


def _target_position(
    own_psi_met: float,
    rel_bearing_met: float,
    range_m: float,
) -> Tuple[float, float]:
    """
    Compute target (x, y) in ENU metres given own heading, relative bearing,
    and slant range.  All angles in meteorological degrees.
    """
    abs_bearing_met = (own_psi_met + rel_bearing_met) % 360.0
    # Convert met bearing to math angle for trig
    ang_math = math.radians(_met_to_math_deg(abs_bearing_met))
    x = range_m * math.cos(ang_math)
    y = range_m * math.sin(ang_math)
    return x, y


def _cpa_tcpa(
    ox: float, oy: float, ovx: float, ovy: float,
    tx: float, ty: float, tvx: float, tvy: float,
) -> Tuple[float, float]:
    """
    Compute CPA (m) and TCPA (s) for two vessels with constant velocity.
    Velocities in m/s, positions in m.
    Returns (cpa_m, tcpa_s); TCPA < 0 means already past CPA.
    """
    dx = tx - ox
    dy = ty - oy
    dvx = tvx - ovx
    dvy = tvy - ovy
    dv2 = dvx * dvx + dvy * dvy
    if dv2 < 1e-9:
        dist = math.sqrt(dx * dx + dy * dy)
        return dist, 0.0
    tcpa = -(dx * dvx + dy * dvy) / dv2
    if tcpa < 0:
        tcpa = 0.0
    cx = dx + dvx * tcpa
    cy = dy + dvy * tcpa
    cpa = math.sqrt(cx * cx + cy * cy)
    return cpa, tcpa


def _kn_to_ms(kn: float) -> float:
    return kn * 0.5144444


def _velocity_components(cog_met: float, sog_kn: float) -> Tuple[float, float]:
    """Return (vx_ms, vy_ms) in ENU from meteorological COG and speed in knots."""
    ang_math = math.radians(_met_to_math_deg(cog_met))
    spd = _kn_to_ms(sog_kn)
    return spd * math.cos(ang_math), spd * math.sin(ang_math)


# ---------------------------------------------------------------------------
# Own-ship base state (fixed for all scenarios — own ship at origin, heading N)
# ---------------------------------------------------------------------------
OWN_PSI_MET = 0.0   # meteorological: heading North
OWN_PSI_MATH = _met_to_math_deg(OWN_PSI_MET)  # = 90° (math convention)


def _own_ship(u_kn: float) -> OwnShipInit:
    return OwnShipInit(x_m=0.0, y_m=0.0, psi_deg=OWN_PSI_MATH, u_kn=u_kn)


# ---------------------------------------------------------------------------
# Rule 14 — Head-on (both give way, turn right)
# ---------------------------------------------------------------------------

def _gen_rule14(counters: dict) -> List[Scenario]:
    """
    Own ship heading North (COG 0°); target heading South (COG ~180°).
    Rel bearing ~0° (dead ahead). CPA near 0.
    Vary: range (500–3000 m), own speed (6–18 kn), target speed (6–18 kn),
          angle offset (−5° to +5°).
    Target: ≥ 150 scenarios.
    """
    scenarios: List[Scenario] = []
    rule = "Rule_14"
    rule_tag = "R14"

    ranges_m = [500, 750, 1000, 1500, 2000, 2500, 3000]
    own_speeds = [6, 10, 14, 18]
    tgt_speeds = [6, 10, 14, 18]
    offsets = [-5, -2, 0, 2, 5]

    for r in ranges_m:
        for own_spd in own_speeds:
            for tgt_spd in tgt_speeds:
                for off in offsets:
                    # Target COG: heading South with small offset
                    tgt_cog = (180.0 + off) % 360.0
                    # Target position: dead ahead (rel bearing 0°) from own ship
                    tx, ty = _target_position(OWN_PSI_MET, 0.0 + off * 0.1, r)
                    # CPA/TCPA
                    ovx, ovy = _velocity_components(OWN_PSI_MET, own_spd)
                    tvx, tvy = _velocity_components(tgt_cog, tgt_spd)
                    cpa, tcpa = _cpa_tcpa(0, 0, ovx, ovy, tx, ty, tvx, tvy)

                    counters[rule_tag] += 1
                    sid = f"SCN-{rule_tag}-{counters[rule_tag]:04d}"
                    tgt = TargetShipInit(
                        mmsi=_next_mmsi(),
                        vessel_type="motor_vessel",
                        x_m=round(tx, 1),
                        y_m=round(ty, 1),
                        cog_deg=round(tgt_cog, 1),
                        sog_kn=float(tgt_spd),
                        length_m=random.choice([80, 100, 120, 150]),
                    )
                    expected = ExpectedOutcome(
                        colregs_rule=rule,
                        own_role="both_give_way",
                        m5_expected_action="turn_right",
                        cpa_m=round(cpa, 1),
                        tcpa_s=round(tcpa, 1),
                    )
                    tags = ["head_on", "single_target"]
                    if r < 1000:
                        tags.append("high_risk")
                    scenarios.append(Scenario(
                        scenario_id=sid,
                        colregs_rule=rule,
                        description=(
                            f"Rule 14 head-on: range {r}m, own {own_spd}kn, "
                            f"tgt {tgt_spd}kn, offset {off}°"
                        ),
                        odd_visibility_nm=5.0,
                        odd_mode="NORMAL",
                        own_ship=_own_ship(own_spd),
                        targets=[tgt],
                        expected=expected,
                        tags=tags,
                    ))
    return scenarios


# ---------------------------------------------------------------------------
# Rule 15 — Crossing (own give way, target from starboard)
# ---------------------------------------------------------------------------

def _gen_rule15(counters: dict) -> List[Scenario]:
    """
    Target from starboard (relative bearing 045–135°), own is give-way.
    Vary: bearing (045, 067, 090, 112, 135), TCPA (60–600 s), speeds.
    Target: ≥ 200 scenarios.
    """
    scenarios: List[Scenario] = []
    rule = "Rule_15"
    rule_tag = "R15"

    rel_bearings = [45, 67, 90, 112, 135]  # meteorological relative bearing from own bow
    tcpas_s = [60, 120, 180, 300, 450, 600]
    own_speeds = [6, 10, 14, 18]
    tgt_speeds = [6, 10, 14]

    for rb in rel_bearings:
        for tcpa_target in tcpas_s:
            for own_spd in own_speeds:
                for tgt_spd in tgt_speeds:
                    # Compute range from desired TCPA and closing speed (approx)
                    closing_spd = _kn_to_ms(own_spd + tgt_spd) * 0.7
                    r = max(300.0, closing_spd * tcpa_target)

                    # Target position at rel bearing rb, range r
                    tx, ty = _target_position(OWN_PSI_MET, float(rb), r)

                    # Target COG: heading roughly perpendicular (will cross own bow)
                    # Target crosses from starboard → target heading roughly West/left of own path
                    # Own heading North (0° met), target from starboard heading toward port side
                    tgt_cog = (OWN_PSI_MET + 180.0 + (90.0 - rb)) % 360.0

                    ovx, ovy = _velocity_components(OWN_PSI_MET, own_spd)
                    tvx, tvy = _velocity_components(tgt_cog, tgt_spd)
                    cpa, tcpa = _cpa_tcpa(0, 0, ovx, ovy, tx, ty, tvx, tvy)

                    counters[rule_tag] += 1
                    sid = f"SCN-{rule_tag}-{counters[rule_tag]:04d}"
                    tgt = TargetShipInit(
                        mmsi=_next_mmsi(),
                        vessel_type="motor_vessel",
                        x_m=round(tx, 1),
                        y_m=round(ty, 1),
                        cog_deg=round(tgt_cog % 360.0, 1),
                        sog_kn=float(tgt_spd),
                        length_m=random.choice([60, 80, 100, 120]),
                    )
                    expected = ExpectedOutcome(
                        colregs_rule=rule,
                        own_role="give_way",
                        m5_expected_action="turn_right_or_slow",
                        cpa_m=round(cpa, 1),
                        tcpa_s=round(tcpa, 1),
                    )
                    tags = ["crossing", "starboard_target", "single_target"]
                    if tcpa_target < 180:
                        tags.append("high_risk")
                    if rb == 90:
                        tags.append("beam_crossing")
                    scenarios.append(Scenario(
                        scenario_id=sid,
                        colregs_rule=rule,
                        description=(
                            f"Rule 15 crossing: rel_bearing {rb}°, TCPA~{tcpa_target}s, "
                            f"own {own_spd}kn, tgt {tgt_spd}kn"
                        ),
                        odd_visibility_nm=5.0,
                        odd_mode="NORMAL",
                        own_ship=_own_ship(own_spd),
                        targets=[tgt],
                        expected=expected,
                        tags=tags,
                    ))
    return scenarios


# ---------------------------------------------------------------------------
# Rule 13 — Overtaking (own is overtaken = give way)
# ---------------------------------------------------------------------------

def _gen_rule13(counters: dict) -> List[Scenario]:
    """
    Target from aft sector (rel bearing 112.5°–247.5° from bow, i.e. from astern).
    Target faster than own ship.  Own is the overtaking vessel (give way).
    Vary: aft bearing, speed differential, range.
    Target: ≥ 150 scenarios.
    """
    scenarios: List[Scenario] = []
    rule = "Rule_13"
    rule_tag = "R13"

    # Aft relative bearings (meteorological, measured from own bow)
    aft_rel_bearings = [113, 135, 157, 180, 203, 225, 247]
    own_speeds = [6, 8, 10, 12]
    spd_differentials = [2, 4, 6, 8]  # target faster by this many knots
    ranges_m = [300, 500, 800, 1200, 1800]

    for arb in aft_rel_bearings:
        for own_spd in own_speeds:
            for delta in spd_differentials:
                tgt_spd = own_spd + delta
                for r in ranges_m:
                    tx, ty = _target_position(OWN_PSI_MET, float(arb), r)
                    # Target heading: same general direction as own ship (both heading North-ish)
                    # with slight variation to converge
                    tgt_cog = (OWN_PSI_MET + random.uniform(-10, 10)) % 360.0

                    ovx, ovy = _velocity_components(OWN_PSI_MET, own_spd)
                    tvx, tvy = _velocity_components(tgt_cog, tgt_spd)
                    cpa, tcpa = _cpa_tcpa(0, 0, ovx, ovy, tx, ty, tvx, tvy)

                    counters[rule_tag] += 1
                    sid = f"SCN-{rule_tag}-{counters[rule_tag]:04d}"
                    tgt = TargetShipInit(
                        mmsi=_next_mmsi(),
                        vessel_type="motor_vessel",
                        x_m=round(tx, 1),
                        y_m=round(ty, 1),
                        cog_deg=round(tgt_cog, 1),
                        sog_kn=float(tgt_spd),
                        length_m=random.choice([80, 100, 120, 150, 180]),
                    )
                    expected = ExpectedOutcome(
                        colregs_rule=rule,
                        own_role="give_way",
                        m5_expected_action="turn_right",
                        cpa_m=round(cpa, 1),
                        tcpa_s=round(tcpa, 1),
                    )
                    tags = ["overtaking", "aft_sector", "single_target"]
                    if delta >= 6:
                        tags.append("high_speed_differential")
                    if r < 500:
                        tags.append("close_range")
                    scenarios.append(Scenario(
                        scenario_id=sid,
                        colregs_rule=rule,
                        description=(
                            f"Rule 13 overtaking: aft_rel_bearing {arb}°, range {r}m, "
                            f"own {own_spd}kn, tgt {tgt_spd}kn (+{delta}kn)"
                        ),
                        odd_visibility_nm=5.0,
                        odd_mode="NORMAL",
                        own_ship=_own_ship(own_spd),
                        targets=[tgt],
                        expected=expected,
                        tags=tags,
                    ))
    return scenarios


# ---------------------------------------------------------------------------
# Rule 17 — Stand-on (own is stand-on, target to port / already altering)
# ---------------------------------------------------------------------------

def _gen_rule17(counters: dict) -> List[Scenario]:
    """
    Own ship is stand-on; target is give-way (crossing from port, or other geometry
    where own has right-of-way).  Target COG shows it is already altering course away.
    Vary: target geometry, separation, speed.
    Target: ≥ 150 scenarios.
    """
    scenarios: List[Scenario] = []
    rule = "Rule_17"
    rule_tag = "R17"

    # Port-side relative bearings
    port_rel_bearings = [225, 247, 270, 292, 315]
    own_speeds = [6, 10, 14, 18]
    tgt_speeds = [6, 10, 14]
    ranges_m = [500, 1000, 1500, 2000, 2500, 3000]
    # Target altering-away COG offsets (positive = turning further away from own ship)
    avoidance_offsets = [10, 20, 30, 45]

    for prb in port_rel_bearings:
        for own_spd in own_speeds:
            for tgt_spd in tgt_speeds:
                for r in ranges_m:
                    for av_off in avoidance_offsets:
                        tx, ty = _target_position(OWN_PSI_MET, float(prb), r)
                        # Target base COG would have it crossing own path; add offset to show it's
                        # already altering away to starboard (clockwise from own perspective)
                        base_cog = (OWN_PSI_MET + 90.0) % 360.0  # heading East to cross
                        tgt_cog = (base_cog + av_off) % 360.0

                        ovx, ovy = _velocity_components(OWN_PSI_MET, own_spd)
                        tvx, tvy = _velocity_components(tgt_cog, tgt_spd)
                        cpa, tcpa = _cpa_tcpa(0, 0, ovx, ovy, tx, ty, tvx, tvy)

                        counters[rule_tag] += 1
                        sid = f"SCN-{rule_tag}-{counters[rule_tag]:04d}"
                        tgt = TargetShipInit(
                            mmsi=_next_mmsi(),
                            vessel_type="motor_vessel",
                            x_m=round(tx, 1),
                            y_m=round(ty, 1),
                            cog_deg=round(tgt_cog, 1),
                            sog_kn=float(tgt_spd),
                            length_m=random.choice([60, 80, 100, 120]),
                        )
                        expected = ExpectedOutcome(
                            colregs_rule=rule,
                            own_role="stand_on",
                            m5_expected_action="maintain_course",
                            cpa_m=round(cpa, 1),
                            tcpa_s=round(tcpa, 1),
                        )
                        tags = ["stand_on", "port_target", "single_target", "target_altering_away"]
                        if r < 1000:
                            tags.append("close_range")
                        scenarios.append(Scenario(
                            scenario_id=sid,
                            colregs_rule=rule,
                            description=(
                                f"Rule 17 stand-on: port_rel_bearing {prb}°, range {r}m, "
                                f"tgt altering +{av_off}° away, own {own_spd}kn"
                            ),
                            odd_visibility_nm=5.0,
                            odd_mode="NORMAL",
                            own_ship=_own_ship(own_spd),
                            targets=[tgt],
                            expected=expected,
                            tags=tags,
                        ))
    return scenarios


# ---------------------------------------------------------------------------
# Rule 18 — Responsibilities between vessels (motor vs restricted)
# ---------------------------------------------------------------------------

def _gen_rule18(counters: dict) -> List[Scenario]:
    """
    Own ship (motor vessel) must give way to restricted-maneuvering / fishing vessels.
    Vary: target vessel type, geometry, range, speed.
    Target: ≥ 150 scenarios.
    """
    scenarios: List[Scenario] = []
    rule = "Rule_18"
    rule_tag = "R18"

    priority_types = [
        ("fishing_vessel", "fishing", [60, 80, 100]),
        ("sailing_vessel", "sailing_vessel", [60, 80, 100, 120]),
        ("restricted_maneuvering", "restricted_maneuvering", [80, 100, 120, 150]),
    ]
    rel_bearings = [0, 45, 90, 135, 180, 225, 270, 315]
    own_speeds = [6, 10, 14]
    tgt_speeds = [3, 5, 8]
    ranges_m = [300, 500, 800, 1200, 2000]

    for vessel_type, type_tag, lengths in priority_types:
        for rb in rel_bearings:
            for own_spd in own_speeds:
                for tgt_spd in tgt_speeds:
                    for r in ranges_m:
                        tx, ty = _target_position(OWN_PSI_MET, float(rb), r)
                        # Target COG: slow, various headings
                        tgt_cog = random.uniform(0, 360)

                        ovx, ovy = _velocity_components(OWN_PSI_MET, own_spd)
                        tvx, tvy = _velocity_components(tgt_cog, tgt_spd)
                        cpa, tcpa = _cpa_tcpa(0, 0, ovx, ovy, tx, ty, tvx, tvy)

                        counters[rule_tag] += 1
                        sid = f"SCN-{rule_tag}-{counters[rule_tag]:04d}"
                        tgt = TargetShipInit(
                            mmsi=_next_mmsi(),
                            vessel_type=vessel_type,
                            x_m=round(tx, 1),
                            y_m=round(ty, 1),
                            cog_deg=round(tgt_cog, 1),
                            sog_kn=float(tgt_spd),
                            length_m=random.choice(lengths),
                        )
                        expected = ExpectedOutcome(
                            colregs_rule=rule,
                            own_role="give_way",
                            m5_expected_action="turn_right",
                            cpa_m=round(cpa, 1),
                            tcpa_s=round(tcpa, 1),
                        )
                        tags = ["rule18_responsibilities", f"target_{type_tag}", "single_target"]
                        if r < 500:
                            tags.append("close_range")
                        scenarios.append(Scenario(
                            scenario_id=sid,
                            colregs_rule=rule,
                            description=(
                                f"Rule 18: own motor_vessel must give way to {vessel_type}, "
                                f"rel_bearing {rb}°, range {r}m"
                            ),
                            odd_visibility_nm=5.0,
                            odd_mode="NORMAL",
                            own_ship=_own_ship(own_spd),
                            targets=[tgt],
                            expected=expected,
                            tags=tags,
                        ))
    return scenarios


# ---------------------------------------------------------------------------
# Rule 19 — Restricted visibility variants of Rules 14/15
# ---------------------------------------------------------------------------

def _gen_rule19(counters: dict) -> List[Scenario]:
    """
    Same geometric setups as Rules 14/15 but odd_visibility_nm=0.3, DEGRADED.
    In restricted visibility all vessels must proceed at safe speed; M5 expected
    action is "slow_down" regardless of original rule geometry.
    Target: ≥ 100 scenarios.
    """
    scenarios: List[Scenario] = []
    rule = "Rule_19"
    rule_tag = "R19"

    geometries = [
        # (description_template, rel_bearing_met, target_cog_offset_from_south, tags)
        ("head-on geometry", 0.0, 0.0, ["head_on_geometry"]),
        ("crossing_stbd geometry", 90.0, -90.0, ["crossing_geometry"]),
        ("crossing_045 geometry", 45.0, -135.0, ["crossing_geometry"]),
        ("overtaking_aft geometry", 180.0, 10.0, ["overtaking_geometry"]),
    ]
    ranges_m = [200, 400, 700, 1000, 1500, 2000]
    own_speeds = [4, 6, 8, 10]
    tgt_speeds = [4, 6, 8]

    for geom_desc, rb, tgt_cog_off, geom_tags in geometries:
        for r in ranges_m:
            for own_spd in own_speeds:
                for tgt_spd in tgt_speeds:
                    tx, ty = _target_position(OWN_PSI_MET, rb, r)
                    tgt_cog = (180.0 + tgt_cog_off + random.uniform(-5, 5)) % 360.0

                    ovx, ovy = _velocity_components(OWN_PSI_MET, own_spd)
                    tvx, tvy = _velocity_components(tgt_cog, tgt_spd)
                    cpa, tcpa = _cpa_tcpa(0, 0, ovx, ovy, tx, ty, tvx, tvy)

                    counters[rule_tag] += 1
                    sid = f"SCN-{rule_tag}-{counters[rule_tag]:04d}"
                    tgt = TargetShipInit(
                        mmsi=_next_mmsi(),
                        vessel_type="motor_vessel",
                        x_m=round(tx, 1),
                        y_m=round(ty, 1),
                        cog_deg=round(tgt_cog, 1),
                        sog_kn=float(tgt_spd),
                        length_m=random.choice([60, 80, 100, 120]),
                    )
                    expected = ExpectedOutcome(
                        colregs_rule=rule,
                        own_role="both_give_way",
                        m5_expected_action="slow_down",
                        cpa_m=round(cpa, 1),
                        tcpa_s=round(tcpa, 1),
                    )
                    tags = ["restricted_visibility", "fog"] + geom_tags + ["single_target"]
                    if r < 500:
                        tags.append("close_range")
                    scenarios.append(Scenario(
                        scenario_id=sid,
                        colregs_rule=rule,
                        description=(
                            f"Rule 19 restricted visibility ({geom_desc}): "
                            f"range {r}m, own {own_spd}kn, vis 0.3nm"
                        ),
                        odd_visibility_nm=0.3,
                        odd_mode="DEGRADED",
                        own_ship=_own_ship(own_spd),
                        targets=[tgt],
                        expected=expected,
                        tags=tags,
                    ))
    return scenarios


# ---------------------------------------------------------------------------
# Fault injection scenarios
# ---------------------------------------------------------------------------

def _gen_fault_injection(counters: dict) -> List[Scenario]:
    """
    Fault injection: DEGRADED (vis=0.3nm) and CRITICAL (vis=0.1nm / GNSS-loss).
    Same geometry as Rule 14/15 but with degraded ODD state.
    Target: ≥ 100 scenarios.
    """
    scenarios: List[Scenario] = []
    rule_tag = "FAULT"

    fault_modes = [
        ("DEGRADED", 0.3, "degraded_visibility"),
        ("CRITICAL", 0.1, "gnss_loss"),
    ]
    rel_bearings = [0, 45, 90, 135, 180, 270]
    own_speeds = [4, 6, 8, 10, 12]
    tgt_speeds = [4, 6, 8, 10]
    ranges_m = [300, 600, 1000, 1500, 2000]

    for odd_mode, vis_nm, fault_tag in fault_modes:
        for rb in rel_bearings:
            for own_spd in own_speeds:
                for tgt_spd in tgt_speeds:
                    for r in ranges_m[:3]:  # keep count manageable while hitting ≥100
                        tx, ty = _target_position(OWN_PSI_MET, float(rb), r)
                        tgt_cog = (180.0 + random.uniform(-10, 10)) % 360.0

                        ovx, ovy = _velocity_components(OWN_PSI_MET, own_spd)
                        tvx, tvy = _velocity_components(tgt_cog, tgt_spd)
                        cpa, tcpa = _cpa_tcpa(0, 0, ovx, ovy, tx, ty, tvx, tvy)

                        counters[rule_tag] += 1
                        sid = f"SCN-{rule_tag}-{counters[rule_tag]:04d}"
                        tgt = TargetShipInit(
                            mmsi=_next_mmsi(),
                            vessel_type="motor_vessel",
                            x_m=round(tx, 1),
                            y_m=round(ty, 1),
                            cog_deg=round(tgt_cog, 1),
                            sog_kn=float(tgt_spd),
                            length_m=random.choice([60, 80, 100, 120]),
                        )
                        colregs_rule = "Rule_19" if odd_mode in ("DEGRADED", "CRITICAL") else "Rule_14"
                        expected = ExpectedOutcome(
                            colregs_rule=colregs_rule,
                            own_role="both_give_way",
                            m5_expected_action="slow_down",
                            cpa_m=round(cpa, 1),
                            tcpa_s=round(tcpa, 1),
                        )
                        tags = ["fault_injection", fault_tag, odd_mode.lower(), "single_target"]
                        if r < 500:
                            tags.append("close_range")
                        scenarios.append(Scenario(
                            scenario_id=sid,
                            colregs_rule=colregs_rule,
                            description=(
                                f"Fault injection ({odd_mode}, vis={vis_nm}nm): "
                                f"rel_bearing {rb}°, range {r}m, own {own_spd}kn"
                            ),
                            odd_visibility_nm=vis_nm,
                            odd_mode=odd_mode,
                            own_ship=_own_ship(own_spd),
                            targets=[tgt],
                            expected=expected,
                            tags=tags,
                        ))
    return scenarios


# ---------------------------------------------------------------------------
# Curated 100-scenario representative subset
# ---------------------------------------------------------------------------

def _select_100(all_scenarios: List[Scenario]) -> List[Scenario]:
    """
    Select one scenario per cell in a rule × geometry matrix.
    Strategy: for each rule, collect unique (colregs_rule, tags[0]) combinations,
    then pick one scenario per combination until we have 100.
    Falls back to round-robin over rules if cells are few.
    """
    from collections import defaultdict

    # Group by (colregs_rule, primary_tag)
    cells: dict = defaultdict(list)
    for s in all_scenarios:
        primary_tag = s.tags[0] if s.tags else "generic"
        key = (s.colregs_rule, primary_tag)
        cells[key].append(s)

    # Sort keys for determinism
    sorted_keys = sorted(cells.keys())

    selected: List[Scenario] = []
    # Round-robin over cells until 100 selected
    idx = 0
    max_rounds = 20
    round_count = 0
    while len(selected) < 100 and round_count < max_rounds:
        round_count += 1
        for key in sorted_keys:
            pool = cells[key]
            if idx < len(pool):
                selected.append(pool[idx])
            if len(selected) == 100:
                break
        idx += 1

    # Fallback: if still short (shouldn't happen with ≥1000 scenarios), top up
    if len(selected) < 100:
        remaining = [s for s in all_scenarios if s not in set(selected)]
        selected.extend(remaining[: 100 - len(selected)])

    return selected[:100]


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def generate(min_count: int) -> List[Scenario]:
    """Generate all scenarios (at minimum min_count total)."""
    counters: dict = {
        "R14": 0, "R15": 0, "R13": 0, "R17": 0,
        "R18": 0, "R19": 0, "FAULT": 0,
    }

    # Generate base sets
    r14 = _gen_rule14(counters)
    r15 = _gen_rule15(counters)
    r13 = _gen_rule13(counters)
    r17 = _gen_rule17(counters)
    r18 = _gen_rule18(counters)
    r19 = _gen_rule19(counters)
    fault = _gen_fault_injection(counters)

    all_scenarios = r14 + r15 + r13 + r17 + r18 + r19 + fault

    # If we haven't hit min_count, generate extra Rule 14 variants with random params
    extra_counter = {"R14": counters["R14"]}
    while len(all_scenarios) < min_count:
        r = random.uniform(300, 3000)
        own_spd = random.uniform(5, 18)
        tgt_spd = random.uniform(5, 18)
        off = random.uniform(-10, 10)
        tgt_cog = (180.0 + off) % 360.0
        tx, ty = _target_position(OWN_PSI_MET, off * 0.05, r)
        ovx, ovy = _velocity_components(OWN_PSI_MET, own_spd)
        tvx, tvy = _velocity_components(tgt_cog, tgt_spd)
        cpa, tcpa = _cpa_tcpa(0, 0, ovx, ovy, tx, ty, tvx, tvy)
        extra_counter["R14"] += 1
        sid = f"SCN-R14-EX{extra_counter['R14']:03d}"
        tgt = TargetShipInit(
            mmsi=_next_mmsi(),
            vessel_type="motor_vessel",
            x_m=round(tx, 1),
            y_m=round(ty, 1),
            cog_deg=round(tgt_cog, 1),
            sog_kn=round(tgt_spd, 1),
            length_m=random.choice([80, 100, 120]),
        )
        expected = ExpectedOutcome(
            colregs_rule="Rule_14",
            own_role="both_give_way",
            m5_expected_action="turn_right",
            cpa_m=round(cpa, 1),
            tcpa_s=round(tcpa, 1),
        )
        all_scenarios.append(Scenario(
            scenario_id=sid,
            colregs_rule="Rule_14",
            description=f"Rule 14 extra: range {r:.0f}m, own {own_spd:.1f}kn, tgt {tgt_spd:.1f}kn",
            odd_visibility_nm=5.0,
            odd_mode="NORMAL",
            own_ship=_own_ship(own_spd),
            targets=[tgt],
            expected=expected,
            tags=["head_on", "single_target", "extra"],
        ))

    return all_scenarios


def main() -> None:
    parser = argparse.ArgumentParser(
        description="COLREGs HIL scenario generator for MASS L3 Tactical Layer"
    )
    parser.add_argument(
        "--count", type=int, default=1000,
        help="Minimum number of scenarios to generate (default: 1000)"
    )
    parser.add_argument(
        "--output-dir", type=str, default="tests/hil/scenarios/",
        help="Output directory for scenario JSON files (default: tests/hil/scenarios/)"
    )
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    all_scenarios = generate(args.count)
    rep_100 = _select_100(all_scenarios)

    all_path = output_dir / "scenarios_all.json"
    rep_path = output_dir / "scenarios_100.json"

    all_path.write_text(scenarios_to_json(all_scenarios), encoding="utf-8")
    rep_path.write_text(scenarios_to_json(rep_100), encoding="utf-8")

    print(
        f"Generated {len(all_scenarios)} scenarios "
        f"(100 representative in {rep_path})"
    )


if __name__ == "__main__":
    main()
