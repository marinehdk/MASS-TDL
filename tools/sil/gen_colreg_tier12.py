"""Generate Tier-1/Tier-2 COLREGs test scenarios (schema_version 3.0).

For each target we fix its course-over-ground (COG) and relative bearing/range
from own ship, then solve for the target SOG that puts both vessels on a
collision course (analytical straight-line DCPA = 0). This guarantees the
scenario carries genuine collision risk so the SIL geometric-compliance gate
treats it as valid.

Geometry conventions (matching tools/sil/scenario_spec.py / simulate.py):
  - Nautical heading/COG: degrees clockwise from North.
  - ENU: x = East, y = North. vx = SOG*sin(brg), vy = SOG*cos(brg).
  - Origin: lat0 = 63.44, lon0 = 10.38. 1deg lat = 111120 m,
    1deg lon = 111120*cos(lat0).

Collision solve: target at relative position P = R * u (u = unit vector toward
the target's absolute bearing). Own velocity Vo. We need target velocity
Vt = s*d (d = unit vector of target COG, s = SOG) such that the relative
velocity Vr = Vt - Vo is anti-parallel to P (closes to zero):
    Vr = -k * P,  k > 0
  =>  s*d + K*u = Vo   with K = k*R  (>0)
A 2x2 linear solve gives (s, K); t_cpa = R / K.

Run:  python -m tools.sil.gen_colreg_tier12   (from repo root)
"""
from __future__ import annotations

import math
from pathlib import Path

import yaml

LAT0 = 63.44
LON0 = 10.38
M_PER_DEG_LAT = 111120.0
M_PER_DEG_LON = 111120.0 * math.cos(math.radians(LAT0))
KN = 0.5144  # knots -> m/s
NM = 1852.0  # nautical mile -> m

OUT_DIR = Path(__file__).resolve().parents[2] / "scenarios" / "COLREGs测试"


def _unit_from_nav_deg(deg: float) -> tuple[float, float]:
    r = math.radians(deg)
    return math.sin(r), math.cos(r)  # (east, north)


def enu_to_latlon(x_m: float, y_m: float) -> tuple[float, float]:
    lat = LAT0 + y_m / M_PER_DEG_LAT
    lon = LON0 + x_m / M_PER_DEG_LON
    return lat, lon


def solve_collision_target(
    own_hdg_deg: float,
    own_sog_kn: float,
    rel_brg_deg: float,
    target_cog_deg: float,
    range_m: float,
) -> dict:
    """Return dict with target lat/lon/cog/sog and t_cpa for a collision course."""
    abs_brg = (own_hdg_deg + rel_brg_deg) % 360.0
    ux, uy = _unit_from_nav_deg(abs_brg)          # unit toward target
    dx, dy = _unit_from_nav_deg(target_cog_deg)   # unit of target heading
    ovx, ovy = _unit_from_nav_deg(own_hdg_deg)
    vox, voy = ovx * own_sog_kn * KN, ovy * own_sog_kn * KN

    # Solve [dx ux; dy uy] [s; K] = [vox; voy]
    det = dx * uy - dy * ux
    if abs(det) < 1e-9:
        raise ValueError("degenerate geometry (target COG parallel to bearing)")
    s = (vox * uy - voy * ux) / det
    K = (dx * voy - dy * vox) / det
    if s <= 0 or K <= 0:
        raise ValueError(
            f"no forward collision solution: s={s:.3f} K={K:.3f} "
            f"(rel_brg={rel_brg_deg}, tgt_cog={target_cog_deg})"
        )
    tx, ty = range_m * ux, range_m * uy
    lat, lon = enu_to_latlon(tx, ty)
    return {
        "lat": round(lat, 6),
        "lon": round(lon, 6),
        "cog": round(target_cog_deg, 1),
        "sog": round(s / KN, 2),
        "t_cpa_s": round(range_m / K, 1),
        "rel_brg": rel_brg_deg,
    }


