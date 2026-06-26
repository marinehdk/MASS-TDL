"""Standalone SIL trace JSONL writer (Track A A5c regression fix).

Background
----------
A5c (commit ``f138b0d9``) deleted ``docker/sil_topic_bridge.py`` and replaced it
with three C++ adapter packages (``sil_fusion_adapter`` / ``sil_trace_adapter``
/ ``sil_pulse_adapter``). The C++ adapters are pure DDS→DDS topic relays — none
of them touch the filesystem. But ``sil_topic_bridge.py`` had been the **sole
writer** of ``runs/trace_current.jsonl``:

  - The orchestrator ``GET /api/v1/debug/snapshot`` reads that file to report
    ``sim_t`` (``src/sil_orchestrator/routers/debug_routes.py``).
  - The COLREGs probe ``run_6_scenarios.get_sim_time()`` reads that snapshot;
    with the file missing it always saw ``sim_t=0`` and the stuck-detector
    fired before any trace evaluation — which is what masked this regression as
    a "GNC warmup" problem for two sessions.

This module restores the writer as an **independent process** (launched from
``sil_entrypoint.sh`` Stage 3a alongside the C++ adapters), preserving the exact
JSONL record schema the trace evaluators consume. It deliberately does *not*
reimplement any of the bridge's DDS→DDS translation — that is the C++ adapters'
job now. It only records.

Design
------
``DebugTraceWriter`` is ROS2-agnostic (pure file I/O + threading) so it is unit
testable off-container. ``TraceWriterNode`` is a thin rclpy node that subscribes
to the trace topics and forwards normalized dicts to the writer. Keeping the two
separate mirrors the original bridge's split and lets the writer's behaviour be
pinned by unit tests without a ROS2 runtime.
"""
from __future__ import annotations

import collections
import gzip
import json
import math
import os
import shutil
import threading
import time
from pathlib import Path
from typing import Any, Protocol


class _LoggerLike(Protocol):
    def error(self, msg: str) -> None: ...
    def warning(self, msg: str) -> None: ...
    def info(self, msg: str) -> None: ...


class _StderrLogger:
    """Fallback logger used when no ROS2 node is supplied (tests / dry-run)."""

    def error(self, msg: str) -> None:
        print(f"[sil_trace_writer] ERROR: {msg}", flush=True)

    def warning(self, msg: str) -> None:
        print(f"[sil_trace_writer] WARN: {msg}", flush=True)

    def info(self, msg: str) -> None:
        print(f"[sil_trace_writer] {msg}", flush=True)


