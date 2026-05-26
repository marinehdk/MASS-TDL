#!/usr/bin/env python3
"""Real-time 8h integration test monitoring dashboard.

HTTP GET / → serves embedded HTML dashboard.
WebSocket ws://localhost:8888/ws → pushes JSON state every 5s.
Screenshot: POST /screenshot?name=cp_A → saves evidence/dashboard_snapshots/cp_A.png

Usage:
    python3 tools/sil/dashboard_server.py \
        --evidence-dir docs/Design/Phase\ 3/D3.7-sil-8module-integration/evidence \
        --state-file /tmp/d37_orch_state.json \
        --p0-alert-file /tmp/d37_p0_alerts.json \
        --port 8888
"""
from __future__ import annotations

import argparse
import asyncio
import csv
import json
import subprocess
import sys
import time
from pathlib import Path
from typing import Set

try:
    import websockets
    from websockets.server import WebSocketServerProtocol
except ImportError:
    print("ERROR: pip install websockets", file=sys.stderr)
    sys.exit(1)

DASHBOARD_HTML = """<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>D3.7 SIL 8h Monitor</title>
<style>
  body { font-family: monospace; background: #0d1117; color: #c9d1d9; margin: 0; padding: 16px; }
  h1 { color: #58a6ff; } .panel { border: 1px solid #30363d; padding: 12px; margin: 8px 0; border-radius: 6px; }
  .led-ok { display:inline-block; width:12px; height:12px; border-radius:50%; background:#3fb950; margin-right:6px; }
  .led-fail { display:inline-block; width:12px; height:12px; border-radius:50%; background:#f85149; margin-right:6px; }
  .led-unk { display:inline-block; width:12px; height:12px; border-radius:50%; background:#6e7681; margin-right:6px; }
  table { border-collapse: collapse; width:100%; }
  td,th { border: 1px solid #30363d; padding: 4px 8px; text-align: right; }
  th { text-align: left; background: #161b22; }
  .ok { color: #3fb950; } .warn { color: #e3b341; } .fail { color: #f85149; }
</style></head><body>
<h1>D3.7 SIL 8h Integration Monitor</h1>
<div id="status">Connecting...</div>
<div class="panel" id="p0panel"><b>P0 Conditions</b><div id="p0leds"></div></div>
<div class="panel"><b>Scenario Progress</b><div id="progress"></div></div>
<div class="panel"><b>Module Heartbeat (M1–M8)</b><div id="heartbeat"></div></div>
<div class="panel"><b>Latency (key topics, p99 last window)</b><div id="latency"></div></div>
<div class="panel"><b>ASDR Integrity</b><div id="asdr"></div></div>
<div class="panel"><b>Checkpoints</b><div id="checkpoints"></div></div>
<script>
const ws = new WebSocket("ws://" + location.host + "/ws");
ws.onmessage = e => { const d = JSON.parse(e.data); render(d); };
ws.onclose = () => { document.getElementById("status").textContent = "⚠ disconnected"; };
function led(ok) { return ok === null ? '<span class="led-unk"></span>' : ok ? '<span class="led-ok"></span>' : '<span class="led-fail"></span>'; }
function render(d) {
  document.getElementById("status").textContent = "Last update: " + new Date(d.ts*1000).toISOString();
  const p0names = ["process_alive","path_s_clean","m7_heartbeat","asdr_ok","reflex_ok","veto_ok"];
  document.getElementById("p0leds").innerHTML = p0names.map(n =>
    led(d.p0 ? d.p0[n] : null) + n).join("<br>");
  const orch = d.orchestrator || {};
  document.getElementById("progress").innerHTML =
    `Segment: ${orch.segment||"?"}/3 | Scenario: ${orch.scenario_current||"?"}/${orch.scenario_total||50} | ` +
    `Elapsed: ${orch.elapsed_h||"?"}h`;
  const hb = d.heartbeat || {};
  document.getElementById("heartbeat").innerHTML = [1,2,3,4,5,6,7,8].map(i =>
    led(hb["m"+i]) + "M"+i).join(" | ");
  const lat = d.latency || [];
  const keyRows = [9, 11, 16, 17, 22, 19];
  document.getElementById("latency").innerHTML = "<table><tr><th>Row</th><th>Topic</th><th>p99 ms</th><th>Thr</th><th>Status</th></tr>" +
    lat.filter(r => keyRows.includes(r.id)).map(r =>
      `<tr><td>${r.id}</td><td>${r.topic.split("/").pop()}</td>` +
      `<td class="${r.status==='OK'?'ok':r.status==='NO_DATA'?'':'fail'}">${r.p99||"N/A"}</td>` +
      `<td>${r.threshold}</td><td>${r.status}</td></tr>`
    ).join("") + "</table>";
  const asdr = d.asdr || {};
  document.getElementById("asdr").innerHTML =
    `Total: ${asdr.total||0} | PASS: ${asdr.pass||0} | FAIL: <span class="${(asdr.fail||0)>0?'fail':'ok'}">${asdr.fail||0}</span>`;
  const cp = d.checkpoints || {};
  document.getElementById("checkpoints").innerHTML =
    `CP-A: ${cp.A||"⏳ waiting"} | CP-B: ${cp.B||"⏳ waiting"}`;
}
</script></body></html>"""


def _read_latency_latest(evidence_dir: Path) -> list[dict]:
    csv_path = evidence_dir / "latency_8h.csv"
    if not csv_path.exists():
        return []
    latest: dict[int, dict] = {}
    try:
        with open(csv_path) as f:
            for row in csv.DictReader(f):
                pid = int(row["topic_pair_id"])
                latest[pid] = {
                    "id": pid, "topic": row["topic"],
                    "p99": row["p99_ms"], "threshold": row["threshold_ms"],
                    "status": row["status"],
                }
    except Exception:
        pass
    return list(latest.values())


