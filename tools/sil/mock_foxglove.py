"""Mock foxglove_bridge WebSocket — pushes synthetic telemetry for SIL DEMO-1.

Until the ROS2 component_container_mt + real foxglove_bridge are stood up
(Plan Task 23 + 25), this script lets the React HMI (Screen ③ Bridge HMI)
render live values for the R14 head-on encounter end-to-end.

Outgoing schema mirrors the protobuf-derived TS types in ``web/src/types/sil/``:
  - /sil/own_ship_state       (camelCase, SI units, radians for angles)
  - /sil/target_vessel_state  (array of TargetVesselState)
  - /sil/environment_state    (wind + current + visibilityNm + seaStateBeaufort)
  - /sil/module_pulse         (8 entries, state=1/2/3 for GREEN/AMBER/RED)
  - /sil/asdr_event           (rule chain trigger events, emitted every 30s)

Usage:
    python tools/sil/mock_foxglove.py            # listen on ws://0.0.0.0:8765
    python tools/sil/mock_foxglove.py --port 8765
"""
from __future__ import annotations

import argparse
import asyncio
import json
import math
import time

try:
    import websockets
except ImportError as exc:  # pragma: no cover
    raise SystemExit("Install with: pip install websockets") from exc


KN_TO_MPS = 0.514444
DEG_TO_RAD = math.pi / 180.0


def _build_payloads(t: float) -> list[dict]:
    """Synthetic R14 head-on: own ship NB 12kt, target SB 10kt.

    Motion is exaggerated ~10x so the encounter unfolds visibly inside the SIL
    HMI viewport within ~60 s of wall-clock — Phase 1 demo only.
    """
    own_lat0, own_lon = 63.4400, 10.4000
    tgt_lat0, tgt_lon = 63.4520, 10.4010
    own_lat = own_lat0 + 0.00045 * t   # own ship marching north
    tgt_lat = tgt_lat0 - 0.00038 * t   # target marching south

    return [
        {
            "topic": "/sil/own_ship_state",
            "payload": {
                "stamp": time.time(),
                "pose": {"lat": own_lat, "lon": own_lon, "heading": 0.0},
                "kinematics": {
                    "sog": 12.0 * KN_TO_MPS,
                    "cog": 0.0,
                    "rot": 0.5 * DEG_TO_RAD,
                },
                "control": {"rudder": 0.0, "throttle": 0.8},
            },
        },
        {
            "topic": "/sil/target_vessel_state",
            "payload": [{
                "mmsi": "100000001",
                "stamp": time.time(),
                "pose": {"lat": tgt_lat, "lon": tgt_lon, "heading": math.pi},
                "kinematics": {
                    "sog": 10.0 * KN_TO_MPS,
                    "cog": math.pi,
                    "rot": 0.0,
                },
                "shipType": "CARGO",
                "mode": "replay",
            }],
        },
        {
            "topic": "/sil/environment_state",
            "payload": {
                "stamp": time.time(),
                "wind": {"direction": 270.0, "speedMps": 5.0},
                "current": {"direction": 0.0, "speedMps": 0.5},
                "visibilityNm": 10.0,
                "seaStateBeaufort": 3,
            },
        },
        {
            "topic": "/sil/module_pulse",
            "payload": [
                {
                    "moduleId": f"M{i}",
                    "state": 1,  # GREEN
                    "latencyMs": 2 + (i % 3),
                    "messageDrops": 0,
                }
                for i in range(1, 9)
            ],
        },
    ]


async def handler(ws):
    print(f"[mock-fg] client connected from {ws.remote_address}")
    t0 = time.time()
    try:
        while True:
            t = time.time() - t0
            for msg in _build_payloads(t):
                await ws.send(json.dumps(msg))
            await asyncio.sleep(0.1)  # ~10 Hz; enough for HMI smoothness
    except websockets.exceptions.ConnectionClosed:
        print("[mock-fg] client disconnected")


async def main(port: int) -> None:
    print(f"[mock-fg] listening on ws://0.0.0.0:{port}")
    async with websockets.serve(handler, "0.0.0.0", port):
        await asyncio.Future()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    asyncio.run(main(args.port))