class DebugTraceWriter:
    """Ring-buffer JSONL writer for the key L3/SIL interface topics.

    Appends to ``$SIL_RUN_DIR/trace_current.jsonl`` (shared volume). Thread-safe;
    flushes every ``FLUSH_INTERVAL_S``. Call :meth:`reset` on scenario ACTIVE to
    truncate stale records from a previous run.

    Ported from the deleted ``docker/sil_topic_bridge.py::DebugTraceWriter``
    (commit ``f138b0d9``). The record schema is unchanged so every downstream
    trace evaluator (``run_6_scenarios``, ``colregs_chain_trace``,
    ``trajectory_dashboard``) keeps working.
    """

    FLUSH_INTERVAL_S = 0.5
    MAX_BUF = 2000
    # Flush inline once this many records have buffered since the last flush,
    # so the on-disk sim_t tracks the live /clock even under burst publishers.
    INLINE_FLUSH_EVERY = 25
    # File size above which flush() gzips the current file aside and reopens
    # fresh. Exposed as an instance attribute (not the constant) so tests can
    # shrink it via monkeypatch without writing 50 MB.
    _rotate_size_bytes = 50 * 1024 * 1024

    def __init__(self, logger: _LoggerLike | None = None) -> None:
        """``logger`` is anything with ``error``/``warning``/``info`` (a ROS2
        node's ``get_logger()`` result). ``None`` falls back to stderr."""
        self._node: _LoggerLike = logger if logger is not None else _StderrLogger()
        self._lock = threading.Lock()
        self._buf: collections.deque[str] = collections.deque(maxlen=self.MAX_BUF)
        self._file: Any = None
        self._flush_timer: threading.Timer | None = None
        run_dir = Path(os.environ.get("SIL_RUN_DIR", "/var/sil/runs"))
        self._trace_path = run_dir / "trace_current.jsonl"
        self.reset()

    # ── lifecycle ────────────────────────────────────────────

    def reset(self) -> None:
        """Truncate the trace file and restart the flush timer.

        Called on scenario ACTIVE so a new run does not inherit the previous
        run's records (which would confuse the orchestrator snapshot's
        sim_t-backward slicing).
        """
        with self._lock:
            if self._file is not None:
                try:
                    self._file.close()
                except Exception:
                    pass
            self._buf.clear()
            try:
                self._trace_path.parent.mkdir(parents=True, exist_ok=True)
                self._file = open(self._trace_path, "w")
            except Exception as exc:
                self._node.error(f"[DebugTraceWriter] cannot open {self._trace_path}: {exc}")
                self._file = None
        self._schedule_flush()

    def close(self) -> None:
        """Flush any pending records and stop the timer. Idempotent."""
        if self._flush_timer is not None:
            self._flush_timer.cancel()
            self._flush_timer = None
        with self._lock:
            if self._file:
                try:
                    if self._buf:
                        self._file.write("\n".join(self._buf) + "\n")
                        self._buf.clear()
                    self._file.flush()
                    self._file.close()
                except Exception as exc:
                    self._node.warning(f"[DebugTraceWriter] close error: {exc}")
                self._file = None

    # ── record ───────────────────────────────────────────────

    def record(self, topic: str, data: dict[str, Any], sim_t: float) -> None:
        """Append one record to the in-memory ring buffer.

        Flushing is timer-driven (every ``FLUSH_INTERVAL_S``) plus inline once
        ``INLINE_FLUSH_EVERY`` records have buffered, so the on-disk sim_t stays
        close to the live /clock even under high-rate publishers. The inline
        flush runs *after* releasing the buffer lock so it cannot self-deadlock.
        """
        entry: dict[str, Any] = {
            "sim_t": round(float(sim_t), 3),
            "wall_t": round(time.time(), 3),
            "topic": topic,
        }
        entry.update(data)
        need_flush = False
        with self._lock:
            self._buf.append(json.dumps(entry, default=str))
            if len(self._buf) >= self.INLINE_FLUSH_EVERY:
                need_flush = True
        if need_flush:
            self.flush()

    # ── flush + rotation ─────────────────────────────────────

    def flush(self) -> None:
        """Write the buffered records to disk, rotating if the file is large."""
        with self._lock:
            if self._file and self._buf:
                try:
                    lines = list(self._buf)
                    self._buf.clear()
                    self._file.write("\n".join(lines) + "\n")
                    self._file.flush()
                except Exception as exc:
                    self._node.warning(f"[DebugTraceWriter] flush error: {exc}")

            if self._file:
                try:
                    if (
                        self._trace_path.exists()
                        and self._trace_path.stat().st_size > self._rotate_size_bytes
                    ):
                        self._node.info(
                            "[DebugTraceWriter] Trace file size exceeded cap. Rotating..."
                        )
                        self._file.close()
                        self._file = None
                        rotated_path = (
                            self._trace_path.parent / f"trace_{int(time.time())}.jsonl.gz"
                        )
                        with open(self._trace_path, "rb") as f_in:
                            with gzip.open(rotated_path, "wb") as f_out:
                                shutil.copyfileobj(f_in, f_out)
                        self._node.info(f"[DebugTraceWriter] Rotated trace to {rotated_path}")
                        self._file = open(self._trace_path, "w")
                except Exception as exc:
                    self._node.error(f"[DebugTraceWriter] Failed to rotate trace file: {exc}")
                    if self._file is None:
                        try:
                            self._file = open(self._trace_path, "a")
                        except Exception:
                            pass

    def _schedule_flush(self) -> None:
        if self._flush_timer is not None:
            self._flush_timer.cancel()
        t = threading.Timer(self.FLUSH_INTERVAL_S, self.flush)
        t.daemon = True
        t.start()
        self._flush_timer = t


# ─────────────────────────────────────────────────────────────
# ROS2 node — subscribes to the trace topics and records them.
# Only imported when run as a process; unit tests exercise the writer directly.
# ─────────────────────────────────────────────────────────────


def _latched_qos():  # pragma: no cover — requires rclpy
    from rclpy.qos import QoSProfile, QoSReliabilityPolicy, QoSDurabilityPolicy, QoSHistoryPolicy

    return QoSProfile(
        reliability=QoSReliabilityPolicy.RELIABLE,
        durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
        history=QoSHistoryPolicy.KEEP_LAST,
        depth=1,
    )


def _volatile_qos(depth: int = 10):  # pragma: no cover — requires rclpy
    from rclpy.qos import QoSProfile, QoSReliabilityPolicy, QoSDurabilityPolicy, QoSHistoryPolicy

    return QoSProfile(
        reliability=QoSReliabilityPolicy.BEST_EFFORT,
        durability=QoSDurabilityPolicy.VOLATILE,
        history=QoSHistoryPolicy.KEEP_LAST,
        depth=depth,
    )


