"""Phase B — behavioral-stability scorer (fishtail / flap detector).

Pure-CPA scoring cannot catch behavioral TDL bugs: a geometric fallback can
still open the CPA while the rudder sawteeth, so CPA alone reads green. The M6
head-on fishtail (commit 21a640b5) is the motivating example — M6 re-classified
the give-way role mid-maneuver, so ``conflict_detected`` toggled, M4 behavior
flapped AVOID↔TRANSIT, M5 plan flapped VALID↔EMPTY, and the rudder sawtoothed,
yet the target still passed clear.

This module derives behavioral-stability KPIs from a single run's
``trace_current.jsonl`` records (already sliced to one run by the sim_t-backward
boundary) and turns them into per-check pass/fail assertions. It is the only
layer that catches fishtail/flap class bugs, and it puts a regression lock on
the M6 fix (revert the onset-latch → these assertions go red).

Design notes / data reality (verified against the live A4000 trace):
  - The trace records ``/sil/own_ship_state`` (heading_deg, **rot_deg_s** — the
    yaw rate, i.e. d(heading)/dt — but NOT rudder), ``/l3/m4/behavior_plan``
    (behavior, avoidance_active), ``/l3/m5/avoidance_plan`` (solver_status), and
    — once the bridge is patched — ``/l3/m6/colregs_constraint``
    (conflict_detected, primary_role).
  - The "rudder reversal" KPI is therefore computed from ROT sign reversals
    (the physical yaw oscillation that a fishtailing rudder produces).
  - The M6-derived checks (conflict_toggles, role_onset_stable) degrade
    gracefully to *not-applicable* when the M6 topic is absent from the trace,
    so old traces and a bridge that has not yet been patched still score.

Pure stdlib + no package-relative imports, so the A4000-host
``run_6_scenarios.py`` can import it directly (no polars/pyarrow / ROS2).

primary_role enum (matches l3_msgs/RuleActive.role):
    0 = STAND_ON, 1 = GIVE_WAY, 2 = BOTH_GIVE_WAY, 3 = FREE
"""
from __future__ import annotations

import statistics
from typing import Any, Dict, List, Optional, Tuple

OWN_SHIP = "/sil/own_ship_state"
BEHAVIOR = "/l3/m4/behavior_plan"
AVOID_PLAN = "/l3/m5/avoidance_plan"
COLREGS = "/l3/m6/colregs_constraint"

ROLE_FREE = 3

# Own-ship DUTY class per primary_role. GIVE_WAY (crossing) and BOTH_GIVE_WAY
# (head-on) impose the SAME duty on own ship — take positive avoiding action,
# keep clear, alter to starboard (COLREGs Rules 14/15/16; confirmed 🟢 via the
# project maritime_regulations notebook). The onset-latch (Rule 13(d)) exists to
# prevent the DANGEROUS re-classification — losing the give-way obligation to
# STAND_ON (Rule 17: maintain course → stop evading → collision) — NOT to forbid
# a benign GIVE_WAY→BOTH_GIVE_WAY refinement as the reciprocal course is
# confirmed. role_onset therefore asserts duty-class fixity, not raw-enum fixity.
ROLE_DUTY: Dict[Any, str] = {0: "stand_on", 1: "give_way", 2: "give_way", 3: "free"}

DEFAULT_THRESHOLDS: Dict[str, float] = {
    # downstream flap signals (always available from the trace)
    "max_behavior_toggles": 2,         # one rise (arm) + one fall (release)
    "max_plan_valid_segments": 2,      # one engagement (+1 tolerance)
    "max_steering_reversals": 4,       # give-way: turn-in, settle, return, settle
    "max_steering_reversals_standon": 2,
    "max_rot_hold_std_dps": 1.5,       # yaw-rate roughness during the hold
    # upstream cause signals (need /l3/m6/colregs_constraint in the trace)
    "max_conflict_toggles": 2,         # onset rise + past-clear fall
    "max_role_onset_changes": 0,       # Rule 13(d): onset role is fixed
    # role-specific
    "max_premature_giveway_deg": 10.0,  # stand-on must hold before 17(b)
    "min_give_way_turn_deg": 5.0,       # a give-way ship must actually alter
    # numerics
    "rot_deadband_dps": 0.2,            # ignore yaw-rate noise below this
    "hold_trim_frac": 0.25,             # drop turn-in/return tails for hold std
    "standon_hold_frac": 0.75,          # first 75 % of the run is the hold phase
}


# ── helpers ──────────────────────────────────────────────────────────────

def _by_topic(records: List[dict], topic: str) -> List[dict]:
    rs = [r for r in records if r.get("topic") == topic]
    rs.sort(key=lambda r: r.get("sim_t", 0.0))
    return rs


def _is_avoiding(r: dict) -> bool:
    av = r.get("avoidance_active")
    if av is None:
        return r.get("behavior", 0) != 0
    return bool(av)


