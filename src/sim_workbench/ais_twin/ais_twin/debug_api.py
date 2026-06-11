from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
from pathlib import Path
from typing import Any


def latest_targets_response(
    targets: list[dict[str, Any]], provider: str, api_key: str | None = None
) -> dict[str, Any]:
    return {
        "provider": provider,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "target_count": len(targets),
        "targets": targets,
    }


def load_latest_targets_from_tracks(tracks_path: Path, limit: int = 200) -> list[dict[str, Any]]:
    if not tracks_path.exists():
        return []

    latest_by_mmsi: dict[int, tuple[float, dict[str, Any]]] = {}
    with tracks_path.open(encoding="utf-8", newline="") as fp:
        for row in csv.DictReader(fp):
            mmsi = int(row["mmsi"])
            t_s = float(row["t_s"])
            target = {
                "target_id": mmsi,
                "lat": float(row["lat"]),
                "lon": float(row["lon"]),
                "sog_kn": float(row["sog_kn"]),
                "cog_deg": float(row["cog_deg"]),
                "heading_deg": float(row["heading_deg"]) if row["heading_deg"] else None,
                "source_sensor": "ais",
            }
            existing = latest_by_mmsi.get(mmsi)
            if existing is None or t_s > existing[0]:
                latest_by_mmsi[mmsi] = (t_s, target)

    return [latest_by_mmsi[mmsi][1] for mmsi in sorted(latest_by_mmsi)[:limit]]


def current_targets(
    tracks_path: Path | None,
    limit: int,
    fallback_targets: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    if tracks_path is None:
        return fallback_targets
    return load_latest_targets_from_tracks(tracks_path, limit=limit)


class DebugHandler(BaseHTTPRequestHandler):
    latest_targets: list[dict[str, Any]] = []
    provider = "aisstream"
    tracks_path: Path | None = None
    limit = 200

    def do_GET(self):
        if self.path != "/api/ais/latest":
            self.send_response(404)
            self.end_headers()
            return
        targets = current_targets(self.tracks_path, self.limit, self.latest_targets)
        payload = latest_targets_response(targets, self.provider)
        body = json.dumps(payload).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--tracks", default="data/ais_twin/safe_route/tracks.csv")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", default=8095, type=int)
    parser.add_argument("--limit", default=200, type=int)
    args = parser.parse_args(argv)

    DebugHandler.tracks_path = Path(args.tracks)
    DebugHandler.limit = args.limit
    server = HTTPServer((args.host, args.port), DebugHandler)
    server.serve_forever()


if __name__ == "__main__":
    main()