# Topic → (message type import path, normalizer). Normalizers turn a ROS msg
# into the dict the trace evaluators expect. Kept 1:1 with the deleted bridge's
# record() payloads so downstream consumers are unaffected.
def _build_subscriptions(writer: DebugTraceWriter) -> list[tuple[str, str, Any]]:
    """Return [(topic, msg_type_fqn, callback), ...]. Importing lazily so the
    writer module stays importable without ROS2 installed."""
    from sil_msgs.msg import OwnShipState as SilOwnShipState, ScoringRow, LifecycleStatus
    from l3_msgs.msg import (
        AvoidancePlan,
        ASDRRecord,
        BehaviorPlan,
        COLREGsConstraint,
        FsmState,
        MissionGoal,
        SafetyAlert,
    )
    from l3_external_msgs.msg import CheckerVetoNotification, PlannedRoute

    def sim_t(node) -> float:
        return node.get_clock().now().nanoseconds * 1e-9

    node = writer  # the node holds the clock; set in TraceWriterNode.__init__

    # NOTE: `sim_t` is captured per-callback from the ROS node's sim clock. The
    # node object is attached below in TraceWriterNode; we close over it via a
    # mutable holder so the lambdas defined here see the final node.
    holder: dict[str, Any] = {"node": None}

    def t_now() -> float:
        return holder["node"].get_clock().now().nanoseconds * 1e-9

    def on_own(msg):
        writer.record(
            "/sil/own_ship_state",
            {
                "heading_deg": round(math.degrees(msg.heading), 2),
                "sog_kn": round(msg.sog * 1.94384, 2),
                "lat": msg.lat,
                "lon": msg.lon,
                "rot_deg_s": round(math.degrees(msg.rot), 3),
            },
            t_now(),
        )

    def on_behavior(msg):
        writer.record(
            "/l3/m4/behavior_plan",
            {
                "behavior": int(msg.behavior),
                "heading_min_deg": float(msg.heading_min_deg),
                "heading_max_deg": float(msg.heading_max_deg),
                "avoidance_active": bool(getattr(msg, "avoidance_active", False)),
            },
            t_now(),
        )

    def on_avoidance(msg):
        wp0 = msg.waypoints[0] if msg.waypoints else None
        wp1 = msg.waypoints[1] if len(msg.waypoints) > 1 else None
        wp0_pos = getattr(wp0, "position", None) if wp0 else None
        wp1_pos = getattr(wp1, "position", None) if wp1 else None
        writer.record(
            "/l3/m5/avoidance_plan",
            {
                "n_waypoints": len(msg.waypoints),
                "solver_status": "VALID" if (wp0 and abs(wp0.turn_radius_m) > 1e-6) else "EMPTY",
                "wp0_turn_radius_m": float(wp0.turn_radius_m) if wp0 else 0.0,
                "wp0_target_speed_kn": float(wp0.target_speed_kn) if wp0 else 0.0,
                "wp0_lat": float(wp0_pos.latitude) if wp0_pos else 0.0,
                "wp0_lon": float(wp0_pos.longitude) if wp0_pos else 0.0,
                "wp1_lat": float(wp1_pos.latitude) if wp1_pos else 0.0,
                "wp1_lon": float(wp1_pos.longitude) if wp1_pos else 0.0,
            },
            t_now(),
        )

    def on_colregs(msg):
        writer.record(
            "/l3/m6/colregs_constraint",
            {
                "conflict_detected": bool(msg.conflict_detected),
                "primary_role": int(msg.primary_role),
                "phase": str(msg.phase),
                "primary_preferred_direction": str(msg.primary_preferred_direction),
                "confidence": float(msg.confidence),
            },
            t_now(),
        )

    def on_veto(msg):
        writer.record(
            "/l3/checker/veto",
            {
                "checker_layer": str(msg.checker_layer),
                "vetoed_module": str(msg.vetoed_module),
                "veto_reason_class": int(msg.veto_reason_class),
                "veto_reason_detail": str(msg.veto_reason_detail),
                "fallback_provided": bool(msg.fallback_provided),
                "confidence": float(msg.confidence),
            },
            t_now(),
        )

    def on_actuator(msg):
        writer.record(
            "/sil/actuator_cmd",
            {
                "rudder_deg": math.degrees(float(getattr(msg, "rudder_angle", 0.0))),
                "throttle": float(getattr(msg, "throttle", 0.0)),
            },
            t_now(),
        )

    def on_scoring(msg):
        writer.record(
            "/sil/scoring",
            {
                "safety": float(msg.safety),
                "rule_compliance": float(msg.rule_compliance),
                "delay": float(msg.delay),
                "magnitude": float(msg.magnitude),
                "phase": float(msg.phase),
                "plausibility": float(msg.plausibility),
                "total": float(msg.total),
            },
            t_now(),
        )

    def on_asdr(msg):
        writer.record(
            "/l3/asdr/record",
            {
                "source_module": str(msg.source_module),
                "decision_type": str(msg.decision_type),
                "decision_json": str(msg.decision_json),
                "confidence": float(msg.confidence),
                "rationale": str(msg.rationale),
            },
            t_now(),
        )

    def on_mission(msg):
        writer.record(
            "/l3/m3/mission_goal",
            {
                "fsm_state": int(msg.fsm_state),
                "task_validity": int(getattr(msg, "task_validity", -1)),
                "target_wp_lat": float(msg.current_target_wp.latitude),
                "target_wp_lon": float(msg.current_target_wp.longitude),
            },
            t_now(),
        )

    def on_fsm(msg):
        writer.record("/l3/fsm_state", {"state": int(msg.current_state)}, t_now())

    def on_safety_alert(msg):
        writer.record(
            "/l3/m7/safety_alert",
            {
                "alert_type": int(msg.alert_type),
                "severity": int(msg.severity),
                "recommended_mrm": str(msg.recommended_mrm),
                "confidence": float(msg.confidence),
            },
            t_now(),
        )

    def on_route(msg):
        # colregs_chain_trace watches route_hash changes as an upstream-stability
        # signal. Derive a stable hash from the route poses.
        import hashlib

        try:
            coords = ",".join(
                f"{p.pose.position.latitude:.7f},{p.pose.position.longitude:.7f}"
                for p in msg.route.poses
            )
            route_hash = hashlib.md5(coords.encode()).hexdigest()[:12]
        except Exception:
            route_hash = ""
        writer.record("/l2/planned_route", {"route_hash": route_hash}, t_now())

    def on_lifecycle(msg):
        # 3 = ACTIVE. On entry to ACTIVE, truncate the trace for a clean run.
        if int(msg.current_state) == 3 and holder.get("prev_lc") != 3:
            writer.reset()
        holder["prev_lc"] = int(msg.current_state)
        writer.record(
            "/sil/lifecycle_status",
            {
                "state": int(msg.current_state),
                "autopilot_enabled": int(msg.current_state) == 3,
                "avoidance_active": False,
            },
            t_now(),
        )

    subs = [
        ("/sil/own_ship_state", SilOwnShipState, on_own),
        ("/l3/m4/behavior_plan", BehaviorPlan, on_behavior),
        ("/l3/m5/avoidance_plan", AvoidancePlan, on_avoidance),
        ("/l3/m6/colregs_constraint", COLREGsConstraint, on_colregs),
        ("/l3/checker/veto", CheckerVetoNotification, on_veto),
        ("/sil/actuator_cmd", SilOwnShipState, on_actuator),
        ("/sil/scoring", ScoringRow, on_scoring),
        ("/l3/asdr/record", ASDRRecord, on_asdr),
        ("/l3/m3/mission_goal", MissionGoal, on_mission),
        ("/l3/fsm_state", FsmState, on_fsm),
        ("/l3/m7/safety_alert", SafetyAlert, on_safety_alert),
        ("/l2/planned_route", PlannedRoute, on_route),
        ("/sil/lifecycle_status", LifecycleStatus, on_lifecycle),
    ]
    # Return the holder alongside the subscriptions so main() can populate it
    # with the ROS node (which owns the sim clock the callbacks read via t_now()).
    return subs, holder


