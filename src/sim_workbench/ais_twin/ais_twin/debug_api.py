from __future__ import annotations

import argparse
import asyncio
import csv
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import os
from pathlib import Path
import threading
from typing import Any
from urllib.parse import parse_qs, urlparse

from ais_twin.aisstream_provider import AISstreamProvider, parse_aisstream_message
from ais_twin.model import BBox, CanonicalAISRecord


REGION_BBOXES = {
    "coastal_archipelago": BBox(lat_min=-4.6, lat_max=-1.1, lon_min=104.7, lon_max=108.7),
    "trondelag": BBox(lat_min=63.0, lat_max=64.2, lon_min=9.0, lon_max=12.0),
}
REGION_ALIASES = {
    "malacca": "coastal_archipelago",
    "safe_route": "coastal_archipelago",
}


class LiveAisTargetStore:
    def __init__(self, limit: int = 200):
        self.limit = limit
        self._lock = threading.Lock()
        self._latest_by_mmsi: dict[int, tuple[datetime, str, dict[str, Any]]] = {}
        self._static_by_mmsi: dict[int, dict[str, Any]] = {}

    def update(self, record: CanonicalAISRecord, region: str = "default") -> None:
        with self._lock:
            static = self._static_by_mmsi.get(record.mmsi, {})
        target = {
            "target_id": record.mmsi,
            "lat": record.lat,
            "lon": record.lon,
            "sog_kn": record.sog_kn,
            "cog_deg": record.cog_deg,
            "heading_deg": record.heading_deg,
            "source_sensor": "ais",
            "received_at_utc": record.received_at_utc.isoformat(),
            "ship_name": record.ship_name or static.get("ship_name"),
            "ship_type": record.ship_type or static.get("ship_type"),
            "destination": static.get("destination"),
            "nav_status": record.nav_status,
            "vessel_length_m": static.get("vessel_length_m"),
            "vessel_beam_m": static.get("vessel_beam_m"),
        }
        with self._lock:
            existing = self._latest_by_mmsi.get(record.mmsi)
            if existing is None or record.received_at_utc >= existing[0]:
                self._latest_by_mmsi[record.mmsi] = (record.received_at_utc, region, target)

    def update_static(self, raw: dict[str, Any]) -> bool:
        static = extract_ship_static(raw)
        if static is None:
            return False
        mmsi = int(static.pop("mmsi"))
        with self._lock:
            self._static_by_mmsi[mmsi] = static
            existing = self._latest_by_mmsi.get(mmsi)
            if existing is not None:
                received_at, region, target = existing
                self._latest_by_mmsi[mmsi] = (received_at, region, {**target, **static})
        return True

    def targets(self, region: str | None = None) -> list[dict[str, Any]]:
        with self._lock:
            rows = list(self._latest_by_mmsi.values())
        if region:
            rows = [row for row in rows if row[1] == region]
        rows.sort(key=lambda row: (row[0], row[2]["target_id"]), reverse=True)
        return [row[2] for row in rows[: self.limit]]


def _ship_type_label(value: Any) -> str | None:
    if value is None:
        return None
    if isinstance(value, str):
        text = value.strip()
        if not text:
            return None
        if text.isdigit():
            value = int(text)
        else:
            return text.lower().replace(" ", "_")
    try:
        code = int(value)
    except (TypeError, ValueError):
        return None
    if 60 <= code <= 69:
        return "passenger"
    if 70 <= code <= 79:
        return "cargo"
    if 80 <= code <= 89:
        return "tanker"
    if code == 30:
        return "fishing"
    if code in {31, 32, 52}:
        return "tug"
    if code in {35, 36, 37}:
        return "service"
    return "other"


def _dimension_value(dimension: dict[str, Any], key: str) -> float | None:
    value = dimension.get(key) or dimension.get(key.lower())
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def extract_ship_static(raw: dict[str, Any]) -> dict[str, Any] | None:
    if raw.get("MessageType") != "ShipStaticData":
        return None
    body = raw.get("Message", {}).get("ShipStaticData", {})
    metadata = raw.get("MetaData") or raw.get("Metadata") or {}
    try:
        mmsi = int(metadata.get("MMSI") or body["UserID"])
    except (KeyError, TypeError, ValueError):
        return None

    dimension = body.get("Dimension") or body.get("Dimensions") or {}
    length_parts = [_dimension_value(dimension, "A"), _dimension_value(dimension, "B")]
    beam_parts = [_dimension_value(dimension, "C"), _dimension_value(dimension, "D")]
    length = sum(part for part in length_parts if part is not None) or None
    beam = sum(part for part in beam_parts if part is not None) or None

    return {
        "mmsi": mmsi,
        "ship_name": (body.get("Name") or body.get("ShipName") or metadata.get("ShipName") or "").strip() or None,
        "ship_type": _ship_type_label(body.get("Type") or body.get("ShipType")),
        "destination": (body.get("Destination") or "").strip() or None,
        "vessel_length_m": length,
        "vessel_beam_m": beam,
    }


