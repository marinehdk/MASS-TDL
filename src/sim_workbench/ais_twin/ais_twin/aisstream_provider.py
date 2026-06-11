from __future__ import annotations

import asyncio
from datetime import datetime, timezone
import json
from typing import Any

from ais_twin.model import BBox, CanonicalAISRecord


def build_subscription_message(api_key: str, bbox: BBox) -> dict[str, Any]:
    return {
        "APIKey": api_key,
        "BoundingBoxes": [[[bbox.lat_min, bbox.lon_min], [bbox.lat_max, bbox.lon_max]]],
        "FilterMessageTypes": ["PositionReport"],
    }


def _parse_aisstream_time(value: str | None) -> datetime:
    cleaned = value.replace(" +0000 UTC", "+00:00")
    return datetime.fromisoformat(cleaned).astimezone(timezone.utc)


def _parse_optional_float(value: Any) -> float | None:
    if value is None:
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def parse_aisstream_message(raw: dict[str, Any], provider: str = "aisstream") -> CanonicalAISRecord | None:
    if raw.get("MessageType") != "PositionReport":
        return None
    body = raw.get("Message", {}).get("PositionReport", {})
    if not body.get("Valid", True):
        return None
    metadata = raw.get("MetaData") or raw.get("Metadata") or {}
    try:
        mmsi = int(metadata.get("MMSI") or body["UserID"])
        lat = float(body.get("Latitude", metadata.get("Latitude", metadata.get("latitude"))))
        lon = float(body.get("Longitude", metadata.get("Longitude", metadata.get("longitude"))))
    except (KeyError, TypeError, ValueError):
        return None
    if not (-90.0 <= lat <= 90.0 and -180.0 <= lon <= 180.0):
        return None

    received_at_utc = datetime.now(timezone.utc)
    flags: set[str] = set()
    try:
        ais_time_utc = _parse_aisstream_time(metadata.get("time_utc"))
    except (AttributeError, TypeError, ValueError):
        ais_time_utc = received_at_utc
        flags.add("missing_ais_time")

    heading_raw = body.get("TrueHeading")
    heading_value = _parse_optional_float(heading_raw)
    heading = None
    if heading_value is None or heading_value < 0.0 or heading_value >= 360.0:
        flags.add("missing_heading")
    else:
        heading = heading_value

    sog_raw = body.get("Sog")
    sog = _parse_optional_float(sog_raw)
    if sog is None:
        flags.add("missing_sog")

    cog_raw = body.get("Cog")
    cog = _parse_optional_float(cog_raw)
    if cog is None:
        flags.add("missing_cog")

    return CanonicalAISRecord(
        provider=provider,
        received_at_utc=received_at_utc,
        ais_time_utc=ais_time_utc,
        mmsi=mmsi,
        lat=lat,
        lon=lon,
        sog_kn=sog,
        cog_deg=cog,
        heading_deg=heading,
        nav_status=str(body.get("NavigationalStatus")) if body.get("NavigationalStatus") is not None else None,
        ship_name=metadata.get("ShipName"),
        ship_type=None,
        raw_message_type="PositionReport",
        raw_json=raw,
        quality_flags=frozenset(flags),
    )


def parse_aisstream_json(message_json: str) -> CanonicalAISRecord | None:
    try:
        raw = json.loads(message_json)
    except (json.JSONDecodeError, TypeError):
        return None
    if not isinstance(raw, dict):
        return None
    try:
        return parse_aisstream_message(raw)
    except Exception:
        return None


class AISstreamProvider:
    def __init__(self, api_key: str, reconnect_base_s: float = 1.0, reconnect_max_s: float = 60.0):
        self.api_key = api_key
        self.reconnect_base_s = reconnect_base_s
        self.reconnect_max_s = reconnect_max_s
        self.url = "wss://stream.aisstream.io/v0/stream"

    async def records(self, bbox: BBox):
        import websockets

        delay = self.reconnect_base_s
        while True:
            try:
                async with websockets.connect(self.url) as websocket:
                    await websocket.send(json.dumps(build_subscription_message(self.api_key, bbox)))
                    delay = self.reconnect_base_s
                    async for message_json in websocket:
                        record = parse_aisstream_json(message_json)
                        if record is not None:
                            yield record
            except Exception:
                await asyncio.sleep(delay)
                delay = min(delay * 2.0, self.reconnect_max_s)
