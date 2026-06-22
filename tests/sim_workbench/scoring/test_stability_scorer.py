"""Unit tests for Phase B behavioral-stability scorer.

The stability scorer consumes a single run's ``trace_current.jsonl`` records
(already sliced to one run) and derives behavioral-stability KPIs that catch
fishtail / flap class TDL bugs which the pure-CPA criterion misses (the M6
head-on fishtail — commit 21a640b5 — is the motivating example: a geometric
fallback can still open the CPA, so CPA alone reads green while the rudder
sawteeth).

``stability_scorer`` is imported standalone (no package ``__init__``) so the
A4000-host ``run_6_scenarios.py`` can import it without polars/pyarrow.
"""

import os
import sys

sys.path.insert(
    0,
    os.path.abspath(
        os.path.join(
            os.path.dirname(__file__),
            "../../../src/sim_workbench/sil_nodes/scoring/scoring",
        )
    ),
)

import stability_scorer as ss  # noqa: E402


# ── Synthetic-record builders ────────────────────────────────────────────

def _oss(sim_t, heading_deg, rot_deg_s):
    return {
        "topic": "/sil/own_ship_state", "sim_t": sim_t,
        "heading_deg": heading_deg, "rot_deg_s": rot_deg_s,
        "sog_kn": 12.0, "lat": 63.44, "lon": 10.38,
    }


def _m4(sim_t, behavior, avoidance_active=None):
    if avoidance_active is None:
        avoidance_active = behavior != 0
    return {
        "topic": "/l3/m4/behavior_plan", "sim_t": sim_t,
        "behavior": behavior, "avoidance_active": avoidance_active,
        "heading_min_deg": 0.0, "heading_max_deg": 0.0,
        "target_heading_deg": None,
    }


def _m5(sim_t, solver_status, n_waypoints=4):
    return {
        "topic": "/l3/m5/avoidance_plan", "sim_t": sim_t,
        "solver_status": solver_status, "n_waypoints": n_waypoints,
        "wp0_turn_radius_m": 500.0, "wp0_target_speed_kn": 12.0,
    }


def _m6(sim_t, conflict_detected, primary_role, phase="T_act"):
    return {
        "topic": "/l3/m6/colregs_constraint", "sim_t": sim_t,
        "conflict_detected": conflict_detected, "primary_role": primary_role,
        "phase": phase, "primary_preferred_direction": "STARBOARD",
    }


def test_m4_behavior_takes_precedence_over_stale_bridge_avoidance_active():
    assert ss._is_avoiding({"behavior": 0, "avoidance_active": True}) is False
    assert ss._is_avoiding({"behavior": 1, "avoidance_active": False}) is True
    assert ss._is_avoiding({"avoidance_active": True}) is True


def _clean_give_way_records():
    """Canonical head-on give-way: arm once, turn starboard 0→60°, hold, return.

    behavior 0→1→0 (2 toggles); one contiguous VALID segment; ROT one
    starboard pulse, ~0 hold, one port pulse on return (1 reversal); conflict
    false→true→false (2 toggles); role constant (BOTH_GIVE_WAY=2)."""
    recs = []
    # transit (t 0..9): heading 0, rot ~0
    for t in range(0, 10, 3):
        recs += [_oss(t, 0.0, 0.0), _m4(t, 0), _m5(t, "EMPTY"),
                 _m6(t, False, 3)]
    # onset + turn-in (t 10..28): heading 0→60, rot +3
    for t, h in [(10, 5.0), (16, 25.0), (22, 45.0), (28, 60.0)]:
        recs += [_oss(t, h, 3.0), _m4(t, 1), _m5(t, "VALID"), _m6(t, True, 2)]
    # hold (t 31..96): heading 60, rot ~0 (tiny noise)
    for i, t in enumerate(range(31, 97, 5)):
        noise = 0.05 if i % 2 == 0 else -0.05
        recs += [_oss(t, 60.0, noise), _m4(t, 1), _m5(t, "VALID"),
                 _m6(t, True, 2)]
    # past-clear + return (t 100..150): heading 60→0, rot -3
    for t, h in [(100, 50.0), (118, 25.0), (135, 5.0), (150, 0.0)]:
        recs += [_oss(t, h, -3.0), _m4(t, 0), _m5(t, "EMPTY"), _m6(t, False, 3)]
    # settled
    for t in range(155, 170, 5):
        recs += [_oss(t, 0.0, 0.0), _m4(t, 0), _m5(t, "EMPTY"), _m6(t, False, 3)]
    return recs