def main() -> None:  # pragma: no cover — process entrypoint
    import rclpy
    from rclpy.executors import MultiThreadedExecutor
    from rclpy.callback_groups import ReentrantCallbackGroup
    from rclpy.node import Node

    rclpy.init()
    node = Node("sil_trace_writer")
    node.get_logger().info("sil_trace_writer starting — recording trace_current.jsonl")
    writer = DebugTraceWriter(logger=node.get_logger())

    subs, holder = _build_subscriptions(writer)
    holder["node"] = node
    holder["prev_lc"] = None

    # Put every subscription in a Reentrant callback group so the use_sim_time
    # /clock callback (internal, on the default mutually-exclusive group) is not
    # queued behind a record() callback. With the default MutuallyExclusive group
    # the /clock update was being starved, which froze node.get_clock().now() at
    # the value from the last /clock that managed to run — so every record past
    # that point carried a stale sim_t and the probe saw the sim stuck.
    cb_group = ReentrantCallbackGroup()
    vq = _volatile_qos(10)
    lq = _latched_qos()
    for topic, msg_type, cb in subs:
        qos = lq if topic in ("/l2/planned_route",) else vq
        node.create_subscription(msg_type, topic, cb, qos, callback_group=cb_group)

    executor = MultiThreadedExecutor(num_threads=4)
    executor.add_node(node)
    try:
        executor.spin()
    except KeyboardInterrupt:
        pass
    finally:
        writer.close()
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":  # pragma: no cover
    main()