def _count_transitions(seq: List[Any]) -> int:
    return sum(1 for a, b in zip(seq, seq[1:]) if a != b)


def _heading_dev_deg(heading_deg: float, init_heading_deg: float) -> float:
    """Signed deviation from the initial heading; +ve = starboard (clockwise)."""
    return (heading_deg - init_heading_deg + 180.0) % 360.0 - 180.0


def _engagement_window(m4: List[dict]) -> Optional[Tuple[float, float]]:
    avoiding = [r for r in m4 if _is_avoiding(r)]
    if not avoiding:
        return None
    return (avoiding[0].get("sim_t", 0.0), avoiding[-1].get("sim_t", 0.0))


# ── individual KPIs ──────────────────────────────────────────────────────

def _behavior_toggles(m4: List[dict]) -> int:
    return _count_transitions([_is_avoiding(r) for r in m4])


def _plan_valid_segments(m5: List[dict]) -> int:
    seg, prev = 0, False
    for r in m5:
        cur = r.get("solver_status") == "VALID"
        if cur and not prev:
            seg += 1
        prev = cur
    return seg


def _steering_reversals(oss: List[dict], deadband: float) -> int:
    signs: List[int] = []
    for r in oss:
        rot = r.get("rot_deg_s", 0.0) or 0.0
        if abs(rot) < deadband:
            continue
        s = 1 if rot > 0 else -1
        if not signs or signs[-1] != s:
            signs.append(s)
    return max(0, len(signs) - 1)


def _rot_hold_std(oss: List[dict], window: Optional[Tuple[float, float]],
                  trim_frac: float) -> float:
    """Std-dev of yaw rate during the steady hold (window middle, tails
    trimmed to drop the turn-in and return ramps). No window → whole run."""
    if window is None:
        vals = [r.get("rot_deg_s", 0.0) or 0.0 for r in oss]
    else:
        t0, t1 = window
        span = t1 - t0
        lo, hi = t0 + trim_frac * span, t1 - trim_frac * span
        vals = [r.get("rot_deg_s", 0.0) or 0.0
                for r in oss if lo <= r.get("sim_t", 0.0) <= hi]
    if len(vals) < 2:
        return 0.0
    return statistics.pstdev(vals)


def _conflict_toggles(m6: List[dict]) -> Optional[int]:
    if not m6:
        return None
    return _count_transitions([bool(r.get("conflict_detected")) for r in m6])


def _role_onset_changes(m6: List[dict]) -> Optional[int]:
    """Changes in own-ship DUTY CLASS across records where a conflict is detected.

    Rule 13(d): once classified at onset, the give-way obligation is held through
    the maneuver. A change of DUTY while ``conflict_detected`` is true — losing
    the give-way obligation to STAND_ON / FREE — is the dangerous re-classification
    bug. A benign GIVE_WAY→BOTH_GIVE_WAY refinement keeps the same duty and is NOT
    counted (see ROLE_DUTY). Returns None when M6 is absent or never flags a
    conflict."""
    conf = [r for r in m6 if bool(r.get("conflict_detected"))]
    if not conf:
        return None
    duties = [ROLE_DUTY.get(r.get("primary_role"), "unknown") for r in conf]
    return _count_transitions(duties)


def _peak_deviations(oss: List[dict], init_heading_deg: float) -> Tuple[float, float]:
    """(max starboard, max port) heading deviation magnitudes, both >= 0."""
    devs = [_heading_dev_deg(r.get("heading_deg", init_heading_deg), init_heading_deg)
            for r in oss]
    max_stbd = max([d for d in devs if d >= 0.0], default=0.0)
    max_port = -min([d for d in devs if d <= 0.0], default=0.0)
    return max_stbd, max_port


def _premature_giveway_deg(oss: List[dict], init_heading_deg: float,
                           hold_frac: float) -> Optional[float]:
    """Max |heading deviation| during the stand-on hold phase (the first
    ``hold_frac`` of the run). A large early alteration = premature give-way;
    a genuine last-moment Rule 17(b) action falls in the final tail and is
    excluded."""
    if not oss:
        return None
    t0 = oss[0].get("sim_t", 0.0)
    t1 = oss[-1].get("sim_t", 0.0)
    cutoff = t0 + hold_frac * (t1 - t0)
    hold = [r for r in oss if r.get("sim_t", 0.0) <= cutoff]
    if not hold:
        return 0.0
    return max(abs(_heading_dev_deg(r.get("heading_deg", init_heading_deg),
                                    init_heading_deg)) for r in hold)


# ── public API ───────────────────────────────────────────────────────────

