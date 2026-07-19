"""FAST lifecycle boundary state machine.

Replaces the legacy window-count stop with a deterministic lifecycle boundary
derived solely from the M4 ``behavior_plan`` stream. The detector observes the
behavior-mode lifecycle:

  TRANSIT (>= min_transit_s stable prefix) -> AVOIDANCE -> first RECOVERY

and stops at the FIRST M4 RECOVERY transition, including that boundary row.

Contract (FAST):
- Only ``behavior=1`` (COLREG_AVOID) counts as AVOIDANCE.
- DP_HOLD (2), BERTH (3), and MRC {4,5,6} are never avoidance aliases.
- Any MRC behavior 4/5/6 before RECOVERY terminates the run early with a
  specific RED (a safety intervention is NOT a normal Clean-8 avoidance PASS).
- Recovery (7) without prior avoidance is RED ``AVOIDANCE_NOT_REACHED``.
- Timeout (avoidance observed but no first recovery) is RED
  ``RECOVERY_BOUNDARY_NOT_REACHED`` (non-terminal: the horizon ran out).
- Only ``FIRST_M4_RECOVERY`` is a PASS boundary; it is immutable to any later
  behavior chatter.

The detector is pure Python and does not read the live stack. It consumes
trace rows in the canonical L3 schema and uses :func:`event_time_s` for time
ordering so it is consistent with every other L3/GNC timeline consumer.
"""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from typing import Any

from tools.sil.trace_time import event_time_s


# M4 behavior_plan coarse-mode labels (see behavior_plan message).
TRANSIT = 0
AVOIDANCE = 1
DP_HOLD = 2
BERTH = 3
MRC = {4, 5, 6}
RECOVERY = 7

# Stop reasons emitted by the detector. ``FIRST_M4_RECOVERY`` is the sole
# PASS boundary; every other terminal reason is RED. ``terminal`` means a
# physical stop sample was observed (early RED or PASS). The timeout codes
# (``RECOVERY_BOUNDARY_NOT_REACHED`` / ``AVOIDANCE_NOT_REACHED`` when avoidance
# was never observed) are non-terminal: the scenario horizon ran out.
FIRST_M4_RECOVERY = "FIRST_M4_RECOVERY"
TRANSIT_NOT_OBSERVED = "TRANSIT_NOT_OBSERVED"
TRANSIT_PREFIX_TOO_SHORT = "TRANSIT_PREFIX_TOO_SHORT"
NON_COLREGS_BEHAVIOR_BEFORE_AVOIDANCE = "NON_COLREGS_BEHAVIOR_BEFORE_AVOIDANCE"
NON_COLREGS_BEHAVIOR_INTERRUPTED_AVOIDANCE = (
    "NON_COLREGS_BEHAVIOR_INTERRUPTED_AVOIDANCE"
)
AVOIDANCE_NOT_REACHED = "AVOIDANCE_NOT_REACHED"
MRC_BEFORE_RECOVERY = "MRC_BEFORE_RECOVERY"
AVOIDANCE_ABORTED_BEFORE_RECOVERY = "AVOIDANCE_ABORTED_BEFORE_RECOVERY"
RECOVERY_BOUNDARY_NOT_REACHED = "RECOVERY_BOUNDARY_NOT_REACHED"
ENCOUNTER_CLEAR_WITH_OWN_HOLD = "ENCOUNTER_CLEAR_WITH_OWN_HOLD"
ENCOUNTER_CLEAR_NOT_REACHED = "ENCOUNTER_CLEAR_NOT_REACHED"
OWN_TACTICAL_TAKEOVER_FOR_TARGET_RESOLUTION = (
    "OWN_TACTICAL_TAKEOVER_FOR_TARGET_RESOLUTION"
)

# Every stop_reason / failure_code the detector can emit. The runner predicate
# uses this to decide fast-scope verdict handling without parsing prefixes.
ALL_BOUNDARY_REASONS = frozenset({
    FIRST_M4_RECOVERY,
    TRANSIT_NOT_OBSERVED,
    TRANSIT_PREFIX_TOO_SHORT,
    NON_COLREGS_BEHAVIOR_BEFORE_AVOIDANCE,
    NON_COLREGS_BEHAVIOR_INTERRUPTED_AVOIDANCE,
    AVOIDANCE_NOT_REACHED,
    MRC_BEFORE_RECOVERY,
    AVOIDANCE_ABORTED_BEFORE_RECOVERY,
    RECOVERY_BOUNDARY_NOT_REACHED,
    ENCOUNTER_CLEAR_WITH_OWN_HOLD,
    ENCOUNTER_CLEAR_NOT_REACHED,
    OWN_TACTICAL_TAKEOVER_FOR_TARGET_RESOLUTION,
})