def straight_target(
    rel_brg_deg: float, range_m: float, own_hdg_deg: float, cog_deg: float, sog_kn: float
) -> dict:
    """Place a target at a bearing/range with an explicit course/speed (no solve).

    Used for overtaking (same-course slower target dead ahead)."""
    abs_brg = (own_hdg_deg + rel_brg_deg) % 360.0
    ux, uy = _unit_from_nav_deg(abs_brg)
    lat, lon = enu_to_latlon(range_m * ux, range_m * uy)
    return {"lat": round(lat, 6), "lon": round(lon, 6),
            "cog": round(cog_deg, 1), "sog": round(sog_kn, 2), "rel_brg": rel_brg_deg}


def _own_ship(hdg: float, sog: float) -> dict:
    lat, lon = enu_to_latlon(0.0, 0.0)
    return {
        "static": {"id": 1, "shipType": "Cargo", "name": "FCB Own Ship"},
        "initial": {
            "position": {"latitude": lat, "longitude": lon},
            "cog": hdg, "sog": sog, "heading": hdg,
        },
        "model": "fcb_mmg_vessel",
        "controller": "psbmpc_wrapper",
    }


def _target(idx: int, t: dict) -> dict:
    return {
        "id": f"ts{idx}",
        "static": {"id": idx + 1, "mmsi": 100000000 + idx},
        "initial": {
            "position": {"latitude": t["lat"], "longitude": t["lon"]},
            "cog": t["cog"], "sog": t["sog"], "heading": t["cog"],
        },
        "model": "ais_replay_vessel",
    }


def _scenario(
    *, scenario_id: str, title: str, description: str,
    own_hdg: float, own_sog: float, targets: list[dict],
    rule: str, give_way: str, expected_action: str,
    colregs_rules: list[str], rule_compliance: list[dict],
    cpa_min_m: float, avoidance_time_s: float, avoidance_delta_rad: float,
    avoidance_duration_s: float, total_time: float, n_rps: float,
    wind_dir: float = 0.0, wind_mps: float = 0.0, vis_nm: float = 5.4, seed: int = 100,
) -> dict:
    wind = {"dir_deg": wind_dir, "speed_mps": wind_mps}
    current = {"dir_deg": 0.0, "speed_mps": 0.0}
    return {
        "title": title,
        "description": description,
        "startTime": "2026-05-15T05:08:47Z",
        "ownShip": _own_ship(own_hdg, own_sog),
        "targetShips": [_target(i + 1, t) for i, t in enumerate(targets)],
        "environment": {"wind": wind, "current": current, "visibility_nm": vis_nm},
        "metadata": {
            "schema_version": "3.0",
            "scenario_id": scenario_id,
            "scenario_source": "colregs_test_suite_tier12_v1.0",
            "colregs_rules": colregs_rules,
            "vessel_class": "FCB",
            "odd_cell": {"domain": "open_sea_offshore_wind_farm"},
            "encounter": {
                "rule": rule,
                "give_way_vessel": give_way,
                "expected_own_action": expected_action,
                "avoidance_time_s": avoidance_time_s,
                "avoidance_delta_rad": avoidance_delta_rad,
                "avoidance_duration_s": avoidance_duration_s,
            },
            "seed": seed,
            "expected_outcome": {
                "cpa_min_m_ge": cpa_min_m,
                "rule_compliance": rule_compliance,
            },
            "simulation_settings": {
                "total_time": total_time,
                "dt": 0.02,
                "n_rps_initial": n_rps,
                "coordinate_origin": [LAT0, LON0],
                "dynamics_mode": "internal",
                "backend": "ros2",
            },
            "disturbance": {"wind": wind, "current": current},
        },
    }