def analyze_stability(
    records: List[dict],
    *,
    role: str,
    init_heading_deg: float = 0.0,
    thresholds: Optional[Dict[str, float]] = None,
) -> Dict[str, Any]:
    """Score one run's behavioral stability.

    Parameters
    ----------
    records : list of trace dicts for ONE run (sliced at the sim_t-backward
        boundary). Each has ``topic``, ``sim_t`` and topic-specific fields.
    role : ``"give_way"`` or ``"stand_on"`` (from scenario
        ``metadata.encounter.give_way_vessel`` — "own" → give_way, else stand_on).
    init_heading_deg : own ship's initial heading.
    thresholds : optional overrides for ``DEFAULT_THRESHOLDS`` (e.g. from a
        scenario's ``metadata.expected_outcome``).

    Returns a dict with ``kpis`` (raw values; None where not applicable),
    ``checks`` (per-assertion {value, threshold, pass, applicable}) and the
    overall ``stability_pass`` (all applicable checks pass).
    """
    th = dict(DEFAULT_THRESHOLDS)
    if thresholds:
        th.update({k: v for k, v in thresholds.items() if v is not None})

    is_give_way = role == "give_way"

    oss = _by_topic(records, OWN_SHIP)
    m4 = _by_topic(records, BEHAVIOR)
    m5 = _by_topic(records, AVOID_PLAN)
    m6 = _by_topic(records, COLREGS)

    window = _engagement_window(m4)

    behavior_toggles = _behavior_toggles(m4)
    plan_segments = _plan_valid_segments(m5)
    steering_reversals = _steering_reversals(oss, th["rot_deadband_dps"])
    rot_hold_std = _rot_hold_std(oss, window, th["hold_trim_frac"])
    conflict_toggles = _conflict_toggles(m6)
    role_onset_changes = _role_onset_changes(m6)
    max_stbd, max_port = _peak_deviations(oss, init_heading_deg)
    premature = (_premature_giveway_deg(oss, init_heading_deg, th["standon_hold_frac"])
                 if not is_give_way else None)

    rev_threshold = (th["max_steering_reversals"] if is_give_way
                     else th["max_steering_reversals_standon"])

    def chk(value, threshold, passed, applicable) -> Dict[str, Any]:
        return {"value": value, "threshold": threshold,
                "pass": bool(passed) if applicable else True,
                "applicable": bool(applicable)}

    checks: Dict[str, Dict[str, Any]] = {
        "behavior_toggles": chk(
            behavior_toggles, th["max_behavior_toggles"],
            behavior_toggles <= th["max_behavior_toggles"], True),
        "plan_valid_segments": chk(
            plan_segments, th["max_plan_valid_segments"],
            plan_segments <= th["max_plan_valid_segments"], True),
        "steering_reversals": chk(
            steering_reversals, rev_threshold,
            steering_reversals <= rev_threshold, True),
        "rot_hold_std": chk(
            round(rot_hold_std, 3), th["max_rot_hold_std_dps"],
            rot_hold_std <= th["max_rot_hold_std_dps"], True),
        "conflict_toggles": chk(
            conflict_toggles, th["max_conflict_toggles"],
            (conflict_toggles is not None
             and conflict_toggles <= th["max_conflict_toggles"]),
            conflict_toggles is not None),
        "role_onset_stable": chk(
            role_onset_changes, th["max_role_onset_changes"],
            (role_onset_changes is not None
             and role_onset_changes <= th["max_role_onset_changes"]),
            role_onset_changes is not None),
        "turn_starboard": chk(
            {"starboard_deg": round(max_stbd, 1), "port_deg": round(max_port, 1)},
            th["min_give_way_turn_deg"],
            (max_stbd >= max_port and max_stbd >= th["min_give_way_turn_deg"]),
            is_give_way),
        "premature_giveway": chk(
            None if premature is None else round(premature, 1),
            th["max_premature_giveway_deg"],
            (premature is not None and premature < th["max_premature_giveway_deg"]),
            (not is_give_way) and premature is not None),
    }

    stability_pass = all(c["pass"] for c in checks.values() if c["applicable"])

    return {
        "role": role,
        "stability_pass": stability_pass,
        "kpis": {
            "behavior_toggles": behavior_toggles,
            "plan_valid_segments": plan_segments,
            "steering_reversals": steering_reversals,
            "rot_hold_std_dps": round(rot_hold_std, 3),
            "conflict_toggles": conflict_toggles,
            "role_onset_changes": role_onset_changes,
            "premature_giveway_deg": None if premature is None else round(premature, 1),
            "max_starboard_dev_deg": round(max_stbd, 1),
            "max_port_dev_deg": round(max_port, 1),
            "engagement_window_s": None if window is None else [round(window[0], 1), round(window[1], 1)],
        },
        "checks": checks,
    }


def format_report(report: Dict[str, Any]) -> str:
    """Compact one-block summary for CLI runners (run_6 / a4000-acceptance)."""
    verdict = "PASS" if report["stability_pass"] else "FAIL"
    lines = [f"  Stability: {verdict}"]
    for name, c in report["checks"].items():
        if not c["applicable"]:
            lines.append(f"    - {name:<20} n/a")
            continue
        mark = "ok " if c["pass"] else "RED"
        lines.append(
            f"    - {name:<20} [{mark}] value={c['value']} thr={c['threshold']}")
    return "\n".join(lines)
