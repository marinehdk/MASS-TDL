"""Unit tests for the standalone DebugTraceWriter (sil_trace_writer.py).

A5c (f138b0d9) deleted sil_topic_bridge.py, which was the sole writer of
``runs/trace_current.jsonl`` — the file the orchestrator ``/debug/snapshot``
endpoint and the COLREGs probe ``get_sim_time()`` read from. sil_trace_adapter
(C++ DDS relay) never reimplemented the JSONL writer, so the snapshot was
permanently empty → probe always saw sim_t=0 → stuck-detector tripped.

These tests pin the writer's behaviour so the regression cannot silently
recur: record/reset/flush/rotation, and that subscribed-topic payloads land in
the JSONL with the exact field names the trace evaluators consume.
"""
from __future__ import annotations

import json
from pathlib import Path
from types import SimpleNamespace

import importlib.util
import sys

import pytest

_REPO_ROOT = Path(__file__).resolve().parents[2]
_MOD_PATH = _REPO_ROOT / "docker" / "sil_trace_writer.py"
_spec = importlib.util.spec_from_file_location("sil_trace_writer_under_test", _MOD_PATH)
assert _spec is not None and _spec.loader is not None
_module = importlib.util.module_from_spec(_spec)
sys.modules["sil_trace_writer_under_test"] = _module
_spec.loader.exec_module(_module)

DebugTraceWriter = _module.DebugTraceWriter
_normalize_colregs_constraint_msg = _module._normalize_colregs_constraint_msg
_normalize_world_state_msg = _module._normalize_world_state_msg
_normalize_gnc_execution_status_msg = _module._normalize_gnc_execution_status_msg
_normalize_avoidance_plan_msg = _module._normalize_avoidance_plan_msg
# Phase 1.3 (G-TR-2): new normalizers closing the trace blind spots.
_normalize_sat2_data_msg = _module._normalize_sat2_data_msg
_normalize_sat3_data_msg = _module._normalize_sat3_data_msg
_normalize_sotif_metrics_msg = _module._normalize_sotif_metrics_msg
_normalize_module_pulse_msg = _module._normalize_module_pulse_msg
_normalize_reactive_override_cmd_msg = _module._normalize_reactive_override_cmd_msg
_normalize_override_active_msg = _module._normalize_override_active_msg
_normalize_m7_observability_msg = _module._normalize_m7_observability_msg


class _FakeLogger:
    """Minimal stand-in for a ROS2 logger so DebugTraceWriter is testable off-ROS."""

    def __init__(self) -> None:
        self.errors: list[str] = []
        self.warnings: list[str] = []
        self.infos: list[str] = []

    def error(self, msg: str) -> None:
        self.errors.append(msg)

    def warning(self, msg: str) -> None:
        self.warnings.append(msg)

    def info(self, msg: str) -> None:
        self.infos.append(msg)


@pytest.fixture()
def trace_path(tmp_path: Path) -> Path:
    return tmp_path / "runs" / "trace_current.jsonl"


@pytest.fixture()
def writer(trace_path: Path) -> DebugTraceWriter:
    import os

    # DebugTraceWriter resolves its path from SIL_RUN_DIR at construction time.
    monkey_env = {"SIL_RUN_DIR": str(trace_path.parent)}
    old = {k: os.environ.get(k) for k in monkey_env}
    os.environ.update(monkey_env)
    try:
        return DebugTraceWriter(logger=_FakeLogger())
    finally:
        for k, v in old.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v


def _read_records(path: Path) -> list[dict]:
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]


# ── record / flush ──────────────────────────────────────────


def test_record_buffers_until_flush(writer: DebugTraceWriter, trace_path: Path) -> None:
    """Records are not visible on disk until flush() runs."""
    writer.record("/sil/own_ship_state", {"heading_deg": 12.0, "sog_kn": 5.0}, sim_t=1.0)
    assert not trace_path.exists() or trace_path.stat().st_size == 0
    writer.flush()
    rows = _read_records(trace_path)
    assert len(rows) == 1
    row = rows[0]
    assert row["topic"] == "/sil/own_ship_state"
    assert row["sim_t"] == 1.0
    assert row["heading_deg"] == 12.0
    assert "wall_t" in row  # audit field


