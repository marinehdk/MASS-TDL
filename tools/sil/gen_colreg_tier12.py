"""Generate the COLREGs fast-probe scenario set (schema_version 3.0).

This is the **single source of truth** for scenarios/COLREGs测试/ — a lean,
high-quality *probe* set used for fast problem-finding during development, NOT a
comprehensive benchmark (that is the Imazu-22 set in scenarios/IMAZU标准测试/,
which stays frozen + hash-protected; do not duplicate multi-ship cases here).

Design (per the 2026-06-09 scenario review):
  - **Single-ship, single-purpose** probes — each isolates one rule or one
    classification boundary so a failure points straight at the cause.
  - **Close-start**: the target starts near the avoidance distance (~1.3-1.5 NM)
    with an analytical straight-line DCPA ~= 0, so the encounter is in/near the
    avoidance phase from t=0 → short runtime, fast iteration.
  - **Valid pass criterion**: cpa_min_m_ge = SHIP_DOMAIN_M (0.5 NM = 926 m). A
    real ship-domain radius — never 0, never below a defined safe distance
    (an invalid criterion per COLREG Rule 8 "pass at a safe distance").
  - **Boundary coverage**: the head-on/crossing edge (±~6°/22.5°) and the
    crossing/overtaking edge (112.5°) — bugs live at classification boundaries
    (the M6 head-on fishtail lived exactly at the ±6° reciprocal edge).

For each solved target we fix its COG and relative bearing/range from own ship,
then solve for the target SOG that puts both on a collision course (DCPA = 0).
This guarantees genuine collision risk so the geometric-compliance gate (and
tools/sil/verify_colreg_tier12.py, which requires straight-line DCPA < 500 m)
treats the scenario as valid.

Geometry conventions (matching tools/sil/scenario_spec.py / simulate.py):
  - Nautical heading/COG: degrees clockwise from North.
  - ENU: x = East, y = North. vx = SOG*sin(brg), vy = SOG*cos(brg).
  - Origin: lat0 = 63.44, lon0 = 10.38.

NOTE: cpa_min_m_ge (926 m) and the close-start ranges are conservative starting
points; confirm/tune each against the A4000 SIL (some tight geometries may need
a slightly larger start range to reach the 926 m ship-domain).

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

SHIP_DOMAIN_M = 926.0  # 0.5 NM ship-domain radius — the pass DCPA floor for every probe

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
    rel_brg_deg: float, range_m: float, own_hdg_deg: float,
    cog_deg: float, sog_kn: float, own_sog_kn: float,
) -> dict:
    """Place a target at a bearing/range with an explicit course/speed (no solve).

    Used where the collision-solver is degenerate — a pure head-on (target COG
    exactly anti-parallel to the bearing) or an overtaking (same-course target
    nearly dead ahead). t_cpa is derived from the straight-line relative motion."""
    abs_brg = (own_hdg_deg + rel_brg_deg) % 360.0
    ux, uy = _unit_from_nav_deg(abs_brg)
    tx, ty = range_m * ux, range_m * uy
    lat, lon = enu_to_latlon(tx, ty)
    ovx, ovy = _unit_from_nav_deg(own_hdg_deg)
    tvx, tvy = _unit_from_nav_deg(cog_deg)
    vrx = tvx * sog_kn * KN - ovx * own_sog_kn * KN
    vry = tvy * sog_kn * KN - ovy * own_sog_kn * KN
    vv = vrx * vrx + vry * vry
    t_cpa = max(0.0, -(tx * vrx + ty * vry) / vv) if vv > 1e-9 else 0.0
    return {"lat": round(lat, 6), "lon": round(lon, 6),
            "cog": round(cog_deg, 1), "sog": round(sog_kn, 2),
            "t_cpa_s": round(t_cpa, 1), "rel_brg": rel_brg_deg}


def _nominal_route(hdg: float, sog: float) -> list[dict]:
    """Straight 15-min dead-reckoned track along the initial heading.

    Gives the bridge a stable line to rejoin after avoidance (so route-return is
    testable) instead of the rolling mock_l2 default route."""
    dist = sog * KN * 900.0  # 15 min ahead
    ex, ny = _unit_from_nav_deg(hdg)
    lat1, lon1 = enu_to_latlon(dist * ex, dist * ny)
    return [
        {"latitude": LAT0, "longitude": LON0, "target_sog_kn": sog},
        {"latitude": round(lat1, 6), "longitude": round(lon1, 6), "target_sog_kn": sog},
    ]


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
        "nominalRoute": _nominal_route(hdg, sog),
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
    avoidance_time_s: float, avoidance_delta_rad: float,
    avoidance_duration_s: float, total_time: float, n_rps: float,
    cpa_min_m: float = SHIP_DOMAIN_M,
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
            "scenario_source": "colregs_probe_suite_v2.0",
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
    """The 8 single-purpose fast probes. Close-start (~2 NM, total_time ~5 min vs
    the old 15 min), DCPA~=0, cpa_min = 926 m ship-domain for give-way probes
    (500 m for the stand-on probe — last-moment 17(b) clears less). Multi-ship
    belongs to the Imazu-22 benchmark, not here."""
    scenarios: dict[str, dict] = {}

    # ── P1  Rule 14 head-on, pure reciprocal (canonical) ────────────────────
    # Pure head-on is degenerate for the collision-solver (target COG anti-parallel
    # to the bearing) → place directly with reciprocal course at own speed (DCPA=0).
    t = straight_target(rel_brg_deg=0.0, range_m=2.0 * NM, own_hdg_deg=0.0,
                        cog_deg=180.0, sog_kn=12.0, own_sog_kn=12.0)
    scenarios["colreg-rule14-ho"] = _scenario(
        scenario_id="colreg-rule14-ho-v2.0",
        title="Probe: Rule 14 head-on (pure reciprocal)",
        description=(
            "Rule 14 PURE head-on. Own heading 000deg/12kn, target dead ahead "
            f"(relative bearing 0deg), reciprocal course 180deg at {t['sog']:.1f}kn, "
            f"initial range 2.0 NM, t_cpa ~= {t['t_cpa_s']:.0f}s. DCPA ~= 0 without action. "
            "Own ship must alter to STARBOARD and pass port-to-port, then return to track. "
            "Canonical close-start probe (the M6 onset-latch regression baseline)."
        ),
        own_hdg=0.0, own_sog=12.0, targets=[t],
        rule="Rule14", give_way="own", expected_action="turn_starboard",
        colregs_rules=["R14", "R8"],
        rule_compliance=[{"rule": "Rule14", "result": "required"}],
        avoidance_time_s=25.0,
        avoidance_delta_rad=1.0472, avoidance_duration_s=200.0,
        total_time=300.0, n_rps=3.5, seed=101,
    )

    # ── P2  Rule 14 head-on PORT edge (head-on / port-stand-on boundary) ────
    # Target ~15deg to PORT of the bow on a near-reciprocal course — near the edge
    # of the head-on cone, where a misclassification would treat it as a port-side
    # CROSSING (own = stand-on -> hold course) instead of head-on (own must turn
    # starboard). The M6 fishtail lived at exactly this kind of boundary.
    t = solve_collision_target(own_hdg_deg=0.0, own_sog_kn=12.0,
                               rel_brg_deg=355.0, target_cog_deg=170.0, range_m=2.0 * NM)
    scenarios["colreg-rule14-ho-port"] = _scenario(
        scenario_id="colreg-rule14-ho-port-v2.0",
        title="Probe: Rule 14 head-on PORT-biased (must still turn starboard)",
        description=(
            "Rule 14 head-on BOUNDARY probe. Target ~5deg to PORT of own bow "
            f"(relative bearing 355deg) on a near-reciprocal course 170deg ({t['sog']:.1f}kn) "
            "— inside the head-on sector but biased to port. Rule 14 still mandates a "
            "STARBOARD alteration even when the other vessel is marginally to port; turning "
            "to port (toward the target) or holding course as if crossing is a violation. "
            "Stresses that a port-biased head-on is not mis-handled."
        ),
        own_hdg=0.0, own_sog=12.0, targets=[t],
        rule="Rule14", give_way="own", expected_action="turn_starboard",
        colregs_rules=["R14", "R8"],
        rule_compliance=[{"rule": "Rule14", "result": "required"}],
        avoidance_time_s=25.0,
        avoidance_delta_rad=1.0472, avoidance_duration_s=200.0,
        total_time=300.0, n_rps=3.5, seed=102,
    )

    # ── P3  Rule 13 overtaking (slow vessel nearly dead ahead) ──────────────
    # Own (14kn) overtakes a 7kn vessel just off the bow. Rule 13(d): once
    # classified as overtaking, the bearing drawing to the side as own passes must
    # NOT reclassify the encounter into a crossing (Phase-B behavioral assertion).
    t = straight_target(rel_brg_deg=3.0, range_m=0.9 * NM, own_hdg_deg=0.0,
                        cog_deg=0.0, sog_kn=7.0, own_sog_kn=14.0)
    scenarios["colreg-rule13-ot"] = _scenario(
        scenario_id="colreg-rule13-ot-v2.0",
        title="Probe: Rule 13 overtaking",
        description=(
            "Rule 13 overtaking. Own ship 000deg/14kn overtakes a slow 7kn vessel nearly "
            "dead ahead (relative bearing 3deg, same course), initial range 0.6 NM. Own is "
            "the give-way vessel and must keep clear (conventionally a STARBOARD pass) until "
            "finally past and clear. Rule 13(d): the bearing drawing aft as own passes must "
            "NOT reclassify this into a crossing (assert classification stability in Phase B)."
        ),
        own_hdg=0.0, own_sog=14.0, targets=[t],
        rule="Rule13", give_way="own", expected_action="turn_starboard",
        colregs_rules=["R13", "R8"],
        rule_compliance=[{"rule": "Rule13", "result": "required"}],
        avoidance_time_s=20.0, avoidance_delta_rad=0.6981, avoidance_duration_s=320.0,
        total_time=420.0, n_rps=4.2, seed=103,
    )

    # ── P4  Rule 15 crossing give-way (target from starboard bow) ───────────
    t = solve_collision_target(own_hdg_deg=0.0, own_sog_kn=12.0,
                               rel_brg_deg=50.0, target_cog_deg=290.0, range_m=2.0 * NM)
    scenarios["colreg-rule15-cs"] = _scenario(
        scenario_id="colreg-rule15-cs-v2.0",
        title="Probe: Rule 15 crossing give-way (starboard)",
        description=(
            "Rule 15 crossing. Target crosses from own ship's STARBOARD bow (relative "
            f"bearing 50deg), course 290deg at {t['sog']:.1f}kn, t_cpa ~= {t['t_cpa_s']:.0f}s. "
            "Own ship has the target on its starboard side -> own is the give-way vessel "
            "(Rule 15/16): alter to STARBOARD and pass astern, then return to track. "
            "DCPA ~= 0 without action."
        ),
        own_hdg=0.0, own_sog=12.0, targets=[t],
        rule="Rule15_Stbd", give_way="own", expected_action="turn_starboard",
        colregs_rules=["R15", "R16", "R8"],
        rule_compliance=[{"rule": "Rule15", "result": "required"},
                         {"rule": "Rule16", "result": "required"}],
        avoidance_time_s=25.0,
        avoidance_delta_rad=1.0472, avoidance_duration_s=200.0,
        total_time=300.0, n_rps=3.5, seed=104,
    )

    # ── P5  Rule 15 crossing give-way, SHORT t_cpa (reaction-time probe) ────
    t = solve_collision_target(own_hdg_deg=0.0, own_sog_kn=12.0,
                               rel_brg_deg=60.0, target_cog_deg=300.0, range_m=1.8 * NM)
    scenarios["colreg-rule15-cs-2"] = _scenario(
        scenario_id="colreg-rule15-cs-2-v2.0",
        title="Probe: Rule 15 crossing give-way, short t_cpa",
        description=(
            "Rule 15 crossing give-way with a SHORT reaction window. Faster target from the "
            f"starboard bow (relative bearing 60deg), course 300deg at {t['sog']:.1f}kn, "
            f"initial range 1.0 NM, t_cpa ~= {t['t_cpa_s']:.0f}s — forces an EARLY, decisive "
            "starboard alteration (Rule 8(b): one readily-apparent maneuver). Probes whether "
            "the give-way action triggers promptly enough to keep the ship-domain clear."
        ),
        own_hdg=0.0, own_sog=12.0, targets=[t],
        rule="Rule15_Stbd", give_way="own", expected_action="turn_starboard",
        colregs_rules=["R15", "R16", "R8"],
        rule_compliance=[{"rule": "Rule15", "result": "required"},
                         {"rule": "Rule16", "result": "required"}],
        avoidance_time_s=20.0,
        avoidance_delta_rad=1.0472, avoidance_duration_s=180.0,
        total_time=260.0, n_rps=3.5, seed=105,
    )

    # ── P6  Head-on / crossing edge (just OUTSIDE the head-on cone) ─────────
    # rel_brg 25deg: just past the head-on sector -> a starboard CROSSING give-way,
    # not a head-on. Pairs with P2 to bracket the head-on/crossing boundary.
    t = solve_collision_target(own_hdg_deg=0.0, own_sog_kn=12.0,
                               rel_brg_deg=25.0, target_cog_deg=215.0, range_m=2.0 * NM)
    scenarios["colreg-rule15-cs-edge"] = _scenario(
        scenario_id="colreg-rule15-cs-edge-v2.0",
        title="Probe: head-on/crossing edge (rel_brg 25deg, starboard)",
        description=(
            "Classification-boundary probe at the HEAD-ON / CROSSING edge. Target just "
            f"OUTSIDE the head-on cone on the starboard bow (relative bearing 25deg), course "
            f"215deg at {t['sog']:.1f}kn. Should be handled as a starboard crossing give-way "
            "(Rule 15), NOT a head-on — but the geometry is close enough to the cone that a "
            "misclassification would flip the maneuver. Brackets the boundary together with "
            "colreg-rule14-ho-port (the inside-cone edge)."
        ),
        own_hdg=0.0, own_sog=12.0, targets=[t],
        rule="Rule15_Stbd", give_way="own", expected_action="turn_starboard",
        colregs_rules=["R15", "R16", "R8"],
        rule_compliance=[{"rule": "Rule15", "result": "required"},
                         {"rule": "Rule16", "result": "required"}],
        # Boundary geometry: a fixed starboard turn clears less than a clean crossing,
        # so the floor is a conservative 500 m (the real MPC should exceed it on A4000).
        cpa_min_m=500.0,
        avoidance_time_s=25.0,
        avoidance_delta_rad=1.0472, avoidance_duration_s=200.0,
        total_time=300.0, n_rps=3.5, seed=106,
    )

    # ── P7  Crossing / overtaking edge (rel_brg ~108deg, starboard quarter) ─
    t = solve_collision_target(own_hdg_deg=0.0, own_sog_kn=10.0,
                               rel_brg_deg=108.0, target_cog_deg=300.0, range_m=2.0 * NM)
    scenarios["colreg-rule15-ot-boundary"] = _scenario(
        scenario_id="colreg-rule15-ot-boundary-v2.0",
        title="Probe: crossing/overtaking edge (rel_brg 108deg)",
        description=(
            "Classification-boundary probe at the CROSSING / OVERTAKING edge (the 112.5deg "
            "two-points-abaft-the-beam line). Target on the starboard quarter (relative "
            f"bearing 108deg), course 300deg at {t['sog']:.1f}kn — just inside the crossing "
            "sector. Own remains the give-way vessel (Rule 15); the encounter must not flip "
            "to/from overtaking as the bearing drifts across the boundary. DCPA ~= 0."
        ),
        own_hdg=0.0, own_sog=10.0, targets=[t],
        rule="Rule15_Stbd", give_way="own", expected_action="turn_starboard",
        colregs_rules=["R15", "R16", "R8"],
        rule_compliance=[{"rule": "Rule15", "result": "required"},
                         {"rule": "Rule16", "result": "required"}],
        # Boundary geometry (quarter target): conservative 500 m floor; A4000 MPC
        # should exceed it. The probe's purpose is correct classification at the edge.
        cpa_min_m=500.0,
        avoidance_time_s=25.0,
        avoidance_delta_rad=1.0472, avoidance_duration_s=240.0,
        total_time=320.0, n_rps=3.0, seed=107,
    )

    # ── P8  Rule 17 stand-on (target crosses from port; never yields) ───────
    # Own is STAND-ON. The give-way target (straight-line replay) never acts, so own
    # must hold course/speed (17(a)(i)) then take last-moment independent action
    # (17(b)) to keep the ship-domain clear. Probes that own does NOT give way early.
    t = solve_collision_target(own_hdg_deg=0.0, own_sog_kn=10.0,
                               rel_brg_deg=315.0, target_cog_deg=90.0, range_m=2.0 * NM)
    scenarios["colreg-rule17-cr-so"] = _scenario(
        scenario_id="colreg-rule17-cr-so-v2.0",
        title="Probe: Rule 17 stand-on (target from port, never yields)",
        description=(
            "Rule 17 stand-on. Target crosses from own ship's PORT bow (relative bearing "
            f"315deg), course 090deg at {t['sog']:.1f}kn, t_cpa ~= {t['t_cpa_s']:.0f}s. Own "
            "has the target on its PORT side -> own is the STAND-ON vessel and must initially "
            "KEEP course and speed (Rule 17(a)(i)). The give-way target holds course "
            "(straight-line replay) and never acts, so own must take last-moment independent "
            "action (Rule 17(b)) to keep the ship-domain clear. Probes that own does NOT "
            "give way prematurely (no early large alteration)."
        ),
        own_hdg=0.0, own_sog=10.0, targets=[t],
        rule="Rule17", give_way="target", expected_action="maintain",
        colregs_rules=["R17", "R15", "R8"],
        rule_compliance=[{"rule": "Rule17", "result": "required"},
                         {"rule": "Rule15", "result": "required"}],
        # Stand-on: a last-moment Rule 17(b) maneuver cannot clear a full ship-domain,
        # so the pass floor is lower than the give-way probes' 926 m (still a valid,
        # non-zero, justified safe distance).
        cpa_min_m=500.0,
        avoidance_time_s=max(30.0, t["t_cpa_s"] - 110.0),
        avoidance_delta_rad=0.8727, avoidance_duration_s=140.0,
        total_time=360.0, n_rps=3.0, seed=108,
    )

    return scenarios


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    scenarios = build_all()
    keep = {f"{stem}.yaml" for stem in scenarios}
    # Clean-regen: remove stale colreg-*.yaml no longer in the probe set so this
    # generator is the single source of truth for the directory.
    removed = []
    for old in OUT_DIR.glob("colreg-*.yaml"):
        if old.name not in keep:
            old.unlink()
            removed.append(old.name)
    for stem, doc in scenarios.items():
        path = OUT_DIR / f"{stem}.yaml"
        path.write_text(yaml.safe_dump(doc, sort_keys=False, allow_unicode=True))
        print(f"wrote {path.relative_to(OUT_DIR.parents[1])}")
    for name in sorted(removed):
        print(f"removed stale {name}")
    print(f"\n{len(scenarios)} probe scenarios generated, {len(removed)} stale removed.")


if __name__ == "__main__":
    main()
