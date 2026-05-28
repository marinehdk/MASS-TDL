"""ASDR (Autonomous Decision & Safety Record) event stream routes.

Events are derived exclusively from the ROS2 topic cache (MessageCache).
No dependency on the dead-reckoning sidecar or _avoidance_state globals.

TODO (10B-E): wire ROS2 subscribers to populate _msg_cache at runtime.
              Until then the cache is empty and events fall back to
              time-gated stubs (INIT / SCENE_CHG / END preserved).
"""
import hashlib
import json
import threading
from collections import deque
from dataclasses import dataclass, field

from fastapi import APIRouter

router = APIRouter(prefix="/api/v1/asdr", tags=["asdr"])


# ---------------------------------------------------------------------------
# 10A-2: MessageCache — thread-safe per-topic deque store
# ---------------------------------------------------------------------------

@dataclass
class MessageCache:
    behavior_plan: deque = field(default_factory=lambda: deque(maxlen=100))
    rule_assessment: deque = field(default_factory=lambda: deque(maxlen=100))
    actuator_cmd: deque = field(default_factory=lambda: deque(maxlen=100))
    threat_state: deque = field(default_factory=lambda: deque(maxlen=100))
    lock: threading.RLock = field(default_factory=threading.RLock)

    def append(self, topic: str, msg: dict) -> None:
        """Append a message dict to the named topic deque. Unknown topics are ignored."""
        with self.lock:
            deque_map = {
                "behavior_plan": self.behavior_plan,
                "rule_assessment": self.rule_assessment,
                "actuator_cmd": self.actuator_cmd,
                "threat_state": self.threat_state,
            }
            target = deque_map.get(topic)
            if target is None:
                return
            target.append(msg)

    def get_snapshot(self) -> dict:
        """Return a point-in-time snapshot of all topics as plain lists."""
        with self.lock:
            return {
                "behavior_plan": list(self.behavior_plan),
                "rule_assessment": list(self.rule_assessment),
                "actuator_cmd": list(self.actuator_cmd),
                "threat_state": list(self.threat_state),
            }


_msg_cache = MessageCache()


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _extract_sec(stamp) -> float | None:
    """Extract seconds from a ROS2 stamp value.

    Accepts:
      - numeric (int/float): used directly
      - dict {"sec": int, "nanosec": int}  — standard ROS2 builtin_interfaces/Time
      - dict {"secs": int, "nsecs": int}   — older rospy serialisation variant
    Returns None for any other type (silently ignored by caller).
    """
    if isinstance(stamp, (int, float)):
        return float(stamp)
    if isinstance(stamp, dict):
        if "sec" in stamp:
            return float(stamp["sec"]) + stamp.get("nanosec", 0) / 1e9
        if "secs" in stamp:
            return float(stamp["secs"]) + stamp.get("nsecs", 0) / 1e9
    return None


def _fmt_time(t: float) -> str:
    total_seconds = int(t)
    minutes = total_seconds // 60
    seconds = total_seconds % 60
    return f"T+{minutes:02d}:{seconds:02d}"