def test_record_carries_all_supplied_fields(writer: DebugTraceWriter, trace_path: Path) -> None:
    writer.record(
        "/l3/m5/avoidance_plan",
        {"n_waypoints": 3, "solver_status": "VALID", "wp0_turn_radius_m": 120.0},
        sim_t=2.5,
    )
    writer.flush()
    row = _read_records(trace_path)[0]
    assert row["n_waypoints"] == 3
    assert row["solver_status"] == "VALID"
    assert row["wp0_turn_radius_m"] == 120.0


# ── reset (scenario ACTIVE truncation) ──────────────────────


def test_reset_truncates_stale_records(writer: DebugTraceWriter, trace_path: Path) -> None:
    """A new scenario ACTIVE must not inherit the previous run's records."""
    writer.record("/sil/own_ship_state", {"sog_kn": 1.0}, sim_t=100.0)
    writer.flush()
    assert len(_read_records(trace_path)) == 1

    writer.reset()  # simulates scenario ACTIVE transition
    writer.record("/sil/own_ship_state", {"sog_kn": 0.0}, sim_t=0.0)
    writer.flush()

    rows = _read_records(trace_path)
    assert len(rows) == 1
    assert rows[0]["sim_t"] == 0.0  # stale sim_t=100 record gone


def test_reset_is_safe_when_file_unwritable(writer: DebugTraceWriter, trace_path: Path) -> None:
    """reset() must not raise if the path is bad; it logs and keeps going."""
    import os

    bad = DebugTraceWriter.__new__(DebugTraceWriter)  # bypass __init__ path resolve
    bad._node = _FakeLogger()  # type: ignore[attr-defined]
    import collections
    import threading

    bad._lock = threading.Lock()  # type: ignore[attr-defined]
    bad._buf = collections.deque(maxlen=2000)  # type: ignore[attr-defined]
    bad._flush_timer = None  # type: ignore[attr-defined]
    bad._trace_path = Path("/nonexistent-root-dir-xyz/runs/trace_current.jsonl")  # type: ignore[attr-defined]
    bad._file = None  # type: ignore[attr-defined]
    bad.MAX_BUF = 2000  # type: ignore[attr-defined]
    bad.FLUSH_INTERVAL_S = 2.0  # type: ignore[attr-defined]
    # Should not raise even though the directory is missing.
    bad.reset()
    assert bad._node.errors  # logged the failure  # type: ignore[attr-defined]


# ── rotation (size cap) ─────────────────────────────────────