def normalize_region(value: str | None) -> str | None:
    if not value:
        return None
    raw = value.strip()
    return REGION_ALIASES.get(raw, raw)


def parse_bbox_arg(value: str) -> BBox:
    parts = [part.strip() for part in value.split(",")]
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("bbox must be lat_min,lon_min,lat_max,lon_max")
    try:
        lat_min, lon_min, lat_max, lon_max = [float(part) for part in parts]
    except ValueError as exc:
        raise argparse.ArgumentTypeError("bbox values must be numbers") from exc
    if not (-90.0 <= lat_min <= 90.0 and -90.0 <= lat_max <= 90.0):
        raise argparse.ArgumentTypeError("bbox latitudes must be within [-90, 90]")
    if not (-180.0 <= lon_min <= 180.0 and -180.0 <= lon_max <= 180.0):
        raise argparse.ArgumentTypeError("bbox longitudes must be within [-180, 180]")
    if lat_min >= lat_max or lon_min >= lon_max:
        raise argparse.ArgumentTypeError("bbox minimums must be lower than maximums")
    return BBox(lat_min=lat_min, lat_max=lat_max, lon_min=lon_min, lon_max=lon_max)


async def _run_live_ais(store: LiveAisTargetStore, api_key: str, bbox: BBox, region: str) -> None:
    provider = AISstreamProvider(api_key=api_key)
    async for raw in provider.messages(bbox, message_types=["PositionReport", "ShipStaticData"]):
        if raw.get("MessageType") == "ShipStaticData":
            store.update_static(raw)
            continue
        record = parse_aisstream_message(raw)
        if record is not None:
            store.update(record, region=region)


def start_live_ais_thread(store: LiveAisTargetStore, api_key: str, bbox: BBox, region: str) -> threading.Thread:
    def runner() -> None:
        asyncio.run(_run_live_ais(store, api_key, bbox, region))

    thread = threading.Thread(target=runner, name=f"aisstream-{region}", daemon=True)
    thread.start()
    return thread


def latest_targets_response(
    targets: list[dict[str, Any]], provider: str, api_key: str | None = None, region: str | None = None
) -> dict[str, Any]:
    return {
        "provider": provider,
        "region": region,
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
    live_store: LiveAisTargetStore | None = None
    provider = "aisstream"
    tracks_path: Path | None = None
    limit = 200

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path != "/api/ais/latest":
            self.send_response(404)
            self.end_headers()
            return
        qs = parse_qs(parsed.query)
        region = normalize_region(qs.get("region", [None])[0])
        if self.live_store is not None:
            targets = self.live_store.targets(region=region)
        else:
            targets = current_targets(self.tracks_path, self.limit, self.latest_targets)
        payload = latest_targets_response(targets, self.provider, region=region)
        body = json.dumps(payload).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def parse_args(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--tracks", default=None)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", default=8095, type=int)
    parser.add_argument("--limit", default=200, type=int)
    parser.add_argument("--provider", default="aisstream")
    parser.add_argument("--bbox", action="append", type=parse_bbox_arg, default=[])
    parser.add_argument("--region", action="append", choices=sorted(REGION_BBOXES), default=[])
    parser.add_argument("--api-key-env", default="AISSTREAM_API_KEY")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)

    DebugHandler.tracks_path = Path(args.tracks) if args.tracks else None
    DebugHandler.limit = args.limit
    DebugHandler.provider = args.provider
    DebugHandler.live_store = None

    live_sources: list[tuple[str, BBox]] = []
    for region in args.region:
        live_sources.append((region, REGION_BBOXES[region]))
    for idx, bbox in enumerate(args.bbox):
        live_sources.append((f"bbox_{idx + 1}", bbox))
    if live_sources:
        api_key = os.environ.get(args.api_key_env)
        if not api_key:
            raise SystemExit(f"{args.api_key_env} is required for live AISstream mode")
        store = LiveAisTargetStore(limit=args.limit)
        DebugHandler.live_store = store
        for region, bbox in live_sources:
            start_live_ais_thread(store, api_key, bbox, region)

    server = HTTPServer((args.host, args.port), DebugHandler)
    server.serve_forever()


if __name__ == "__main__":
    main()
