from __future__ import annotations

import json
from typing import Any


KNOWN_KINDS = {"targets", "ownship", "environment", "route_in", "route_out_path"}


def encode_payload(payload: dict[str, Any]) -> bytes:
    return (json.dumps(payload, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")


def decode_line(line: bytes | str) -> dict[str, Any]:
    if isinstance(line, bytes):
        line = line.decode("utf-8")
    payload = json.loads(line)
    if not isinstance(payload, dict):
        raise ValueError("IPC payload must be a JSON object")
    kind = payload.get("kind")
    if kind not in KNOWN_KINDS:
        raise ValueError(f"IPC payload kind must be one of {sorted(KNOWN_KINDS)}")
    return payload