def _event_hash(event: dict) -> str:
    raw = json.dumps(event, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(raw.encode()).hexdigest()[:12]


# ---------------------------------------------------------------------------
# 10A-3: _generate_asdr_events — cache-based, no _avoidance_state
# ---------------------------------------------------------------------------

def _generate_asdr_events(sim_time: float) -> list[dict]:
    """Generate ASDR event list from the ROS2 message cache.

    Sources:
      T01_DET   @ 25 s  ← cache["threat_state"][0]
      CPA_PROJ  @ 38 s  ← cache["threat_state"] where cpa_nm < 0.40
      COLREG_R14@ 49 s  ← cache["rule_assessment"] applicable_rule == "Rule 14"
      MPC_BRANCH@ 52 s  ← cache["behavior_plan"] rationale contains "mpc"
      CPA_MIN   @140 s  ← min cpa across cache["threat_state"]
      SCENE_CHG / END   — time-gated, no cache dependency (preserved)
    """
    snap = _msg_cache.get_snapshot()
    events = []

    # INIT — always at t=0 when sim has started
    if sim_time >= 0:
        events.append(
            {
                "t": 0.0,
                "type": "INIT",
                "module": "M1",
                "payload": {"scene": "TRANSIT", "odd_status": "NOMINAL"},
            }
        )

    # T01_DET — first threat_state entry
    if sim_time >= 25 and snap["threat_state"]:
        first_threat = snap["threat_state"][0]
        events.append(
            {
                "t": 25.0,
                "type": "T01_DET",
                "module": "M2",
                "payload": {
                    "target_mmsi": first_threat.get("target_mmsi", 0),
                    "source": "RADAR+AIS",
                },
            }
        )

    # CPA_PROJ — any threat with cpa_nm < 0.40
    if sim_time >= 38:
        cpa_entries = [
            m.get("cpa_nm", float("inf"))
            for m in snap["threat_state"]
            if m.get("cpa_nm", float("inf")) < 0.40
        ]
        if cpa_entries:
            dcpa_val = round(min(cpa_entries), 3)
            events.append(
                {
                    "t": 38.0,
                    "type": "CPA_PROJ",
                    "module": "M2",
                    "payload": {
                        "dcpa_nm": dcpa_val,
                        "threshold_nm": 0.40,
                        "severity": "WARNING",
                    },
                }
            )

    # SCENE_CHG TRANSIT→COLREG_AVOIDANCE — time-gated, no cache dep
    if sim_time >= 47:
        events.append(
            {
                "t": 47.0,
                "type": "SCENE_CHG",
                "module": "M1",
                "payload": {"from": "TRANSIT", "to": "COLREG_AVOIDANCE"},
            }
        )

    # COLREG_R14 — rule_assessment with applicable_rule == "Rule 14"
    if sim_time >= 49:
        r14_entries = [
            m for m in snap["rule_assessment"]
            if m.get("applicable_rule") == "Rule 14"
        ]
        if r14_entries:
            entry = r14_entries[0]
            events.append(
                {
                    "t": 49.0,
                    "type": "COLREG_R14",
                    "module": "M6",
                    "payload": {
                        "rule": "Rule 14",
                        "name": "Head-on",
                        "give_way": entry.get("give_way", "OWN"),
                    },
                }
            )

    # MPC_BRANCH — behavior_plan entry with "mpc" in rationale
    if sim_time >= 52:
        mpc_entries = [
            m for m in snap["behavior_plan"]
            if "mpc" in str(m.get("rationale", "")).lower()
        ]
        if mpc_entries:
            entry = mpc_entries[0]
            events.append(
                {
                    "t": 52.0,
                    "type": "MPC_BRANCH",
                    "module": "M5",
                    "payload": {
                        "action": entry.get("action", "STARBOARD_TURN"),
                        "delta_heading_deg": entry.get("delta_heading_deg", 35.0),
                    },
                }
            )

    # CPA_MIN — minimum cpa across all threat_state entries
    # Only emitted when real cpa_nm data exists; omitted entirely otherwise.
    if sim_time >= 140 and snap["threat_state"]:
        all_cpas = [
            m.get("cpa_nm", float("inf"))
            for m in snap["threat_state"]
        ]
        min_cpa = min(all_cpas)
        if min_cpa < float("inf"):
            events.append(
                {
                    "t": 140.0,
                    "type": "CPA_MIN",
                    "module": "M2",
                    "payload": {"dcpa_nm": round(min_cpa, 3)},
                }
            )

    # SCENE_CHG COLREG_AVOIDANCE→TRANSIT — time-gated, no cache dep
    if sim_time >= 152:
        events.append(
            {
                "t": 152.0,
                "type": "SCENE_CHG",
                "module": "M1",
                "payload": {"from": "COLREG_AVOIDANCE", "to": "TRANSIT"},
            }
        )

    # END — time-gated, no cache dep
    if sim_time >= 600:
        events.append(
            {
                "t": 600.0,
                "type": "END",
                "module": "M1",
                "payload": {"scene": "TRANSIT", "odd_status": "NOMINAL"},
            }
        )

    return events


# ---------------------------------------------------------------------------
# 10A-4: /events endpoint — reads sim_time from cache, not _avoidance_state
# ---------------------------------------------------------------------------

@router.get("/events")
async def get_asdr_events():
    """Return ASDR event ledger.

    sim_time is derived from the latest timestamp in the cache.
    Falls back to 0.0 if cache is empty (no avoidance-state dependency).

    TODO (10B-E): ROS2 subscriber wiring to populate _msg_cache at runtime.
    """
    snap = _msg_cache.get_snapshot()

    # Derive sim_time from latest stamp across all topics.
    # Stamps may be numeric or ROS2 dict form; _extract_sec handles both.
    latest_stamp = 0.0
    for topic_msgs in snap.values():
        for msg in topic_msgs:
            sec = _extract_sec(msg.get("stamp", 0.0))
            if sec is not None and sec > latest_stamp:
                latest_stamp = sec

    sim_time = latest_stamp  # 0.0 when cache empty

    events = _generate_asdr_events(sim_time)

    ledger = []
    for ev in events:
        ledger.append(
            {
                "time": _fmt_time(ev["t"]),
                "type": ev["type"],
                "module": ev["module"],
                "payload": ev["payload"],
                "hash": _event_hash(ev),
            }
        )

    return {"events": events, "ledger": ledger}
