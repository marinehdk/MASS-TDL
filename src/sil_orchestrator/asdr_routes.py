import hashlib
import json

from fastapi import APIRouter

from sil_orchestrator.demo_avoidance import AvoidanceState

router = APIRouter(prefix="/api/v1/asdr", tags=["asdr"])


def _fmt_time(t: float) -> str:
    total_seconds = int(t)
    minutes = total_seconds // 60
    seconds = total_seconds % 60
    return f"T+{minutes:02d}:{seconds:02d}"


def _event_hash(event: dict) -> str:
    raw = json.dumps(event, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(raw.encode()).hexdigest()[:12]


def _generate_asdr_events(
    state: AvoidanceState | None, sim_time: float, min_cpa_nm: float
) -> list[dict]:
    events = []

    if sim_time >= 0:
        events.append(
            {
                "t": 0.0,
                "type": "INIT",
                "module": "M1",
                "payload": {"scene": "TRANSIT", "odd_status": "NOMINAL"},
            }
        )

    if sim_time >= 25:
        mmsi = state.targets[0].mmsi if state and state.targets else 0
        events.append(
            {
                "t": 25.0,
                "type": "T01_DET",
                "module": "M2",
                "payload": {"target_mmsi": mmsi, "source": "RADAR+AIS"},
            }
        )

    if sim_time >= 38 and (min_cpa_nm < 0.40 or min_cpa_nm == float("inf")):
        dcpa_val = round(min_cpa_nm, 3) if min_cpa_nm < float("inf") else 0.380
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

    if sim_time >= 47:
        events.append(
            {
                "t": 47.0,
                "type": "SCENE_CHG",
                "module": "M1",
                "payload": {"from": "TRANSIT", "to": "COLREG_AVOIDANCE"},
            }
        )

    if sim_time >= 49:
        events.append(
            {
                "t": 49.0,
                "type": "COLREG_R14",
                "module": "M6",
                "payload": {"rule": "Rule 14", "name": "Head-on", "give_way": "OWN"},
            }
        )

    if sim_time >= 52:
        events.append(
            {
                "t": 52.0,
                "type": "MPC_BRANCH",
                "module": "M5",
                "payload": {
                    "action": "STARBOARD_TURN",
                    "delta_heading_deg": 35.0,
                },
            }
        )

    if sim_time >= 140:
        dcpa_val = round(min_cpa_nm, 3) if min_cpa_nm < float("inf") else 0.420
        events.append(
            {
                "t": 140.0,
                "type": "CPA_MIN",
                "module": "M2",
                "payload": {"dcpa_nm": dcpa_val},
            }
        )

    if sim_time >= 152:
        events.append(
            {
                "t": 152.0,
                "type": "SCENE_CHG",
                "module": "M1",
                "payload": {"from": "COLREG_AVOIDANCE", "to": "TRANSIT"},
            }
        )

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


@router.get("/events")
async def get_asdr_events():
    from sil_orchestrator.main import _avoidance_state, _demo_min_cpa_nm

    if _avoidance_state is None:
        return {"events": [], "ledger": []}

    sim_time = _avoidance_state.sim_time
    events = _generate_asdr_events(_avoidance_state, sim_time, _demo_min_cpa_nm)

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