def test_flush_rotates_when_file_exceeds_size_cap(
    writer: DebugTraceWriter, trace_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    """Once the file crosses the size cap, flush gzips it and starts fresh."""
    # Shrink the cap so the test does not have to write 50 MB.
    monkeypatch.setattr(writer, "_rotate_size_bytes", 512)

    big = "x" * 400
    for i in range(5):
        writer.record("/sil/own_ship_state", {"blob": big}, sim_t=float(i))
        writer.flush()

    rotated = list(trace_path.parent.glob("trace_*.jsonl.gz"))
    assert rotated, "expected a gzipped rotation archive"
    # Current trace file must still exist and be writable.
    assert trace_path.exists()


def test_default_rotation_cap_keeps_clean_probe_sized_trace(
    writer: DebugTraceWriter, trace_path: Path
) -> None:
    """A normal long COLREGs probe trace must not rotate mid-run by default."""
    assert writer._file is not None
    writer._file.truncate(60 * 1024 * 1024)
    writer._file.flush()

    writer.record("/sil/own_ship_state", {"heading_deg": 12.0}, sim_t=1.0)
    writer.flush()

    assert not list(trace_path.parent.glob("trace_*.jsonl.gz"))


# ── close ───────────────────────────────────────────────────


def test_close_flushes_pending_buffer(writer: DebugTraceWriter, trace_path: Path) -> None:
    writer.record("/l3/m6/colregs_constraint", {"conflict_detected": True}, sim_t=3.0)
    writer.close()
    rows = _read_records(trace_path)
    assert len(rows) == 1
    assert rows[0]["conflict_detected"] is True


# ── subscribed-topic field contracts (what evaluators consume) ──


@pytest.mark.parametrize(
    "topic, payload",
    [
        (
            "/sil/own_ship_state",
            {"heading_deg": 1.0, "sog_kn": 2.0, "lat": 3.0, "lon": 4.0, "rot_deg_s": 0.1},
        ),
        (
            "/l3/m4/behavior_plan",
            {"behavior": 1, "avoidance_active": True, "target_heading_deg": 90.0},
        ),
        (
            "/l3/m5/avoidance_plan",
            {"n_waypoints": 2, "solver_status": "VALID", "wp0_turn_radius_m": 150.0},
        ),
        (
            "/l3/m5/avoidance_waypoints",
            {
                "plan_id": "m5-colregs-1",
                "behavior_mode": "avoidance",
                "n_waypoints": 3,
            },
        ),
        (
            "/l3/m6/colregs_constraint",
            {"conflict_detected": True, "primary_role": 1, "phase": "approach"},
        ),
        (
            "/l3/m2/world_state",
            {"target_count": 1, "primary_cpa_m": 120.0, "primary_tcpa_s": 60.0},
        ),
        ("/l3/checker/veto", {"checker_layer": "M7", "vetoed_module": "M5", "confidence": 0.9}),
        ("/sil/actuator_cmd", {"rudder_deg": 10.0, "throttle": 0.5}),
        ("/sil/scoring", {"safety": 1.0, "total": 0.8}),
        ("/l3/asdr/record", {"source_module": "M5", "decision_type": "avoid_wp"}),
        ("/l3/m3/mission_goal", {"fsm_state": 3, "task_validity": 1}),
        ("/l3/fsm_state", {"state": 3}),
        ("/l3/m7/safety_alert", {"alert_type": 0, "severity": 3, "recommended_mrm": "MRM-01"}),
        (
            "/l3/gnc/execution_status",
            {
                "accepted": True,
                "executing": True,
                "execution_state": "EXECUTING",
                "cross_track_error_m": 4.2,
            },
        ),
        ("/l2/planned_route", {"route_hash": "abc123"}),
    ],
)
def test_record_preserves_evaluator_field_names(
    writer: DebugTraceWriter, trace_path: Path, topic: str, payload: dict
) -> None:
    writer.record(topic, payload, sim_t=1.0)
    writer.flush()
    row = _read_records(trace_path)[0]
    for k, v in payload.items():
        assert row[k] == v, f"field {k} lost for topic {topic}"


def test_colregs_normalizer_preserves_active_rules() -> None:
    msg = SimpleNamespace(
        conflict_detected=True,
        primary_role=2,
        phase="SOUND_WARNING",
        primary_preferred_direction="STARBOARD",
        confidence=0.5,
        active_rules=[
            SimpleNamespace(
                rule_id=14,
                target_id=100000001,
                role=2,
                rule_phase="T_warn",
                preferred_direction="STARBOARD",
                min_alteration_deg=30.0,
                confidence=0.9,
            )
        ],
        # Phase 1.2 (G-M6-1): M6 lifecycle fields are part of the contract.
        encounter_state=2,  # ENCOUNTER_ACTIVE
        past_clear=False,
        release_predicted=False,
        colregs_chain_target_id="100000001",
    )

    payload = _normalize_colregs_constraint_msg(msg)

    assert payload["conflict_detected"] is True
    assert payload["active_rules"] == [
        {
            "rule_id": 14,
            "target_id": 100000001,
            "role": 2,
            "rule_phase": "T_warn",
            "preferred_direction": "STARBOARD",
            "min_alteration_deg": 30.0,
            "confidence": 0.9,
        }
    ]


def test_colregs_normalizer_preserves_encounter_lifecycle_fields() -> None:
    """Phase 1.2 (G-M6-1, spec v2.3 §15): M6 encounter lifecycle fields must
    be captured so future release/active debugging has direct trace evidence.
    The V2.3 phase 3b m6_not_past_clear × 874 root cause was only inferable
    via geometry back-projection because these fields were absent from trace.
    """
    # ACTIVE phase: encounter_state=2 (ENCOUNTER_ACTIVE), past_clear False,
    # release_predicted False. This is the state M5 TailBuilder needs to see
    # to enter the active-phase tail branch (the R8 fix unblocks this).
    msg_active = SimpleNamespace(
        conflict_detected=True,
        primary_role=2,
        phase="T_act",
        primary_preferred_direction="STARBOARD",
        confidence=0.9,
        active_rules=[],
        encounter_state=2,  # ENCOUNTER_ACTIVE
        past_clear=False,
        release_predicted=False,
        colregs_chain_target_id="100000001",
    )

    payload_active = _normalize_colregs_constraint_msg(msg_active)

    assert payload_active["encounter_state"] == 2
    assert payload_active["past_clear"] is False
    assert payload_active["release_predicted"] is False
    assert payload_active["colregs_chain_target_id"] == "100000001"

    # RELEASE phase: encounter_state=3 (ENCOUNTER_RELEASE), past_clear True.
    # M5 TailBuilder uses past_clear=True to enter the rejoin branch.
    msg_release = SimpleNamespace(
        conflict_detected=False,
        primary_role=0,
        phase="T_postAvoid",
        primary_preferred_direction="NONE",
        confidence=1.0,
        active_rules=[],
        encounter_state=3,  # ENCOUNTER_RELEASE
        past_clear=True,
        release_predicted=True,
        colregs_chain_target_id="100000001",
    )

    payload_release = _normalize_colregs_constraint_msg(msg_release)

    assert payload_release["encounter_state"] == 3
    assert payload_release["past_clear"] is True
    assert payload_release["release_predicted"] is True

    # ONSET phase: encounter_state=1. The R8 bug case — M6 must NOT stay here
    # when Rule14 geometry is ACTIVE. After the Phase 1.1 fix, M6 publishes
    # ENCOUNTER_ACTIVE; this test only pins that ONSET is also captured.
    msg_onset = SimpleNamespace(
        conflict_detected=True,
        primary_role=2,
        phase="",
        primary_preferred_direction="STARBOARD",
        confidence=0.5,
        active_rules=[],
        encounter_state=1,  # ENCOUNTER_ONSET
        past_clear=False,
        release_predicted=False,
        colregs_chain_target_id="",
    )

    payload_onset = _normalize_colregs_constraint_msg(msg_onset)

    assert payload_onset["encounter_state"] == 1
    assert payload_onset["past_clear"] is False


def test_world_state_normalizer_records_primary_target_geometry() -> None:
    def target(target_id: int, cpa_m: float) -> SimpleNamespace:
        return SimpleNamespace(
            target_id=target_id,
            heading_deg=180.0,
            cog_deg=180.0,
            sog_kn=8.0,
            brg_deg=22.0,
            rng_m=900.0,
            cpa_m=cpa_m,
            tcpa_s=120.0,
            target_compliance=0.75,
            encounter=SimpleNamespace(
                encounter_type=1,
                relative_bearing_deg=4.0,
                aspect_angle_deg=178.0,
                is_giveway=True,
            ),
        )

    msg = SimpleNamespace(
        confidence=0.8,
        own_ship=SimpleNamespace(
            heading_deg=12.0,
            sog_kn=6.0,
            position=SimpleNamespace(latitude=30.0, longitude=122.0),
        ),
        targets=[target(10, 300.0), target(11, 120.0)],
    )

    payload = _normalize_world_state_msg(msg)

    assert payload["target_count"] == 2
    assert payload["own_heading_deg"] == 12.0
    assert payload["primary_target_id"] == 11
    assert payload["primary_cpa_m"] == 120.0
    assert payload["primary_tcpa_s"] == 120.0
    assert payload["primary_encounter_type"] == 1
    assert payload["primary_relative_bearing_deg"] == 4.0


def test_avoidance_plan_normalizer_uses_waypoint_presence_for_validity() -> None:
    msg = SimpleNamespace(
        status="RECOVERY",
        waypoints=[
            SimpleNamespace(
                turn_radius_m=0.0,
                target_speed_kn=6.0,
                position=SimpleNamespace(latitude=63.0, longitude=10.0),
            ),
            SimpleNamespace(
                turn_radius_m=120.0,
                target_speed_kn=6.0,
                position=SimpleNamespace(latitude=63.001, longitude=10.002),
            ),
        ],
    )

    payload = _normalize_avoidance_plan_msg(msg)

    assert payload["n_waypoints"] == 2
    assert payload["solver_status"] == "VALID"
    assert payload["plan_status"] == "RECOVERY"
    assert payload["wp0_turn_radius_m"] == 0.0
    assert payload["wp1_lon"] == pytest.approx(10.002)


def test_gnc_execution_status_normalizer_preserves_l4_contract_fields() -> None:
    msg = SimpleNamespace(
        stamp=SimpleNamespace(sec=123, nanosec=456),
        schema_version=1,
        plan_id="avoid-1",
        active_route_id="route-9",
        command_source="COLAV",
        accepted=True,
        executing=True,
        degraded=False,
        rejected=False,
        execution_state="EXECUTING",
        reason="tracking",
        suggested_action="none",
        requested_speed_mps=3.4,
        applied_speed_mps=3.2,
        suggested_max_speed_mps=3.5,
        current_latitude=63.4,
        current_longitude=10.4,
        current_heading_deg=12.5,
        current_speed_mps=3.1,
        cross_track_error_m=4.2,
        confidence=0.9,
        rationale="bridge-translated",
    )

    payload = _normalize_gnc_execution_status_msg(msg)

    assert payload["stamp_sec"] == 123
    assert payload["stamp_nanosec"] == 456
    assert payload["schema_version"] == 1
    assert payload["accepted"] is True
    assert payload["executing"] is True
    assert payload["rejected"] is False
    assert payload["execution_state"] == "EXECUTING"
    assert payload["cross_track_error_m"] == 4.2
    assert payload["confidence"] == pytest.approx(0.9)
    assert payload["rationale"] == "bridge-translated"


# The legacy `_normalize_avoidance_waypoints_msg` helper and its test were
# removed when the `/l3/m5/avoidance_waypoints` shadow topic was deleted
# (Slice A: `/l3/m5/avoidance_plan` is the only M5 execution-truth topic).
# The corresponding test_avoidance_waypoints_normalizer_* case is also dropped
# here so the suite collects cleanly. R9 contract-yaml cleanup lives in
# Phase 2 Task 2.6 (spec v2.3).


# --- Phase 1.3 (G-TR-2, spec v2.3 §15): new topic normalizers ---------------
# These pin the audit-relevant fields captured for each previously-untraced
# topic, so a future regression that drops a field is caught.


def test_sat2_data_normalizer_captures_reasoning_chain_summary() -> None:
    msg = SimpleNamespace(
        trigger_reason="mpc_cycle",
        system_confidence=0.82,
        colregs_chain_target_id="100000001",
        reasoning_latency_ms=12.5,
    )

    payload = _normalize_sat2_data_msg(msg)

    assert payload["trigger_reason"] == "mpc_cycle"
    assert payload["system_confidence"] == pytest.approx(0.82)
    assert payload["colregs_chain_target_id"] == "100000001"
    assert payload["reasoning_latency_ms"] == pytest.approx(12.5)


def test_sat3_data_normalizer_captures_forecast_horizon_fields() -> None:
    msg = SimpleNamespace(
        predicted_state="AVOIDANCE",
        prediction_uncertainty=0.15,
        tdl_s=120.0,
        tmr_s=60.0,
        primary_trajectory_idx=3,
        takeover_window_s=45.0,
        trajectory_candidates=[object(), object(), object()],  # count only
    )

    payload = _normalize_sat3_data_msg(msg)

    assert payload["predicted_state"] == "AVOIDANCE"
    assert payload["prediction_uncertainty"] == pytest.approx(0.15)
    assert payload["tdl_s"] == pytest.approx(120.0)
    assert payload["tmr_s"] == pytest.approx(60.0)
    assert payload["primary_trajectory_idx"] == 3
    assert payload["takeover_window_s"] == pytest.approx(45.0)
    assert payload["trajectory_candidate_count"] == 3


def test_sotif_metrics_normalizer_lists_active_violations() -> None:
    msg = SimpleNamespace(
        active_violation_count=2,
        metrics=[
            SimpleNamespace(assumption_id=0, violation_flag=True, severity=0.8),
            SimpleNamespace(assumption_id=1, violation_flag=False, severity=0.0),
            SimpleNamespace(assumption_id=3, violation_flag=True, severity=0.6),
        ],
    )

    payload = _normalize_sotif_metrics_msg(msg)

    assert payload["active_violation_count"] == 2
    assert payload["active_violations"] == [
        {"assumption_id": 0, "severity": pytest.approx(0.8)},
        {"assumption_id": 3, "severity": pytest.approx(0.6)},
    ]


def test_module_pulse_normalizer_captures_health_state() -> None:
    msg = SimpleNamespace(module_id=5, state=2, latency_ms=42, message_drops=1)

    payload = _normalize_module_pulse_msg(msg)

    assert payload["module_id"] == 5  # M5
    assert payload["state"] == 2  # AMBER
    assert payload["latency_ms"] == 42
    assert payload["message_drops"] == 1


def test_reactive_override_cmd_normalizer_captures_command_and_validity() -> None:
    msg = SimpleNamespace(
        trigger_reason="CPA_EMERGENCY",
        heading_cmd_deg=53.2,
        speed_cmd_kn=6.0,
        rot_cmd_deg_s=4.7,
        validity_s=2.0,
        confidence=0.9,
    )

    payload = _normalize_reactive_override_cmd_msg(msg)

    assert payload["trigger_reason"] == "CPA_EMERGENCY"
    assert payload["heading_cmd_deg"] == pytest.approx(53.2)
    assert payload["speed_cmd_kn"] == pytest.approx(6.0)
    assert payload["rot_cmd_deg_s"] == pytest.approx(4.7)
    assert payload["validity_s"] == pytest.approx(2.0)
    assert payload["confidence"] == pytest.approx(0.9)


def test_override_active_normalizer_captures_activation_source() -> None:
    msg = SimpleNamespace(
        override_active=True,
        activation_source="automatic_trigger",
        confidence=1.0,
    )

    payload = _normalize_override_active_msg(msg)

    assert payload["override_active"] is True
    assert payload["activation_source"] == "automatic_trigger"
    assert payload["confidence"] == pytest.approx(1.0)


def test_m7_observability_normalizer_captures_verdict_and_path_s() -> None:
    msg = SimpleNamespace(verdict_code=2, path_s_clean=True)

    payload = _normalize_m7_observability_msg(msg)

    assert payload["verdict_code"] == 2
    assert payload["path_s_clean"] is True


def test_new_normalizers_default_safely_on_missing_fields() -> None:
    """All Phase 1.3 normalizers use getattr defaults so a malformed or partial
    ROS message (e.g. older schema_version) does not crash the trace writer."""
    empty = SimpleNamespace()

    assert _normalize_sat2_data_msg(empty)["trigger_reason"] == ""
    assert _normalize_sat3_data_msg(empty)["trajectory_candidate_count"] == 0
    assert _normalize_sotif_metrics_msg(empty)["active_violations"] == []
    assert _normalize_module_pulse_msg(empty)["module_id"] == 0
    assert _normalize_reactive_override_cmd_msg(empty)["validity_s"] == 0.0
    assert _normalize_override_active_msg(empty)["override_active"] is False
    assert _normalize_m7_observability_msg(empty)["verdict_code"] == 0