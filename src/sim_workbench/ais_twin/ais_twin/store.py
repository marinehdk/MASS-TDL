from __future__ import annotations

import csv
from collections.abc import Mapping
from dataclasses import fields, is_dataclass
from datetime import datetime
import hashlib
import json
from pathlib import Path
from typing import Any

import yaml

from ais_twin.model import BBox, CanonicalAISRecord, TrackSegment


class DatasetStore:
    _ARTIFACT_NAMES = ("raw.jsonl", "tracks.csv", "manifest.yaml")

    def __init__(self, root: Path | str, overwrite: bool = False) -> None:
        self.root = Path(root)
        self.root.mkdir(parents=True, exist_ok=True)
        artifacts = [self.root / name for name in self._ARTIFACT_NAMES]
        existing = [path for path in artifacts if path.exists()]
        if existing and not overwrite:
            raise FileExistsError(f"dataset artifacts already exist: {', '.join(str(path) for path in existing)}")
        if overwrite:
            for path in existing:
                path.unlink()

    def write_raw(self, record: CanonicalAISRecord) -> Path:
        path = self.root / "raw.jsonl"
        with path.open("a", encoding="utf-8") as fp:
            fp.write(json.dumps(_jsonable(record), sort_keys=True, separators=(",", ":")) + "\n")
        return path

    def write_tracks(self, segments: list[TrackSegment]) -> Path:
        path = self.root / "tracks.csv"
        with path.open("w", encoding="utf-8", newline="") as fp:
            writer = csv.DictWriter(fp, fieldnames=["mmsi", "t_s", "lat", "lon", "sog_kn", "cog_deg", "heading_deg"])
            writer.writeheader()
            for segment in segments:
                for point in segment.points:
                    writer.writerow(_jsonable(point))
        return path

    def write_manifest(
        self,
        provider: str,
        bbox: BBox,
        route_path: str,
        capture_duration_hours: float,
        records_written: int,
        segments_written: int,
        api_key: str | None,
    ) -> Path:
        raw_path = self.root / "raw.jsonl"
        tracks_path = self.root / "tracks.csv"
        path = self.root / "manifest.yaml"
        manifest = {
            "provider": provider,
            "bbox": _jsonable(bbox),
            "route_path": route_path,
            "capture_duration_hours": capture_duration_hours,
            "records_written": records_written,
            "segments_written": segments_written,
            "raw_sha256": _sha256(raw_path),
            "tracks_sha256": _sha256(tracks_path),
            "time_alignment": "real_time_trim",
            "api_key_present": bool(api_key),
        }
        path.write_text(yaml.safe_dump(manifest, sort_keys=False, allow_unicode=True), encoding="utf-8")
        return path


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    if not path.exists():
        return digest.hexdigest()
    with path.open("rb") as fp:
        for chunk in iter(lambda: fp.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _jsonable(value: Any) -> Any:
    if isinstance(value, datetime):
        return value.isoformat()
    if isinstance(value, Mapping):
        return {str(k): _jsonable(v) for k, v in value.items()}
    if isinstance(value, (tuple, list)):
        return [_jsonable(v) for v in value]
    if isinstance(value, (frozenset, set)):
        return sorted(_jsonable(v) for v in value)
    if is_dataclass(value) and not isinstance(value, type):
        return {field.name: _jsonable(getattr(value, field.name)) for field in fields(value)}
    return value
