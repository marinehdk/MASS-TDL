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
_normalize_avoidance_waypoints_msg = _module._normalize_avoidance_waypoints_msg
_normalize_avoidance_plan_msg = _module._normalize_avoidance_plan_msg


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


def test_avoidance_waypoints_normalizer_preserves_external_m5_contract_fields() -> None:
    msg = SimpleNamespace(
        stamp=SimpleNamespace(sec=12, nanosec=34),
        schema_version=1,
        plan_id="m5-return-42",
        parent_route_id="nominal-route",
        behavior_mode="return_to_route",
        command_source="M5_COLREGS",
        latitude=[63.0, 63.001, 63.002],
        longitude=[10.0, 10.002, 10.004],
        command_speed_mps=[3.2, 3.2, 3.2],
        navigation_mode=["emergency", "emergency", "normal"],
        valid_until=SimpleNamespace(sec=99, nanosec=88),
        allow_degraded_execution=True,
        has_return_to_route_point=True,
        return_latitude=63.002,
        return_longitude=10.004,
        confidence=0.8,
        rationale="stable-return-corridor",
    )

    payload = _normalize_avoidance_waypoints_msg(msg)

    assert payload["stamp_sec"] == 12
    assert payload["schema_version"] == 1
    assert payload["plan_id"] == "m5-return-42"
    assert payload["parent_route_id"] == "nominal-route"
    assert payload["behavior_mode"] == "return_to_route"
    assert payload["command_source"] == "M5_COLREGS"
    assert payload["n_waypoints"] == 3
    assert payload["latitude"] == [63.0, 63.001, 63.002]
    assert payload["longitude"] == [10.0, 10.002, 10.004]
    assert payload["command_speed_mps"] == [3.2, 3.2, 3.2]
    assert payload["navigation_mode"] == ["emergency", "emergency", "normal"]
    assert payload["valid_until_sec"] == 99
    assert payload["allow_degraded_execution"] is True
    assert payload["has_return_to_route_point"] is True
    assert payload["return_latitude"] == pytest.approx(63.002)
    assert payload["return_longitude"] == pytest.approx(10.004)
    assert payload["wp0_lat"] == pytest.approx(63.0)
    assert payload["wp_last_lon"] == pytest.approx(10.004)
    assert payload["confidence"] == pytest.approx(0.8)
    assert payload["rationale"] == "stable-return-corridor"
