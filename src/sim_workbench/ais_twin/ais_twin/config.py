from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import yaml

from ais_twin.model import BBox


@dataclass(frozen=True)
class AisTwinConfig:
    provider: str
    route_path: Path
    bbox: BBox
    capture_duration_hours: float
    risk_top_n: int
    output_dir: Path


def load_config(path: Path) -> AisTwinConfig:
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    bbox_data = data["bbox"]
    return AisTwinConfig(
        provider=str(data["provider"]),
        route_path=Path(data["route_path"]),
        bbox=BBox(
            lat_min=float(bbox_data["lat_min"]),
            lat_max=float(bbox_data["lat_max"]),
            lon_min=float(bbox_data["lon_min"]),
            lon_max=float(bbox_data["lon_max"]),
        ),
        capture_duration_hours=float(data["capture_duration_hours"]),
        risk_top_n=int(data["risk_top_n"]),
        output_dir=Path(data["output_dir"]),
    )