# The terminal early-RED reasons: a physical behavior sample terminated the run
# before (or instead of) the first recovery. These never count as a PASS and
# avoid wasting the rest of the scenario horizon.
TERMINAL_REASONS = frozenset({
    FIRST_M4_RECOVERY,
    TRANSIT_NOT_OBSERVED,
    TRANSIT_PREFIX_TOO_SHORT,
    NON_COLREGS_BEHAVIOR_BEFORE_AVOIDANCE,
    NON_COLREGS_BEHAVIOR_INTERRUPTED_AVOIDANCE,
    MRC_BEFORE_RECOVERY,
    AVOIDANCE_ABORTED_BEFORE_RECOVERY,
    OWN_TACTICAL_TAKEOVER_FOR_TARGET_RESOLUTION,
})


@dataclass(frozen=True)
class FastBoundary:
    """Result of a FAST lifecycle boundary scan.

    Attributes:
        ready: True iff the FIRST_M4_RECOVERY PASS boundary was observed.
        transit_start_t: canonical time of the first stable TRANSIT sample.
        avoidance_start_t: canonical time of the first COLREG_AVOID sample.
        recovery_start_t: canonical time of the first RECOVERY transition.
        stop_t: canonical time the run should stop (PASS or terminal RED);
            None when the horizon ran out before any terminal sample.
        stop_reason: short label for the stop (PASS or terminal RED);
            None for the non-terminal timeout case.
        failure_code: RED reason string (None on PASS). For the non-terminal
            timeout case this is the deterministic failure code the runner
            must report.
    """

    ready: bool
    transit_start_t: float | None
    avoidance_start_t: float | None
    recovery_start_t: float | None
    stop_t: float | None
    stop_reason: str | None
    failure_code: str | None

    @property
    def terminal(self) -> bool:
        """True when a physical stop sample (PASS or RED) was reached."""
        return self.stop_t is not None

    @property
    def transit_duration_s(self) -> float:
        """Duration of the stable TRANSIT prefix; 0.0 until avoidance seen."""
        if (
            self.stop_reason == ENCOUNTER_CLEAR_WITH_OWN_HOLD
            and self.transit_start_t is not None
            and self.recovery_start_t is not None
        ):
            return self.recovery_start_t - self.transit_start_t
        if self.transit_start_t is None or self.avoidance_start_t is None:
            return 0.0
        return self.avoidance_start_t - self.transit_start_t


def _behavior_of(row: Mapping[str, Any]) -> int:
    """Return the coarse behavior label of an M4 behavior_plan row."""
    try:
        return int(row.get("behavior", -1))
    except (TypeError, ValueError):
        return -1


def _m6_encounter_active(row: Mapping[str, Any]) -> bool:
    try:
        encounter_state = int(row.get("encounter_state", 0) or 0)
    except (TypeError, ValueError):
        encounter_state = 0
    return (
        bool(row.get("conflict_detected", False))
        or bool(row.get("active_rules") or [])
        or encounter_state in {1, 2}
    )