def build_all() -> dict[str, dict]:
    scenarios: dict[str, dict] = {}

    # ── Tier 1.1 — Rule 17 stand-on, target crossing from port bow ───────────
    t = solve_collision_target(own_hdg_deg=0.0, own_sog_kn=10.0,
                               rel_brg_deg=315.0, target_cog_deg=90.0, range_m=2.0 * NM)
    scenarios["colreg-rule17-cr-so"] = _scenario(
        scenario_id="colreg-rule17-cr-so-001-v1.0",
        title="Scenario: colreg-rule17-cr-so-001-v1.0",
        description=(
            "Rule 17 stand-on. Target crosses from own ship's PORT bow "
            f"(relative bearing {t['rel_brg']:.0f} deg), heading {t['cog']:.0f} deg at "
            f"{t['sog']:.1f} kn. Own ship is the STAND-ON vessel and must initially keep "
            "course and speed (Rule 17(a)(i)). The give-way target holds its course "
            "(straight-line replay) and never acts, so own ship must take last-moment "
            f"independent action (Rule 17(b)) near t_cpa ~= {t['t_cpa_s']:.0f} s. DCPA ~= 0 m "
            "without action."
        ),
        own_hdg=0.0, own_sog=10.0, targets=[t],
        rule="Rule17", give_way="target", expected_action="maintain",
        colregs_rules=["R17", "R15", "R8"],
        rule_compliance=[{"rule": "Rule17", "result": "required"},
                         {"rule": "Rule15", "result": "required"}],
        cpa_min_m=500.0, avoidance_time_s=t["t_cpa_s"] - 120.0,
        avoidance_delta_rad=0.6109, avoidance_duration_s=60.0,
        total_time=max(600.0, t["t_cpa_s"] + 300.0), n_rps=3.0, seed=101,
    )

    # ── Tier 1.2 — Rule 17 stand-on variant, faster target, shorter TCPA ─────
    t = solve_collision_target(own_hdg_deg=0.0, own_sog_kn=10.0,
                               rel_brg_deg=290.0, target_cog_deg=70.0, range_m=1.5 * NM)
    scenarios["colreg-rule17-cr-so-2"] = _scenario(
        scenario_id="colreg-rule17-cr-so-002-v1.0",
        title="Scenario: colreg-rule17-cr-so-002-v1.0",
        description=(
            "Rule 17 stand-on variant. Faster target crossing from the port quarter-bow "
            f"(relative bearing {t['rel_brg']:.0f} deg), heading {t['cog']:.0f} deg at "
            f"{t['sog']:.1f} kn, shorter t_cpa ~= {t['t_cpa_s']:.0f} s forcing an earlier "
            "Rule 17(b) transition. Own ship is stand-on (maintain, then last-moment action). "
            "DCPA ~= 0 m without action."
        ),
        own_hdg=0.0, own_sog=10.0, targets=[t],
        rule="Rule17", give_way="target", expected_action="maintain",
        colregs_rules=["R17", "R15", "R8"],
        rule_compliance=[{"rule": "Rule17", "result": "required"},
                         {"rule": "Rule15", "result": "required"}],
        cpa_min_m=400.0, avoidance_time_s=max(30.0, t["t_cpa_s"] - 90.0),
        avoidance_delta_rad=0.6981, avoidance_duration_s=60.0,
        total_time=max(600.0, t["t_cpa_s"] + 300.0), n_rps=3.0,
        wind_dir=180.0, wind_mps=4.0, vis_nm=4.0, seed=102,
    )

    # ── Tier 1.3 — Rule 14 head-on, target biased slightly to PORT ──────────
    t = solve_collision_target(own_hdg_deg=0.0, own_sog_kn=12.0,
                               rel_brg_deg=355.0, target_cog_deg=170.0, range_m=2.0 * NM)
    scenarios["colreg-rule14-ho-port"] = _scenario(
        scenario_id="colreg-rule14-ho-port-001-v1.0",
        title="Scenario: colreg-rule14-ho-port-001-v1.0",
        description=(
            "Rule 14 head-on with the target biased slightly to PORT of own bow "
            f"(relative bearing {t['rel_brg']:.0f} deg, i.e. ~5 deg to port; still inside the "
            "+/-22.5 deg head-on sector), nearly reciprocal course "
            f"{t['cog']:.0f} deg at {t['sog']:.1f} kn. Rule 14 requires a STARBOARD turn even "
            "when the other vessel is marginally to port; a port turn is a violation. "
            "DCPA ~= 0 m without action."
        ),
        own_hdg=0.0, own_sog=12.0, targets=[t],
        rule="Rule14", give_way="own", expected_action="turn_starboard",
        colregs_rules=["R14", "R8"],
        rule_compliance=[{"rule": "Rule14", "result": "required"}],
        cpa_min_m=500.0, avoidance_time_s=max(30.0, t["t_cpa_s"] - 150.0),
        avoidance_delta_rad=1.0472, avoidance_duration_s=90.0,
        total_time=max(600.0, t["t_cpa_s"] + 300.0), n_rps=3.5, seed=103,
    )

    # ── Tier 2.1 — Multi-ship: two starboard crossings (dual give-way) ──────
    own_hdg, own_sog = 270.0, 12.0
    t1 = solve_collision_target(own_hdg, own_sog, rel_brg_deg=45.0,
                                target_cog_deg=200.0, range_m=2.2 * NM)
    t2 = solve_collision_target(own_hdg, own_sog, rel_brg_deg=65.0,
                                target_cog_deg=220.0, range_m=1.8 * NM)
    scenarios["colreg-rule15-ms"] = _scenario(
        scenario_id="colreg-rule15-ms-001-v1.0",
        title="Scenario: colreg-rule15-ms-001-v1.0",
        description=(
            "Rule 15/16 multi-ship squeeze. Own ship heads WEST (270 deg) at "
            f"{own_sog:.0f} kn. Two power-driven vessels cross from starboard: TS1 at relative "
            f"bearing {t1['rel_brg']:.0f} deg ({t1['sog']:.1f} kn, t_cpa ~= {t1['t_cpa_s']:.0f} s) "
            f"and TS2 at relative bearing {t2['rel_brg']:.0f} deg ({t2['sog']:.1f} kn, t_cpa ~= "
            f"{t2['t_cpa_s']:.0f} s). Own "
            "ship is give-way to BOTH and must produce a single unified maneuver (large "
            "starboard alteration or speed reduction) clearing both, not just the nearest. "
            "DCPA ~= 0 m to each without action."
        ),
        own_hdg=own_hdg, own_sog=own_sog, targets=[t1, t2],
        rule="Rule15_Stbd", give_way="own", expected_action="turn_starboard",
        colregs_rules=["R15", "R16", "R8"],
        rule_compliance=[{"rule": "Rule15", "result": "required"},
                         {"rule": "Rule16", "result": "required"}],
        cpa_min_m=300.0, avoidance_time_s=max(30.0, min(t1["t_cpa_s"], t2["t_cpa_s"]) - 120.0),
        avoidance_delta_rad=0.8727, avoidance_duration_s=90.0,
        total_time=max(800.0, max(t1["t_cpa_s"], t2["t_cpa_s"]) + 300.0), n_rps=3.5, seed=104,
    )

    # ── Tier 2.2 — Overtaking + crossing overlap (R15 > R13 priority) ───────
    own_hdg, own_sog = 0.0, 14.0
    t1 = straight_target(rel_brg_deg=0.0, range_m=1.0 * NM, own_hdg_deg=own_hdg,
                         cog_deg=0.0, sog_kn=7.0)  # slow vessel dead ahead -> overtaking
    t2 = solve_collision_target(own_hdg, own_sog, rel_brg_deg=60.0,
                                target_cog_deg=290.0, range_m=2.0 * NM)
    scenarios["colreg-rule13-15-ms"] = _scenario(
        scenario_id="colreg-rule13-15-ms-001-v1.0",
        title="Scenario: colreg-rule13-15-ms-001-v1.0",
        description=(
            "Multi-rule overlap: Rule 13 overtaking vs Rule 15 crossing. Own ship (14 kn) is "
            "overtaking a slow vessel TS1 dead ahead (7 kn, same course) when a faster vessel "
            f"TS2 enters from starboard (relative bearing 60 deg, {t2['sog']:.1f} kn, t_cpa ~= "
            f"{t2['t_cpa_s']:.0f} s). COLREGs priority is Rule 15 > Rule 13: own ship must "
            "suspend/defer the overtaking pass and give way to TS2 (starboard alteration or "
            "speed reduction) while still keeping clear of TS1."
        ),
        own_hdg=own_hdg, own_sog=own_sog, targets=[t1, t2],
        rule="Rule15_Stbd", give_way="own", expected_action="turn_starboard",
        colregs_rules=["R13", "R15", "R16", "R8"],
        rule_compliance=[{"rule": "Rule13", "result": "required"},
                         {"rule": "Rule15", "result": "required"},
                         {"rule": "Rule16", "result": "required"}],
        cpa_min_m=300.0, avoidance_time_s=max(30.0, t2["t_cpa_s"] - 120.0),
        avoidance_delta_rad=0.6109, avoidance_duration_s=90.0,
        total_time=max(800.0, t2["t_cpa_s"] + 300.0), n_rps=4.2, seed=105,
    )

    # ── Tier 2.3 — Head-on + starboard crossing combo (R14/R15 arbitration) ─
    own_hdg, own_sog = 0.0, 12.0
    t1 = solve_collision_target(own_hdg, own_sog, rel_brg_deg=5.0,
                                target_cog_deg=190.0, range_m=2.4 * NM)  # head-on
    t2 = solve_collision_target(own_hdg, own_sog, rel_brg_deg=70.0,
                                target_cog_deg=300.0, range_m=2.0 * NM)  # stbd crossing
    scenarios["colreg-ms-headon-cross"] = _scenario(
        scenario_id="colreg-ms-headon-cross-001-v1.0",
        title="Scenario: colreg-ms-headon-cross-001-v1.0",
        description=(
            "Multi-ship Rule 14 + Rule 15 arbitration. TS1 is head-on (relative bearing 5 deg, "
            f"reciprocal course, {t1['sog']:.1f} kn, t_cpa ~= {t1['t_cpa_s']:.0f} s) and TS2 "
            f"crosses from starboard (relative bearing 70 deg, {t2['sog']:.1f} kn, t_cpa ~= "
            f"{t2['t_cpa_s']:.0f} s). Both rules resolve to a STARBOARD alteration; own ship "
            "must produce one consistent give-way maneuver clearing both. DCPA ~= 0 m to each "
            "without action."
        ),
        own_hdg=own_hdg, own_sog=own_sog, targets=[t1, t2],
        rule="Rule14", give_way="own", expected_action="turn_starboard",
        colregs_rules=["R14", "R15", "R16", "R8"],
        rule_compliance=[{"rule": "Rule14", "result": "required"},
                         {"rule": "Rule15", "result": "required"},
                         {"rule": "Rule16", "result": "required"}],
        cpa_min_m=300.0, avoidance_time_s=max(30.0, min(t1["t_cpa_s"], t2["t_cpa_s"]) - 150.0),
        avoidance_delta_rad=1.0472, avoidance_duration_s=90.0,
        total_time=max(800.0, max(t1["t_cpa_s"], t2["t_cpa_s"]) + 300.0), n_rps=3.5, seed=106,
    )

    return scenarios


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    scenarios = build_all()
    for stem, doc in scenarios.items():
        path = OUT_DIR / f"{stem}.yaml"
        path.write_text(yaml.safe_dump(doc, sort_keys=False, allow_unicode=True))
        print(f"wrote {path.relative_to(OUT_DIR.parents[1])}")
    print(f"\n{len(scenarios)} scenarios generated.")


if __name__ == "__main__":
    main()