def _fishtail_give_way_records():
    """The M6 fishtail: M6 re-classifies mid-maneuver → conflict/role/behavior/
    plan all flap, rudder (ROT) sawteeth."""
    recs = []
    for i, t in enumerate(range(10, 130, 5)):
        avoiding = i % 2 == 0
        # role collapses give-way ↔ STAND_ON across episodes — the dangerous
        # re-classification (own loses the give-way duty mid-encounter).
        role = 1 if (i // 2) % 2 == 0 else 0
        recs += [
            _oss(t, 30.0, 4.0 if avoiding else -4.0),       # ROT sign saws
            _m4(t, 1 if avoiding else 0),                    # behavior flaps
            _m5(t, "VALID" if avoiding else "EMPTY"),        # plan flaps
            _m6(t, avoiding, role),                          # conflict+role flap
        ]
    return recs


def _clean_stand_on_records():
    """Stand-on: hold course (~0°) through the encounter, only a small
    last-moment (Rule 17(b)) alteration near the end."""
    recs = []
    for t in range(0, 250, 10):          # hold: heading ~0 (±1°), rot ~0
        h = 1.0 if (t // 10) % 2 == 0 else -1.0
        recs += [_oss(t, h % 360.0, 0.05), _m4(t, 0), _m5(t, "EMPTY"),
                 _m6(t, False, 0)]
    for t in range(255, 300, 5):         # last-moment 17(b): small starboard
        recs += [_oss(t, 18.0, 1.5), _m4(t, 1), _m5(t, "VALID"),
                 _m6(t, True, 0)]
    return recs


def _premature_stand_on_records():
    """Bug: stand-on ship gives way EARLY — 35° alteration in the hold phase."""
    recs = []
    for t in range(0, 200, 10):
        h = min(35.0, t * 0.5)           # early ramp to 35°
        recs += [_oss(t, h, 1.0), _m4(t, 1 if h > 5 else 0),
                 _m5(t, "VALID" if h > 5 else "EMPTY"), _m6(t, True, 0)]
    return recs


def _stand_on_independent_action_route_return_records():
    """Stand-on holds before Rule17 action, then avoids and returns to route."""
    recs = []
    for t in range(0, 320, 20):
        phase = "SOUND_WARNING" if t >= 100 else "PRESERVE_COURSE"
        recs += [_oss(t, 0.5, 0.0), _m4(t, 0), _m5(t, "EMPTY"),
                 _m6(t, False, 0, phase=phase)]
    for t, h, rot, phase in [
        (340, 12.0, 1.2, "INDEPENDENT_ACTION"),
        (390, 55.0, 1.8, "INDEPENDENT_ACTION"),
        (450, 35.0, -1.2, "INDEPENDENT_ACTION"),
        (610, 65.0, 1.4, "CRITICAL_ACTION"),
        (690, 55.0, -1.3, "PRESERVE_COURSE"),
        (850, 300.0, -1.5, "PRESERVE_COURSE"),
        (1000, 355.0, 1.4, "PRESERVE_COURSE"),
    ]:
        behavior = 1 if t < 690 else 0
        solver = "VALID" if behavior else "EMPTY"
        conflict = t < 690
        role = 0 if conflict else 3
        recs += [_oss(t, h, rot), _m4(t, behavior), _m5(t, solver),
                 _m6(t, conflict, role, phase=phase)]
    return recs


def _long_give_way_with_route_return_records():
    """Give-way avoidance followed by a long route-return turn.

    The COLREG maneuver is a clean starboard alteration. After M4 releases
    avoidance, the ship turns port to reacquire the route line. That recovery
    turn must not make the COLREG turn-direction check fail.
    """
    recs = []
    for t in range(0, 10, 2):
        recs += [_oss(t, 0.0, 0.0), _m4(t, 0), _m5(t, "EMPTY"),
                 _m6(t, False, 3)]
    for t, h in [(10, 8.0), (20, 28.0), (30, 48.0), (40, 55.0)]:
        recs += [_oss(t, h, 2.5), _m4(t, 1), _m5(t, "VALID"),
                 _m6(t, True, 2)]
    for t in range(50, 100, 10):
        recs += [_oss(t, 55.0, 0.02), _m4(t, 1), _m5(t, "VALID"),
                 _m6(t, True, 2)]
    recs += [_oss(110, 50.0, -1.5), _m4(110, 0), _m5(110, "EMPTY"),
             _m6(110, False, 3)]
    for t, h, rot in [(250, 15.0, -1.0), (400, 350.0, 1.0),
                      (700, 320.0, -1.0), (1000, 300.0, 1.0),
                      (1200, 0.0, -1.0)]:
        recs += [_oss(t, h, rot), _m4(t, 0), _m5(t, "EMPTY"),
                 _m6(t, False, 3)]
    # Late M6 chatter after the avoidance release should not affect the
    # avoidance-window conflict stability check.
    recs += [_oss(500, 340.0, -0.5), _m4(500, 0), _m5(500, "EMPTY"),
             _m6(500, True, 2)]
    recs += [_oss(540, 335.0, -0.5), _m4(540, 0), _m5(540, "EMPTY"),
             _m6(540, False, 3)]
    return recs


# ── Tests ────────────────────────────────────────────────────────────────

class TestCleanGiveWay:
    def test_clean_give_way_passes_all(self):
        rep = ss.analyze_stability(
            _clean_give_way_records(), role="give_way", init_heading_deg=0.0)
        assert rep["stability_pass"] is True, rep["checks"]
        assert rep["kpis"]["behavior_toggles"] <= 2
        assert rep["kpis"]["plan_valid_segments"] == 1
        assert rep["kpis"]["conflict_toggles"] == 2
        assert rep["kpis"]["role_onset_changes"] == 0

    def test_one_sample_behavior_release_blip_is_debounced(self):
        recs = _clean_give_way_records()
        recs += [
            _oss(80.0, 60.0, 0.0), _m4(80.0, 0), _m5(80.0, "VALID"),
            _m6(80.0, True, 2),
            _oss(80.2, 60.0, 0.0), _m4(80.2, 1), _m5(80.2, "VALID"),
            _m6(80.2, True, 2),
        ]

        rep = ss.analyze_stability(
            recs, role="give_way", init_heading_deg=0.0)

        assert rep["kpis"]["behavior_toggles"] <= 2

    def test_one_sample_plan_empty_blip_is_debounced(self):
        recs = _clean_give_way_records()
        recs += [
            _oss(80.0, 60.0, 0.0), _m4(80.0, 1), _m5(80.0, "EMPTY"),
            _m6(80.0, True, 2),
            _oss(80.2, 60.0, 0.0), _m4(80.2, 1), _m5(80.2, "VALID"),
            _m6(80.2, True, 2),
        ]

        rep = ss.analyze_stability(
            recs, role="give_way", init_heading_deg=0.0)

        assert rep["kpis"]["plan_valid_segments"] == 1
        assert rep["checks"]["plan_valid_segments"]["pass"] is True

    def test_small_hold_trim_dither_does_not_count_as_steering_reversal(self):
        recs = _clean_give_way_records()
        sign = 1.0
        for r in recs:
            if (r["topic"] == "/sil/own_ship_state" and
                    31 <= r["sim_t"] <= 96):
                r["rot_deg_s"] = sign * 0.3
                sign *= -1.0

        rep = ss.analyze_stability(
            recs, role="give_way", init_heading_deg=0.0)

        assert rep["checks"]["steering_reversals"]["pass"] is True
        assert rep["stability_pass"] is True, rep["checks"]

    def test_short_conflict_gap_does_not_count_as_toggle(self):
        recs = _clean_give_way_records()
        recs += [_m6(64.0, False, 3), _m6(64.5, True, 2)]

        rep = ss.analyze_stability(
            recs, role="give_way", init_heading_deg=0.0)

        assert rep["kpis"]["conflict_toggles"] == 2
        assert rep["checks"]["conflict_toggles"]["pass"] is True
        assert rep["stability_pass"] is True, rep["checks"]


class TestFishtailGiveWay:
    def test_fishtail_fails_stability(self):
        rep = ss.analyze_stability(
            _fishtail_give_way_records(), role="give_way", init_heading_deg=0.0)
        assert rep["stability_pass"] is False
        # the flap shows up across every downstream + upstream KPI
        assert rep["kpis"]["behavior_toggles"] > 2
        assert rep["kpis"]["plan_valid_segments"] > 2
        assert rep["kpis"]["steering_reversals"] > 4
        assert rep["kpis"]["conflict_toggles"] > 2
        assert rep["kpis"]["role_onset_changes"] > 0
        # at least one named check must report failed
        assert any(not c["pass"] for c in rep["checks"].values() if c["applicable"])


class TestStandOn:
    def test_clean_stand_on_passes(self):
        rep = ss.analyze_stability(
            _clean_stand_on_records(), role="stand_on", init_heading_deg=0.0)
        assert rep["stability_pass"] is True, rep["checks"]
        assert rep["kpis"]["premature_giveway_deg"] < 10.0

    def test_premature_giveway_fails(self):
        rep = ss.analyze_stability(
            _premature_stand_on_records(), role="stand_on", init_heading_deg=0.0)
        assert rep["stability_pass"] is False
        assert rep["kpis"]["premature_giveway_deg"] >= 10.0
        assert rep["checks"]["premature_giveway"]["applicable"] is True
        assert rep["checks"]["premature_giveway"]["pass"] is False

    def test_independent_action_and_route_return_are_not_premature_giveway(self):
        rep = ss.analyze_stability(
            _stand_on_independent_action_route_return_records(),
            role="stand_on",
            init_heading_deg=0.0,
        )
        assert rep["kpis"]["premature_giveway_deg"] < 10.0
        assert rep["checks"]["premature_giveway"]["pass"] is True


class TestDirection:
    def test_give_way_turning_port_fails(self):
        recs = _clean_give_way_records()
        # mirror the maneuver to PORT (negate every heading) — a give-way ship
        # turning to port is a Rule 14/15 violation.
        for r in recs:
            if r["topic"] == "/sil/own_ship_state":
                r["heading_deg"] = (-r["heading_deg"]) % 360.0
                r["rot_deg_s"] = -r["rot_deg_s"]
        rep = ss.analyze_stability(recs, role="give_way", init_heading_deg=0.0)
        assert rep["checks"]["turn_starboard"]["pass"] is False
        assert rep["stability_pass"] is False

    def test_route_return_port_turn_after_release_does_not_fail_starboard_check(self):
        recs = _long_give_way_with_route_return_records()
        rep = ss.analyze_stability(recs, role="give_way", init_heading_deg=0.0)
        assert rep["checks"]["turn_starboard"]["pass"] is True
        assert rep["checks"]["conflict_toggles"]["pass"] is True
        assert rep["checks"]["steering_reversals"]["pass"] is True
        assert rep["stability_pass"] is True, rep["checks"]

    def test_recovery_behavior_after_conflict_release_does_not_fail_starboard_check(self):
        recs = []
        recs += [_oss(0.0, 0.0, 0.0), _m4(0.0, 0), _m5(0.0, "EMPTY"),
                 _m6(0.0, False, 3)]
        for t, h, rot in [
            (120.0, 8.0, 1.0),
            (400.0, 28.0, 0.2),
            (900.0, 36.0, 0.0),
            (1500.0, 36.0, 0.0),
        ]:
            recs += [_oss(t, h, rot), _m4(t, 1), _m5(t, "VALID"),
                     _m6(t, True, 1)]
        for t, h, rot in [
            (1608.0, 32.0, -0.8),
            (1680.0, 10.0, -1.0),
            (1740.0, 355.0, -1.0),
            (1760.0, 315.3, -0.8),
        ]:
            recs += [_oss(t, h, rot), _m4(t, 7), _m5(t, "VALID"),
                     _m6(t, False, 3)]
        recs += [_oss(1800.0, 0.0, 0.0), _m4(1800.0, 0), _m5(1800.0, "EMPTY"),
                 _m6(1800.0, False, 3)]

        rep = ss.analyze_stability(recs, role="give_way", init_heading_deg=0.0)

        assert rep["checks"]["turn_starboard"]["pass"] is True
        assert rep["kpis"]["max_starboard_dev_deg"] == 36.0
        assert rep["stability_pass"] is True, rep["checks"]


class TestM6Optional:
    def test_missing_m6_topic_degrades_gracefully(self):
        recs = [r for r in _clean_give_way_records()
                if r["topic"] != "/l3/m6/colregs_constraint"]
        rep = ss.analyze_stability(recs, role="give_way", init_heading_deg=0.0)
        # M6-derived checks become not-applicable, not failures
        assert rep["checks"]["conflict_toggles"]["applicable"] is False
        assert rep["checks"]["role_onset_stable"]["applicable"] is False
        assert rep["kpis"]["conflict_toggles"] is None
        # downstream checks still evaluated → still passes
        assert rep["stability_pass"] is True, rep["checks"]


class TestRoleOnset:
    def test_duty_flip_to_standon_mid_conflict_fails(self):
        recs = _clean_give_way_records()
        # corrupt: own loses the give-way duty mid-conflict (role 2→0 STAND_ON) —
        # the dangerous re-classification (Rule 17: stop evading → collision).
        flipped = False
        for r in recs:
            if (r["topic"] == "/l3/m6/colregs_constraint"
                    and r["conflict_detected"] and not flipped and r["sim_t"] > 40):
                r["primary_role"] = 0  # STAND_ON — duty change
                flipped = True
        rep = ss.analyze_stability(recs, role="give_way", init_heading_deg=0.0)
        assert rep["kpis"]["role_onset_changes"] >= 1
        assert rep["checks"]["role_onset_stable"]["pass"] is False
        assert rep["stability_pass"] is False

    def test_giveway_to_bothgiveway_refinement_is_benign(self):
        # The real A4000 rule14-ho behavior: onset GIVE_WAY(1), then refine to
        # BOTH_GIVE_WAY(2) as the head-on is confirmed. Same own-ship duty
        # (keep clear, alter starboard) → NOT a re-classification. Must NOT fail.
        recs = _clean_give_way_records()
        flipped = False
        for r in recs:
            if (r["topic"] == "/l3/m6/colregs_constraint"
                    and r["conflict_detected"] and r["sim_t"] < 30):
                r["primary_role"] = 1  # onset as GIVE_WAY before refining to 2
                flipped = True
        assert flipped
        rep = ss.analyze_stability(recs, role="give_way", init_heading_deg=0.0)
        assert rep["kpis"]["role_onset_changes"] == 0
        assert rep["checks"]["role_onset_stable"]["pass"] is True
        assert rep["stability_pass"] is True


class TestEmpty:
    def test_empty_records_no_crash(self):
        rep = ss.analyze_stability([], role="give_way", init_heading_deg=0.0)
        assert "stability_pass" in rep
        assert isinstance(rep["checks"], dict)