def find_fast_boundary(
    rows: list[Mapping[str, Any]],
    min_transit_s: float = 60.0,
    terminal: str = "OWN_RECOVERY_ENTRY",
    clear_dwell_s: float = 10.0,
) -> FastBoundary:
    """Scan *rows* for the FAST lifecycle boundary.

    Only ``/l3/m4/behavior_plan`` rows participate. Events are ordered by
    canonical event time (:func:`event_time_s`), so unsorted or interleaved
    trace input is handled deterministically.

    Returns a :class:`FastBoundary` describing the observed lifecycle and the
    deterministic stop / failure code.
    """
    if terminal == ENCOUNTER_CLEAR_WITH_OWN_HOLD:
        return _find_target_resolution_boundary(
            rows,
            min_transit_s=min_transit_s,
            clear_dwell_s=clear_dwell_s,
        )
    if terminal != "OWN_RECOVERY_ENTRY":
        raise ValueError(f"unsupported FAST terminal: {terminal}")

    events = sorted(
        (row for row in rows if row.get("topic") == "/l3/m4/behavior_plan"),
        key=event_time_s,
    )
    transit_start: float | None = None
    transit_last: float | None = None
    avoidance_start: float | None = None
    for row in events:
        t_s = event_time_s(row)
        behavior = _behavior_of(row)
        if avoidance_start is None:
            # --- Pre-avoidance phase: observe a stable TRANSIT prefix. ---
            if behavior == TRANSIT:
                if transit_start is None:
                    transit_start = t_s
                transit_last = t_s
                continue
            if behavior == AVOIDANCE:
                if transit_start is None or transit_last is None:
                    return FastBoundary(
                        False, transit_start, None, None, t_s,
                        TRANSIT_NOT_OBSERVED, TRANSIT_NOT_OBSERVED,
                    )
                if transit_last - transit_start < min_transit_s:
                    return FastBoundary(
                        False, transit_start, t_s, None, t_s,
                        TRANSIT_PREFIX_TOO_SHORT, TRANSIT_PREFIX_TOO_SHORT,
                    )
                avoidance_start = t_s
                continue
            if behavior in {DP_HOLD, BERTH, *MRC}:
                # DP_HOLD / BERTH / MRC are never COLREG_AVOID aliases.
                return FastBoundary(
                    False, transit_start, None, None, t_s,
                    NON_COLREGS_BEHAVIOR_BEFORE_AVOIDANCE,
                    NON_COLREGS_BEHAVIOR_BEFORE_AVOIDANCE,
                )
            if behavior == RECOVERY:
                # Recovery observed before any avoidance -> cannot PASS.
                return FastBoundary(
                    False, transit_start, None, t_s, t_s,
                    AVOIDANCE_NOT_REACHED, AVOIDANCE_NOT_REACHED,
                )
            continue
        # --- Avoidance phase: wait for the FIRST recovery transition. ---
        if behavior == RECOVERY:
            # Immutable first recovery: PASS boundary, includes this row.
            return FastBoundary(
                True, transit_start, avoidance_start, t_s, t_s,
                FIRST_M4_RECOVERY, None,
            )
        if behavior in MRC:
            # Coarse MRC before recovery: a safety intervention is not a PASS.
            return FastBoundary(
                False, transit_start, avoidance_start, None, t_s,
                MRC_BEFORE_RECOVERY, MRC_BEFORE_RECOVERY,
            )
        if behavior == TRANSIT:
            # Ownship returned to transit without ever recovering.
            return FastBoundary(
                False, transit_start, avoidance_start, None, t_s,
                AVOIDANCE_ABORTED_BEFORE_RECOVERY,
                AVOIDANCE_ABORTED_BEFORE_RECOVERY,
            )
        if behavior in {DP_HOLD, BERTH}:
            # A non-COLREGs mode interrupted avoidance before recovery.
            return FastBoundary(
                False, transit_start, avoidance_start, None, t_s,
                NON_COLREGS_BEHAVIOR_INTERRUPTED_AVOIDANCE,
                NON_COLREGS_BEHAVIOR_INTERRUPTED_AVOIDANCE,
            )
    # Horizon exhausted without a terminal sample.
    if avoidance_start is None:
        return FastBoundary(
            False, transit_start, None, None, None, None, AVOIDANCE_NOT_REACHED,
        )
    return FastBoundary(
        False, transit_start, avoidance_start, None, None, None,
        RECOVERY_BOUNDARY_NOT_REACHED,
    )


def _find_target_resolution_boundary(
    rows: list[Mapping[str, Any]],
    *,
    min_transit_s: float,
    clear_dwell_s: float = 10.0,
) -> FastBoundary:
    events = sorted(
        (
            row for row in rows
            if row.get("topic") in {
                "/l3/m4/behavior_plan",
                "/l3/m6/colregs_constraint",
            }
        ),
        key=event_time_s,
    )
    transit_start: float | None = None
    transit_last: float | None = None
    saw_conflict = False
    clear_since: float | None = None
    clear_dwell_ready = False
    for row in events:
        time_s = event_time_s(row)
        if row.get("topic") == "/l3/m4/behavior_plan":
            behavior = _behavior_of(row)
            if behavior == TRANSIT:
                if transit_start is None:
                    transit_start = time_s
                transit_last = time_s
                continue
            return FastBoundary(
                False,
                transit_start,
                None,
                None,
                time_s,
                OWN_TACTICAL_TAKEOVER_FOR_TARGET_RESOLUTION,
                OWN_TACTICAL_TAKEOVER_FOR_TARGET_RESOLUTION,
            )

        encounter_active = _m6_encounter_active(row)
        if encounter_active:
            saw_conflict = True
            clear_since = None
            clear_dwell_ready = False
            continue
        if not saw_conflict:
            continue
        if clear_since is None:
            clear_since = time_s
            continue
        if time_s - clear_since >= clear_dwell_s:
            clear_dwell_ready = True
            if (
                transit_start is not None
                and transit_last is not None
                and transit_last - transit_start >= min_transit_s
            ):
                return FastBoundary(
                    True,
                    transit_start,
                    None,
                    clear_since,
                    time_s,
                    ENCOUNTER_CLEAR_WITH_OWN_HOLD,
                    None,
                )
    if clear_dwell_ready and transit_start is None:
        failure_code = TRANSIT_NOT_OBSERVED
    elif clear_dwell_ready and (
        transit_last is None or transit_last - transit_start < min_transit_s
    ):
        failure_code = TRANSIT_PREFIX_TOO_SHORT
    else:
        failure_code = ENCOUNTER_CLEAR_NOT_REACHED
    return FastBoundary(
        False,
        transit_start,
        None,
        clear_since,
        None,
        None,
        failure_code,
    )


def is_fast_boundary_stop(early_stop_reason: Any) -> bool:
    """True when *early_stop_reason* is a FAST lifecycle boundary stop.

    Used by the runner to decide fast-scope verdict handling (relieve
    full-horizon route-return / overtake / full-phase metrics) for any run
    that stopped on the boundary detector rather than the full horizon.
    """
    return str(early_stop_reason or "") in ALL_BOUNDARY_REASONS