def _read_asdr_counts(evidence_dir: Path) -> dict:
    csv_path = evidence_dir / "asdr_integrity.csv"
    counts = {"total": 0, "pass": 0, "fail": 0}
    if not csv_path.exists():
        return counts
    try:
        with open(csv_path) as f:
            for row in csv.DictReader(f):
                counts["total"] += 1
                if row.get("status") == "PASS":
                    counts["pass"] += 1
                elif row.get("status") == "FAIL":
                    counts["fail"] += 1
    except Exception:
        pass
    return counts


def _build_state(evidence_dir: Path, state_file: Path | None,
                 p0_alert_file: Path | None) -> dict:
    orch = {}
    if state_file and state_file.exists():
        try:
            orch = json.loads(state_file.read_text())
        except Exception:
            pass

    p0_alerts = []
    if p0_alert_file and p0_alert_file.exists():
        try:
            p0_alerts = json.loads(p0_alert_file.read_text())
        except Exception:
            pass

    cp_a = cp_b = None
    for cp_file in [
        evidence_dir / "seg1" / "checkpoint_A.json",
        evidence_dir / "seg2" / "checkpoint_B.json",
    ]:
        if cp_file.exists():
            try:
                data = json.loads(cp_file.read_text())
                ts = data.get("timestamp", "")
                if "A" in cp_file.name:
                    cp_a = f"✅ {ts}"
                else:
                    cp_b = f"✅ {ts}"
            except Exception:
                pass

    return {
        "ts": time.time(),
        "orchestrator": orch,
        "p0": {
            "process_alive": len([a for a in p0_alerts if a.get("type") == "P0-1"]) == 0 or None,
            "path_s_clean": len([a for a in p0_alerts if a.get("type") == "P0-2"]) == 0 or None,
            "m7_heartbeat": len([a for a in p0_alerts if a.get("type") == "P0-5"]) == 0 or None,
            "asdr_ok": len([a for a in p0_alerts if a.get("type") == "P0-6"]) == 0 or None,
            "reflex_ok": None,
            "veto_ok": None,
        },
        "heartbeat": {f"m{i}": None for i in range(1, 9)},
        "latency": _read_latency_latest(evidence_dir),
        "asdr": _read_asdr_counts(evidence_dir),
        "checkpoints": {"A": cp_a, "B": cp_b},
    }


async def ws_handler(websocket: "WebSocketServerProtocol",
                     evidence_dir: Path, state_file: Path | None,
                     p0_alert_file: Path | None, clients: Set):
    clients.add(websocket)
    try:
        async for _ in websocket:
            pass
    finally:
        clients.discard(websocket)


async def broadcast_loop(evidence_dir: Path, state_file: Path | None,
                         p0_alert_file: Path | None, clients: Set):
    while True:
        if clients:
            state = _build_state(evidence_dir, state_file, p0_alert_file)
            payload = json.dumps(state)
            await asyncio.gather(
                *[ws.send(payload) for ws in set(clients)],
                return_exceptions=True,
            )
        await asyncio.sleep(5)


async def http_handler(reader: asyncio.StreamReader, writer: asyncio.StreamWriter,
                       evidence_dir: Path, state_file: Path | None,
                       p0_alert_file: Path | None):
    try:
        data = await asyncio.wait_for(reader.read(1024), timeout=5)
        line = data.decode(errors="ignore").split("\n")[0]
        if "POST /screenshot" in line:
            name = "snapshot"
            if "name=" in line:
                name = line.split("name=")[1].split(" ")[0].strip()
            snap_dir = evidence_dir / "dashboard_snapshots"
            snap_dir.mkdir(parents=True, exist_ok=True)
            out_path = snap_dir / f"{name}.png"
            try:
                subprocess.run(
                    ["chromium", "--headless", "--screenshot", str(out_path),
                     "http://localhost:8888"],
                    timeout=15, capture_output=True,
                )
            except Exception:
                pass
            writer.write(b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK")
        else:
            body = DASHBOARD_HTML.encode()
            writer.write(
                b"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"
                + f"Content-Length: {len(body)}\r\n\r\n".encode()
                + body
            )
    except Exception:
        pass
    finally:
        try:
            writer.close()
        except Exception:
            pass


async def main_async(args):
    evidence_dir = args.evidence_dir
    state_file = args.state_file
    p0_alert_file = args.p0_alert_file
    port = args.port

    clients: Set = set()

    async def _ws_handler(ws, path="/ws"):
        await ws_handler(ws, evidence_dir, state_file, p0_alert_file, clients)

    ws_server = await websockets.serve(_ws_handler, "0.0.0.0", port)

    async def _http_factory(r, w):
        await http_handler(r, w, evidence_dir, state_file, p0_alert_file)

    http_server = await asyncio.start_server(_http_factory, "0.0.0.0", port)
    print(f"Dashboard: http://localhost:{port+1}  WebSocket: ws://localhost:{port}/ws")

    await asyncio.gather(
        ws_server.wait_closed(),
        broadcast_loop(evidence_dir, state_file, p0_alert_file, clients),
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--evidence-dir", required=True, type=Path)
    ap.add_argument("--state-file", type=Path, default=None)
    ap.add_argument("--p0-alert-file", type=Path, default=None)
    ap.add_argument("--port", type=int, default=8888)
    args = ap.parse_args()
    asyncio.run(main_async(args))


if __name__ == "__main__":
    main()
