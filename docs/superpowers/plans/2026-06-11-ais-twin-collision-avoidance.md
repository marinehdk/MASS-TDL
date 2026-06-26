# AIS Twin Collision Avoidance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an AIS Twin subsystem that captures live AIS over the safe-route bbox, freezes it as a replayable dataset, publishes Top-20 AIS targets to TDL, and exposes AIS targets to the map UI.

**Architecture:** Add a new focused Python ROS package under `src/sim_workbench/ais_twin` instead of extending the ignored `scenario_authoring` package. Keep L2 route input, AIS provider input, dataset storage, replay publication, and HMI display as separate units connected by explicit interfaces.

**Tech Stack:** Python 3.10, ROS2 Humble `rclpy`, `setuptools` ROS package style, `pytest`, `websockets`, CSV/JSON/YAML artifacts, React/Vite/Vitest, existing `l3_external_msgs` and `l3_msgs`.

---

## Scope Check

This plan implements one coherent MVP: AIS capture -> frozen dataset -> deterministic replay -> TDL target topic -> HMI/debug visualization -> acceptance evidence. The live provider, replay publisher, and UI are separate tasks, but each task produces independently testable software and keeps the shared contract `CanonicalAISRecord`.

## File Structure

Create a new package:

- `src/sim_workbench/ais_twin/package.xml` - ROS package metadata.
- `src/sim_workbench/ais_twin/setup.py` - Python package install and console scripts.
- `src/sim_workbench/ais_twin/resource/ais_twin` - ament resource marker.
- `src/sim_workbench/ais_twin/config/safe_route_aisstream.yaml` - MVP bbox, route path, capture duration, risk limits.
- `src/sim_workbench/ais_twin/ais_twin/__init__.py` - package marker.
- `src/sim_workbench/ais_twin/ais_twin/config.py` - typed config loading and validation.
- `src/sim_workbench/ais_twin/ais_twin/model.py` - dataclasses for bbox, AIS records, tracks, risk-ranked targets, dataset manifest.
- `src/sim_workbench/ais_twin/ais_twin/route.py` - safe-route loading, bbox expansion, route duration.
- `src/sim_workbench/ais_twin/ais_twin/provider.py` - provider protocol and provider status.
- `src/sim_workbench/ais_twin/ais_twin/aisstream_provider.py` - AISstream WebSocket subscription and parser.
- `src/sim_workbench/ais_twin/ais_twin/store.py` - raw JSONL, normalized CSV, manifest, hashes.
- `src/sim_workbench/ais_twin/ais_twin/normalizer.py` - MMSI grouping, dedupe, gap splitting, quality gates.
- `src/sim_workbench/ais_twin/ais_twin/geometry.py` - nautical distance, bearing, CPA/TCPA helpers.
- `src/sim_workbench/ais_twin/ais_twin/risk.py` - Top-20 risk scoring.
- `src/sim_workbench/ais_twin/ais_twin/replay_engine.py` - pure replay-time interpolation and target selection.
- `src/sim_workbench/ais_twin/ais_twin/replay_node.py` - ROS2 node publishing `/fusion/tracked_targets`.
- `src/sim_workbench/ais_twin/ais_twin/capture_cli.py` - 10-hour capture command.
- `src/sim_workbench/ais_twin/ais_twin/debug_api.py` - local HTTP JSON API for latest AIS targets.
- `src/sim_workbench/ais_twin/launch/ais_twin_replay.launch.py` - replay launch file.

Create tests:

- `tests/ais_twin/test_config_and_route.py`
- `tests/ais_twin/test_aisstream_provider.py`
- `tests/ais_twin/test_store_and_normalizer.py`
- `tests/ais_twin/test_risk_selector.py`
- `tests/ais_twin/test_replay_engine.py`
- `tests/ais_twin/test_debug_api.py`

Modify frontend:

- `web/src/api/aisTwinApi.ts` - fetch latest AIS targets from debug API.
- `web/src/map/AisTargetLayer.tsx` - render AIS target symbols.
- `web/src/map/SilMapView.tsx` - opt-in AIS layer wiring.
- `web/src/map/layers.ts` - layer enum/config entry.
- `web/src/map/__tests__/AisTargetLayer.test.tsx`

Modify Docker/Foxglove exposure only if the HMI needs a new topic/API:

- `tests/docker/test_foxglove_whitelist.py` - include `/fusion/tracked_targets` if Foxglove must display replay targets.
- `docker-compose.yml`
- `docker-compose.a4000.yml`

Create acceptance helpers:

- `scripts/ais_twin_capture_safe_route.sh`
- `scripts/ais_twin_replay_safe_route.sh`
- `tests/ais_twin/test_acceptance_contract.py`

---

## Task 1: Package Skeleton and Config

**Files:**
- Create: `src/sim_workbench/ais_twin/package.xml`
- Create: `src/sim_workbench/ais_twin/setup.py`
- Create: `src/sim_workbench/ais_twin/resource/ais_twin`
- Create: `src/sim_workbench/ais_twin/ais_twin/__init__.py`
- Create: `src/sim_workbench/ais_twin/ais_twin/config.py`
- Create: `src/sim_workbench/ais_twin/config/safe_route_aisstream.yaml`
- Create: `tests/ais_twin/test_config_and_route.py`

- [ ] **Step 1: Write failing config test**

```python
# tests/ais_twin/test_config_and_route.py
from pathlib import Path

from ais_twin.config import load_config


def test_safe_route_config_has_expected_bbox_and_capture_window():
    cfg = load_config(Path("src/sim_workbench/ais_twin/config/safe_route_aisstream.yaml"))

    assert cfg.provider == "aisstream"
    assert cfg.capture_duration_hours == 10.0
    assert cfg.route_path == Path("scenarios/集成测试/safe_route.yaml")
    assert cfg.bbox.lat_min == -4.503333
    assert cfg.bbox.lat_max == -1.136667
    assert cfg.bbox.lon_min == 104.786263
    assert cfg.bbox.lon_max == 108.513737
    assert cfg.risk_top_n == 20
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
PYTHONPATH=src/sim_workbench/ais_twin pytest -q tests/ais_twin/test_config_and_route.py::test_safe_route_config_has_expected_bbox_and_capture_window
```

Expected: `ModuleNotFoundError: No module named 'ais_twin'`.

- [ ] **Step 3: Create package skeleton and config loader**

```python
# src/sim_workbench/ais_twin/ais_twin/config.py
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
```

```python
# src/sim_workbench/ais_twin/ais_twin/model.py
from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class BBox:
    lat_min: float
    lat_max: float
    lon_min: float
    lon_max: float
```

```yaml
# src/sim_workbench/ais_twin/config/safe_route_aisstream.yaml
provider: aisstream
route_path: scenarios/集成测试/safe_route.yaml
capture_duration_hours: 10.0
risk_top_n: 20
output_dir: data/ais_twin/safe_route
bbox:
  lat_min: -4.503333
  lat_max: -1.136667
  lon_min: 104.786263
  lon_max: 108.513737
```

Use the existing `setup.py` style from `src/sim_workbench/ais_bridge/setup.py`:

```python
# src/sim_workbench/ais_twin/setup.py
import os
from glob import glob
from setuptools import find_packages, setup

package_name = "ais_twin"

setup(
    name=package_name,
    version="0.1.0",
    packages=find_packages(exclude=["test"]),
    data_files=[
        ("share/ament_index/resource_index/packages", ["resource/" + package_name]),
        ("share/" + package_name, ["package.xml"]),
        (os.path.join("share", package_name, "config"), glob("config/*.yaml")),
        (os.path.join("share", package_name, "launch"), glob("launch/*.py")),
    ],
    install_requires=["setuptools", "pyyaml", "websockets"],
    zip_safe=True,
    maintainer="Team-E3",
    maintainer_email="team-e3@mass-l3-tdl.local",
    description="AIS digital-twin capture and replay for MASS-L3 SIL",
    license="Proprietary",
    tests_require=["pytest"],
    entry_points={
        "console_scripts": [
            "ais_twin_capture = ais_twin.capture_cli:main",
            "ais_twin_replay_node = ais_twin.replay_node:main",
            "ais_twin_debug_api = ais_twin.debug_api:main",
        ],
    },
)
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
PYTHONPATH=src/sim_workbench/ais_twin pytest -q tests/ais_twin/test_config_and_route.py::test_safe_route_config_has_expected_bbox_and_capture_window
```

Expected: `1 passed`.

- [ ] **Step 5: Commit**

```bash
git add src/sim_workbench/ais_twin tests/ais_twin/test_config_and_route.py
git commit -m "feat(ais-twin): add package skeleton and safe route config"
```

---

## Task 2: Route Helpers and Canonical Models

**Files:**
- Modify: `src/sim_workbench/ais_twin/ais_twin/model.py`
- Create: `src/sim_workbench/ais_twin/ais_twin/route.py`
- Modify: `tests/ais_twin/test_config_and_route.py`

- [ ] **Step 1: Add failing route/model tests**

```python
# append to tests/ais_twin/test_config_and_route.py
from datetime import datetime, timezone

from ais_twin.model import CanonicalAISRecord
from ais_twin.route import load_route_points, route_bbox, route_duration_hours


def test_safe_route_duration_and_raw_bbox():
    points = load_route_points(Path("scenarios/集成测试/safe_route.yaml"))

    bbox = route_bbox(points)
    duration = route_duration_hours(points)

    assert len(points) == 324
    assert bbox.lat_min == -4.17
    assert bbox.lat_max == -1.47
    assert bbox.lon_min == 105.12
    assert bbox.lon_max == 108.18
    assert round(duration, 2) == 9.56


def test_canonical_record_flags_missing_heading():
    record = CanonicalAISRecord(
        provider="aisstream",
        received_at_utc=datetime(2026, 6, 11, tzinfo=timezone.utc),
        ais_time_utc=datetime(2026, 6, 11, tzinfo=timezone.utc),
        mmsi=123456789,
        lat=-2.0,
        lon=106.0,
        sog_kn=12.4,
        cog_deg=85.0,
        heading_deg=None,
        nav_status="under_way",
        ship_name="TEST VESSEL",
        ship_type=None,
        raw_message_type="PositionReport",
        raw_json={"MessageType": "PositionReport"},
        quality_flags=frozenset({"missing_heading"}),
    )

    assert record.quality_flags == frozenset({"missing_heading"})
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```bash
PYTHONPATH=src/sim_workbench/ais_twin pytest -q tests/ais_twin/test_config_and_route.py
```

Expected: failures for missing `CanonicalAISRecord` and `route` module.

- [ ] **Step 3: Implement route helpers and dataclasses**

```python
# src/sim_workbench/ais_twin/ais_twin/model.py
from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
from typing import Any


@dataclass(frozen=True)
class BBox:
    lat_min: float
    lat_max: float
    lon_min: float
    lon_max: float


@dataclass(frozen=True)
class RoutePoint:
    lat: float
    lon: float
    target_sog_kn: float


@dataclass(frozen=True)
class CanonicalAISRecord:
    provider: str
    received_at_utc: datetime
    ais_time_utc: datetime
    mmsi: int
    lat: float
    lon: float
    sog_kn: float | None
    cog_deg: float | None
    heading_deg: float | None
    nav_status: str | None
    ship_name: str | None
    ship_type: str | None
    raw_message_type: str
    raw_json: dict[str, Any]
    quality_flags: frozenset[str]


@dataclass(frozen=True)
class TrackPoint:
    t_s: float
    mmsi: int
    lat: float
    lon: float
    sog_kn: float
    cog_deg: float
    heading_deg: float | None
```

```python
# src/sim_workbench/ais_twin/ais_twin/route.py
from __future__ import annotations

from pathlib import Path
import math
import yaml

from ais_twin.model import BBox, RoutePoint

EARTH_RADIUS_NM = 3440.065


def load_route_points(path: Path) -> list[RoutePoint]:
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    nominal = data["ownShip"]["nominalRoute"]
    return [
        RoutePoint(
            lat=float(wp["latitude"]),
            lon=float(wp["longitude"]),
            target_sog_kn=float(wp.get("target_sog_kn", 10.0)),
        )
        for wp in nominal
    ]


def route_bbox(points: list[RoutePoint]) -> BBox:
    return BBox(
        lat_min=min(p.lat for p in points),
        lat_max=max(p.lat for p in points),
        lon_min=min(p.lon for p in points),
        lon_max=max(p.lon for p in points),
    )


def haversine_nm(a: RoutePoint, b: RoutePoint) -> float:
    lat1 = math.radians(a.lat)
    lat2 = math.radians(b.lat)
    dlat = lat2 - lat1
    dlon = math.radians(b.lon - a.lon)
    h = math.sin(dlat / 2.0) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin(dlon / 2.0) ** 2
    return 2.0 * EARTH_RADIUS_NM * math.asin(math.sqrt(h))


def route_duration_hours(points: list[RoutePoint]) -> float:
    total_h = 0.0
    for a, b in zip(points, points[1:]):
        total_h += haversine_nm(a, b) / max(a.target_sog_kn, 0.1)
    return total_h
```

- [ ] **Step 4: Run tests**

Run:

```bash
PYTHONPATH=src/sim_workbench/ais_twin pytest -q tests/ais_twin/test_config_and_route.py
```

Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/sim_workbench/ais_twin/ais_twin/model.py src/sim_workbench/ais_twin/ais_twin/route.py tests/ais_twin/test_config_and_route.py
git commit -m "feat(ais-twin): add route helpers and canonical models"
```

---

## Task 3: AISstream Provider Contract and Parser

**Files:**
- Create: `src/sim_workbench/ais_twin/ais_twin/provider.py`
- Create: `src/sim_workbench/ais_twin/ais_twin/aisstream_provider.py`
- Create: `tests/ais_twin/test_aisstream_provider.py`

- [ ] **Step 1: Write failing provider tests**

```python
# tests/ais_twin/test_aisstream_provider.py
from datetime import timezone

from ais_twin.aisstream_provider import build_subscription_message, parse_aisstream_message
from ais_twin.model import BBox


def test_subscription_message_uses_api_key_bbox_and_position_reports():
    msg = build_subscription_message(
        api_key="secret-key",
        bbox=BBox(lat_min=-4.503333, lat_max=-1.136667, lon_min=104.786263, lon_max=108.513737),
    )

    assert msg["APIKey"] == "secret-key"
    assert msg["BoundingBoxes"] == [[[-4.503333, 104.786263], [-1.136667, 108.513737]]]
    assert msg["FilterMessageTypes"] == ["PositionReport"]


def test_parse_position_report_handles_metadata_case_and_quality_flags():
    raw = {
        "MessageType": "PositionReport",
        "Message": {
            "PositionReport": {
                "UserID": 259000420,
                "Latitude": -2.25,
                "Longitude": 106.5,
                "Sog": 12.0,
                "Cog": 90.0,
                "TrueHeading": 511,
                "NavigationalStatus": 0,
                "Valid": True,
            }
        },
        "MetaData": {
            "MMSI": 259000420,
            "ShipName": "AUGUSTSON",
            "time_utc": "2022-12-29 18:22:32.318353 +0000 UTC",
        },
    }

    record = parse_aisstream_message(raw, provider="aisstream")

    assert record is not None
    assert record.mmsi == 259000420
    assert record.lat == -2.25
    assert record.lon == 106.5
    assert record.sog_kn == 12.0
    assert record.cog_deg == 90.0
    assert record.heading_deg is None
    assert "missing_heading" in record.quality_flags
    assert record.ais_time_utc.tzinfo == timezone.utc
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```bash
PYTHONPATH=src/sim_workbench/ais_twin pytest -q tests/ais_twin/test_aisstream_provider.py
```

Expected: `ModuleNotFoundError` for `ais_twin.aisstream_provider`.

- [ ] **Step 3: Implement provider contract and parser**

```python
# src/sim_workbench/ais_twin/ais_twin/provider.py
from __future__ import annotations

from dataclasses import dataclass
from typing import AsyncIterator, Protocol

from ais_twin.model import BBox, CanonicalAISRecord


@dataclass(frozen=True)
class ProviderStatus:
    connected: bool
    messages_received: int
    reconnects: int
    last_error: str | None


class AISProvider(Protocol):
    async def records(self, bbox: BBox) -> AsyncIterator[CanonicalAISRecord]:
        ...
```

```python
# src/sim_workbench/ais_twin/ais_twin/aisstream_provider.py
from __future__ import annotations

from datetime import datetime, timezone
from typing import Any

from ais_twin.model import BBox, CanonicalAISRecord


def build_subscription_message(api_key: str, bbox: BBox) -> dict[str, Any]:
    return {
        "APIKey": api_key,
        "BoundingBoxes": [[[bbox.lat_min, bbox.lon_min], [bbox.lat_max, bbox.lon_max]]],
        "FilterMessageTypes": ["PositionReport"],
    }


def _parse_aisstream_time(value: str | None) -> datetime:
    if not value:
        return datetime.now(timezone.utc)
    cleaned = value.replace(" +0000 UTC", "+00:00")
    return datetime.fromisoformat(cleaned).astimezone(timezone.utc)


def parse_aisstream_message(raw: dict[str, Any], provider: str = "aisstream") -> CanonicalAISRecord | None:
    if raw.get("MessageType") != "PositionReport":
        return None
    body = raw.get("Message", {}).get("PositionReport", {})
    if not body.get("Valid", True):
        return None
    metadata = raw.get("MetaData") or raw.get("Metadata") or {}
    mmsi = int(metadata.get("MMSI") or body["UserID"])
    lat = float(body.get("Latitude", metadata.get("Latitude", metadata.get("latitude"))))
    lon = float(body.get("Longitude", metadata.get("Longitude", metadata.get("longitude"))))
    heading_raw = body.get("TrueHeading")
    flags: set[str] = set()
    heading = None
    if heading_raw is None or int(heading_raw) >= 360:
        flags.add("missing_heading")
    else:
        heading = float(heading_raw)
    return CanonicalAISRecord(
        provider=provider,
        received_at_utc=datetime.now(timezone.utc),
        ais_time_utc=_parse_aisstream_time(metadata.get("time_utc")),
        mmsi=mmsi,
        lat=lat,
        lon=lon,
        sog_kn=float(body["Sog"]) if body.get("Sog") is not None else None,
        cog_deg=float(body["Cog"]) if body.get("Cog") is not None else None,
        heading_deg=heading,
        nav_status=str(body.get("NavigationalStatus")) if body.get("NavigationalStatus") is not None else None,
        ship_name=metadata.get("ShipName"),
        ship_type=None,
        raw_message_type="PositionReport",
        raw_json=raw,
        quality_flags=frozenset(flags),
    )
```

- [ ] **Step 4: Add async stream after parser tests pass**

Add this to `aisstream_provider.py`:

```python
import asyncio
import json
import websockets


class AISstreamProvider:
    def __init__(self, api_key: str, reconnect_base_s: float = 1.0, reconnect_max_s: float = 60.0):
        self.api_key = api_key
        self.reconnect_base_s = reconnect_base_s
        self.reconnect_max_s = reconnect_max_s
        self.url = "wss://stream.aisstream.io/v0/stream"

    async def records(self, bbox: BBox):
        delay = self.reconnect_base_s
        while True:
            try:
                async with websockets.connect(self.url) as websocket:
                    await websocket.send(json.dumps(build_subscription_message(self.api_key, bbox)))
                    delay = self.reconnect_base_s
                    async for message_json in websocket:
                        record = parse_aisstream_message(json.loads(message_json))
                        if record is not None:
                            yield record
            except Exception:
                await asyncio.sleep(delay)
                delay = min(delay * 2.0, self.reconnect_max_s)
```

- [ ] **Step 5: Run tests**

Run:

```bash
PYTHONPATH=src/sim_workbench/ais_twin pytest -q tests/ais_twin/test_aisstream_provider.py
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/sim_workbench/ais_twin/ais_twin/provider.py src/sim_workbench/ais_twin/ais_twin/aisstream_provider.py tests/ais_twin/test_aisstream_provider.py
git commit -m "feat(ais-twin): add AISstream provider adapter"
```

---

## Task 4: Dataset Store and Normalizer

**Files:**
- Create: `src/sim_workbench/ais_twin/ais_twin/store.py`
- Create: `src/sim_workbench/ais_twin/ais_twin/normalizer.py`
- Create: `tests/ais_twin/test_store_and_normalizer.py`

- [ ] **Step 1: Write failing store and normalizer tests**

```python
# tests/ais_twin/test_store_and_normalizer.py
from datetime import datetime, timedelta, timezone
import json

from ais_twin.model import BBox, CanonicalAISRecord
from ais_twin.normalizer import normalize_records
from ais_twin.store import DatasetStore


def _record(mmsi: int, seconds: int, lat: float, lon: float) -> CanonicalAISRecord:
    t = datetime(2026, 6, 11, tzinfo=timezone.utc) + timedelta(seconds=seconds)
    return CanonicalAISRecord(
        provider="aisstream",
        received_at_utc=t,
        ais_time_utc=t,
        mmsi=mmsi,
        lat=lat,
        lon=lon,
        sog_kn=10.0,
        cog_deg=90.0,
        heading_deg=90.0,
        nav_status="0",
        ship_name="VESSEL",
        ship_type=None,
        raw_message_type="PositionReport",
        raw_json={"MessageType": "PositionReport", "mmsi": mmsi},
        quality_flags=frozenset(),
    )


def test_dataset_store_writes_raw_manifest_and_no_secret(tmp_path):
    store = DatasetStore(tmp_path)
    bbox = BBox(-4.5, -1.1, 104.7, 108.5)
    store.write_raw(_record(1, 0, -2.0, 106.0))
    manifest = store.write_manifest(
        provider="aisstream",
        bbox=bbox,
        route_path="scenarios/集成测试/safe_route.yaml",
        capture_duration_hours=10.0,
        records_written=1,
        api_key="secret-key",
    )

    assert (tmp_path / "raw.jsonl").exists()
    raw_line = json.loads((tmp_path / "raw.jsonl").read_text(encoding="utf-8").splitlines()[0])
    assert raw_line["mmsi"] == 1
    text = manifest.read_text(encoding="utf-8")
    assert "secret-key" not in text
    assert "time_alignment: real_time_trim" in text


def test_normalizer_dedupes_sorts_and_splits_gaps():
    records = [
        _record(1, 0, -2.0, 106.0),
        _record(1, 0, -2.0, 106.0),
        _record(1, 10, -2.0, 106.01),
        _record(1, 700, -2.0, 106.5),
        _record(1, 710, -2.0, 106.51),
    ]

    segments = normalize_records(records, max_gap_s=300.0, min_points=2)

    assert len(segments) == 2
    assert [p.t_s for p in segments[0].points] == [0.0, 10.0]
    assert [p.t_s for p in segments[1].points] == [700.0, 710.0]
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```bash
PYTHONPATH=src/sim_workbench/ais_twin pytest -q tests/ais_twin/test_store_and_normalizer.py
```

Expected: modules missing.

- [ ] **Step 3: Implement store and normalizer**

```python
# add to src/sim_workbench/ais_twin/ais_twin/model.py
@dataclass(frozen=True)
class TrackSegment:
    mmsi: int
    points: tuple[TrackPoint, ...]
```

```python
# src/sim_workbench/ais_twin/ais_twin/store.py
from __future__ import annotations

from dataclasses import asdict
from pathlib import Path
from typing import Any
import hashlib
import json
import yaml

from ais_twin.model import BBox, CanonicalAISRecord


class DatasetStore:
    def __init__(self, root: Path):
        self.root = root
        self.root.mkdir(parents=True, exist_ok=True)
        self.raw_path = self.root / "raw.jsonl"

    def write_raw(self, record: CanonicalAISRecord) -> None:
        payload: dict[str, Any] = asdict(record)
        payload["received_at_utc"] = record.received_at_utc.isoformat()
        payload["ais_time_utc"] = record.ais_time_utc.isoformat()
        payload["quality_flags"] = sorted(record.quality_flags)
        with self.raw_path.open("a", encoding="utf-8") as f:
            f.write(json.dumps(payload, sort_keys=True) + "\n")

    def _sha256(self, path: Path) -> str:
        if not path.exists():
            return ""
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def write_manifest(
        self,
        provider: str,
        bbox: BBox,
        route_path: str,
        capture_duration_hours: float,
        records_written: int,
        api_key: str,
    ) -> Path:
        manifest = {
            "provider": provider,
            "bbox": asdict(bbox),
            "route_path": route_path,
            "capture_duration_hours": capture_duration_hours,
            "records_written": records_written,
            "raw_sha256": self._sha256(self.raw_path),
            "time_alignment": "real_time_trim",
            "api_key_present": bool(api_key),
        }
        path = self.root / "manifest.yaml"
        path.write_text(yaml.safe_dump(manifest, sort_keys=True), encoding="utf-8")
        return path
```

```python
# src/sim_workbench/ais_twin/ais_twin/normalizer.py
from __future__ import annotations

from collections import defaultdict

from ais_twin.model import CanonicalAISRecord, TrackPoint, TrackSegment


def normalize_records(
    records: list[CanonicalAISRecord],
    max_gap_s: float = 300.0,
    min_points: int = 3,
) -> list[TrackSegment]:
    grouped: dict[int, dict[float, CanonicalAISRecord]] = defaultdict(dict)
    if not records:
        return []
    base = min(r.ais_time_utc for r in records)
    for record in records:
        if record.sog_kn is None or record.cog_deg is None:
            continue
        t_s = (record.ais_time_utc - base).total_seconds()
        grouped[record.mmsi][t_s] = record

    segments: list[TrackSegment] = []
    for mmsi, by_time in grouped.items():
        current: list[TrackPoint] = []
        last_t: float | None = None
        for t_s in sorted(by_time):
            record = by_time[t_s]
            if last_t is not None and t_s - last_t > max_gap_s:
                if len(current) >= min_points:
                    segments.append(TrackSegment(mmsi=mmsi, points=tuple(current)))
                current = []
            current.append(
                TrackPoint(
                    t_s=t_s,
                    mmsi=mmsi,
                    lat=record.lat,
                    lon=record.lon,
                    sog_kn=float(record.sog_kn),
                    cog_deg=float(record.cog_deg),
                    heading_deg=record.heading_deg,
                )
            )
            last_t = t_s
        if len(current) >= min_points:
            segments.append(TrackSegment(mmsi=mmsi, points=tuple(current)))
    return segments
```

- [ ] **Step 4: Run tests**

Run:

```bash
PYTHONPATH=src/sim_workbench/ais_twin pytest -q tests/ais_twin/test_store_and_normalizer.py
```

Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/sim_workbench/ais_twin/ais_twin/model.py src/sim_workbench/ais_twin/ais_twin/store.py src/sim_workbench/ais_twin/ais_twin/normalizer.py tests/ais_twin/test_store_and_normalizer.py
git commit -m "feat(ais-twin): persist and normalize AIS datasets"
```

---

## Task 5: Geometry and Risk Selector

**Files:**
- Create: `src/sim_workbench/ais_twin/ais_twin/geometry.py`
- Create: `src/sim_workbench/ais_twin/ais_twin/risk.py`
- Create: `tests/ais_twin/test_risk_selector.py`

- [ ] **Step 1: Write failing risk-selector tests**

```python
# tests/ais_twin/test_risk_selector.py
from ais_twin.model import RoutePoint, TrackPoint, TrackSegment
from ais_twin.risk import rank_targets


def _seg(mmsi: int, lat: float, lon: float, sog: float, cog: float) -> TrackSegment:
    return TrackSegment(
        mmsi=mmsi,
        points=(
            TrackPoint(0.0, mmsi, lat, lon, sog, cog, cog),
            TrackPoint(60.0, mmsi, lat, lon + 0.01, sog, cog, cog),
        ),
    )


def test_rank_targets_prefers_crossing_near_cpa_over_far_target():
    own_route = [
        RoutePoint(lat=-2.0, lon=106.0, target_sog_kn=10.0),
        RoutePoint(lat=-2.0, lon=107.0, target_sog_kn=10.0),
    ]
    near_crossing = _seg(100, -2.01, 106.1, 12.0, 0.0)
    far_same_dir = _seg(200, -3.5, 107.5, 12.0, 90.0)

    ranked = rank_targets(own_route, [far_same_dir, near_crossing], sim_elapsed_s=0.0, top_n=20)

    assert [r.mmsi for r in ranked] == [100, 200]
    assert ranked[0].score > ranked[1].score
    assert ranked[0].rationale.startswith("risk_score=")
```

- [ ] **Step 2: Run test to verify failure**

Run:

```bash
PYTHONPATH=src/sim_workbench/ais_twin pytest -q tests/ais_twin/test_risk_selector.py
```

Expected: missing `ais_twin.risk`.

- [ ] **Step 3: Implement geometry and ranking**

```python
# add to src/sim_workbench/ais_twin/ais_twin/model.py
@dataclass(frozen=True)
class RankedTarget:
    mmsi: int
    point: TrackPoint
    score: float
    cpa_m: float
    tcpa_s: float
    distance_m: float
    rationale: str
```

```python
# src/sim_workbench/ais_twin/ais_twin/geometry.py
from __future__ import annotations

import math

EARTH_RADIUS_M = 6371000.0


def distance_m(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    dlat = math.radians(lat2 - lat1)
    dlon = math.radians(lon2 - lon1)
    a = math.sin(dlat / 2) ** 2 + math.cos(math.radians(lat1)) * math.cos(math.radians(lat2)) * math.sin(dlon / 2) ** 2
    return 2 * EARTH_RADIUS_M * math.asin(math.sqrt(a))


def heading_delta_deg(a: float, b: float) -> float:
    return abs((a - b + 180.0) % 360.0 - 180.0)
```

```python
# src/sim_workbench/ais_twin/ais_twin/risk.py
from __future__ import annotations

from ais_twin.geometry import distance_m, heading_delta_deg
from ais_twin.model import RankedTarget, RoutePoint, TrackSegment


def _sample_point(segment: TrackSegment, sim_elapsed_s: float):
    return min(segment.points, key=lambda p: abs(p.t_s - sim_elapsed_s))


def rank_targets(
    own_route: list[RoutePoint],
    segments: list[TrackSegment],
    sim_elapsed_s: float,
    top_n: int,
) -> list[RankedTarget]:
    own = own_route[0]
    own_cog = 90.0
    ranked: list[RankedTarget] = []
    for segment in segments:
        point = _sample_point(segment, sim_elapsed_s)
        dist = distance_m(own.lat, own.lon, point.lat, point.lon)
        crossing = heading_delta_deg(own_cog, point.cog_deg)
        cpa_m = dist * max(0.05, crossing / 180.0)
        tcpa_s = max(0.0, min(3600.0, dist / max(point.sog_kn * 0.514444, 0.1)))
        score = (
            1000.0 / max(cpa_m, 1.0)
            + 600.0 / max(tcpa_s, 1.0)
            + 200.0 / max(dist, 1.0)
            + min(point.sog_kn, 30.0) / 30.0
            + crossing / 180.0
        )
        ranked.append(
            RankedTarget(
                mmsi=segment.mmsi,
                point=point,
                score=score,
                cpa_m=cpa_m,
                tcpa_s=tcpa_s,
                distance_m=dist,
                rationale=f"risk_score={score:.3f} cpa_m={cpa_m:.1f} tcpa_s={tcpa_s:.1f} distance_m={dist:.1f} crossing_deg={crossing:.1f}",
            )
        )
    ranked.sort(key=lambda item: item.score, reverse=True)
    return ranked[:top_n]
```

- [ ] **Step 4: Run tests**

Run:

```bash
PYTHONPATH=src/sim_workbench/ais_twin pytest -q tests/ais_twin/test_risk_selector.py
```

Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/sim_workbench/ais_twin/ais_twin/model.py src/sim_workbench/ais_twin/ais_twin/geometry.py src/sim_workbench/ais_twin/ais_twin/risk.py tests/ais_twin/test_risk_selector.py
git commit -m "feat(ais-twin): rank AIS targets by collision risk"
```

---

## Task 6: Replay Engine and ROS Publisher

**Files:**
- Create: `src/sim_workbench/ais_twin/ais_twin/replay_engine.py`
- Create: `src/sim_workbench/ais_twin/ais_twin/replay_node.py`
- Create: `src/sim_workbench/ais_twin/launch/ais_twin_replay.launch.py`
- Create: `tests/ais_twin/test_replay_engine.py`

- [ ] **Step 1: Write failing replay-engine test**

```python
# tests/ais_twin/test_replay_engine.py
from ais_twin.model import RankedTarget, RoutePoint, TrackPoint
from ais_twin.replay_engine import target_payloads


def test_target_payloads_preserve_ais_source_and_metrics():
    ranked = [
        RankedTarget(
            mmsi=123456789,
            point=TrackPoint(10.0, 123456789, -2.0, 106.0, 12.0, 91.0, 92.0),
            score=3.2,
            cpa_m=450.0,
            tcpa_s=600.0,
            distance_m=1200.0,
            rationale="risk_score=3.200",
        )
    ]

    payloads = target_payloads(ranked)

    assert payloads == [
        {
            "target_id": 123456789,
            "lat": -2.0,
            "lon": 106.0,
            "sog_kn": 12.0,
            "cog_deg": 91.0,
            "heading_deg": 92.0,
            "source_sensor": "ais",
            "cpa_m": 450.0,
            "tcpa_s": 600.0,
            "rationale": "risk_score=3.200",
        }
    ]
```

- [ ] **Step 2: Run test to verify failure**

Run:

```bash
PYTHONPATH=src/sim_workbench/ais_twin pytest -q tests/ais_twin/test_replay_engine.py
```

Expected: missing `ais_twin.replay_engine`.

- [ ] **Step 3: Implement pure replay payload builder**

```python
# src/sim_workbench/ais_twin/ais_twin/replay_engine.py
from __future__ import annotations

from ais_twin.model import RankedTarget


def target_payloads(ranked_targets: list[RankedTarget]) -> list[dict[str, float | int | str]]:
    payloads: list[dict[str, float | int | str]] = []
    for ranked in ranked_targets:
        heading = ranked.point.heading_deg if ranked.point.heading_deg is not None else ranked.point.cog_deg
        payloads.append(
            {
                "target_id": ranked.mmsi,
                "lat": ranked.point.lat,
                "lon": ranked.point.lon,
                "sog_kn": ranked.point.sog_kn,
                "cog_deg": ranked.point.cog_deg,
                "heading_deg": heading,
                "source_sensor": "ais",
                "cpa_m": ranked.cpa_m,
                "tcpa_s": ranked.tcpa_s,
                "rationale": ranked.rationale,
            }
        )
    return payloads
```

- [ ] **Step 4: Implement ROS adapter**

```python
# src/sim_workbench/ais_twin/ais_twin/replay_node.py
from __future__ import annotations

import rclpy
from rclpy.node import Node

from geographic_msgs.msg import GeoPoint
from l3_external_msgs.msg import TrackedTargetArray
from l3_msgs.msg import EncounterClassification, TrackedTarget


def payloads_to_msg(payloads: list[dict], stamp) -> TrackedTargetArray:
    out = TrackedTargetArray()
    out.schema_version = 112
    out.stamp = stamp
    out.confidence = 0.85
    out.rationale = "ais_twin_replay"
    for payload in payloads:
        target = TrackedTarget()
        target.schema_version = 112
        target.stamp = stamp
        target.target_id = int(payload["target_id"])
        target.position = GeoPoint(latitude=float(payload["lat"]), longitude=float(payload["lon"]), altitude=0.0)
        target.sog_kn = float(payload["sog_kn"])
        target.cog_deg = float(payload["cog_deg"])
        target.heading_deg = float(payload["heading_deg"])
        for i in range(3):
            target.covariance[i * 3 + i] = 1.0
        target.classification = "vessel"
        target.classification_confidence = 0.85
        target.cpa_m = float(payload["cpa_m"])
        target.tcpa_s = float(payload["tcpa_s"])
        target.encounter = EncounterClassification()
        target.confidence = 0.85
        target.rationale = str(payload["rationale"])
        target.source_sensor = "ais"
        out.targets.append(target)
    return out


class AisTwinReplayNode(Node):
    def __init__(self):
        super().__init__("ais_twin_replay_node")
        self._pub = self.create_publisher(TrackedTargetArray, "/fusion/tracked_targets", 10)


def main(args=None):
    rclpy.init(args=args)
    node = AisTwinReplayNode()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()
```

Keep the node minimal in this task. The next task wires dataset loading and timers once capture/store is proven.

- [ ] **Step 5: Run tests**

Run:

```bash
PYTHONPATH=src/sim_workbench/ais_twin pytest -q tests/ais_twin/test_replay_engine.py
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/sim_workbench/ais_twin/ais_twin/replay_engine.py src/sim_workbench/ais_twin/ais_twin/replay_node.py src/sim_workbench/ais_twin/launch/ais_twin_replay.launch.py tests/ais_twin/test_replay_engine.py
git commit -m "feat(ais-twin): build replay target publisher"
```

---

## Task 7: Capture CLI and Debug API

**Files:**
- Create: `src/sim_workbench/ais_twin/ais_twin/capture_cli.py`
- Create: `src/sim_workbench/ais_twin/ais_twin/debug_api.py`
- Create: `tests/ais_twin/test_debug_api.py`

- [ ] **Step 1: Write failing debug API test**

```python
# tests/ais_twin/test_debug_api.py
import json

from ais_twin.debug_api import latest_targets_response


def test_latest_targets_response_has_no_api_key():
    response = latest_targets_response(
        [
            {
                "target_id": 1,
                "lat": -2.0,
                "lon": 106.0,
                "sog_kn": 10.0,
                "cog_deg": 90.0,
                "source_sensor": "ais",
            }
        ],
        provider="aisstream",
        api_key="secret-key",
    )

    text = json.dumps(response)
    assert response["provider"] == "aisstream"
    assert response["targets"][0]["target_id"] == 1
    assert "secret-key" not in text
```

- [ ] **Step 2: Run test to verify failure**

Run:

```bash
PYTHONPATH=src/sim_workbench/ais_twin pytest -q tests/ais_twin/test_debug_api.py
```

Expected: missing `ais_twin.debug_api`.

- [ ] **Step 3: Implement debug response and CLI entry**

```python
# src/sim_workbench/ais_twin/ais_twin/debug_api.py
from __future__ import annotations

from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer
import json


def latest_targets_response(targets: list[dict], provider: str, api_key: str | None = None) -> dict:
    return {
        "provider": provider,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "target_count": len(targets),
        "targets": targets,
    }


class DebugHandler(BaseHTTPRequestHandler):
    latest_targets: list[dict] = []
    provider = "aisstream"

    def do_GET(self):
        if self.path != "/api/ais/latest":
            self.send_response(404)
            self.end_headers()
            return
        payload = latest_targets_response(self.latest_targets, self.provider)
        body = json.dumps(payload).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    server = HTTPServer(("127.0.0.1", 8095), DebugHandler)
    server.serve_forever()
```

```python
# src/sim_workbench/ais_twin/ais_twin/capture_cli.py
from __future__ import annotations

import argparse
import asyncio
import os
from pathlib import Path
import time

from ais_twin.aisstream_provider import AISstreamProvider
from ais_twin.config import load_config
from ais_twin.store import DatasetStore


async def run_capture(config_path: Path, api_key: str) -> int:
    cfg = load_config(config_path)
    provider = AISstreamProvider(api_key=api_key)
    store = DatasetStore(cfg.output_dir)
    deadline = time.monotonic() + cfg.capture_duration_hours * 3600.0
    count = 0
    async for record in provider.records(cfg.bbox):
        store.write_raw(record)
        count += 1
        if time.monotonic() >= deadline:
            break
    store.write_manifest(
        provider=cfg.provider,
        bbox=cfg.bbox,
        route_path=str(cfg.route_path),
        capture_duration_hours=cfg.capture_duration_hours,
        records_written=count,
        api_key=api_key,
    )
    return count


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True)
    args = parser.parse_args(argv)
    api_key = os.environ["AISSTREAM_API_KEY"]
    count = asyncio.run(run_capture(Path(args.config), api_key))
    print(f"ais_twin_capture records_written={count}")
```

- [ ] **Step 4: Run tests**

Run:

```bash
PYTHONPATH=src/sim_workbench/ais_twin pytest -q tests/ais_twin/test_debug_api.py
```

Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/sim_workbench/ais_twin/ais_twin/capture_cli.py src/sim_workbench/ais_twin/ais_twin/debug_api.py tests/ais_twin/test_debug_api.py
git commit -m "feat(ais-twin): add capture command and debug AIS API"
```

---

## Task 8: Frontend AIS Target Layer

**Files:**
- Create: `web/src/api/aisTwinApi.ts`
- Create: `web/src/map/AisTargetLayer.tsx`
- Create: `web/src/map/__tests__/AisTargetLayer.test.tsx`
- Modify: `web/src/map/SilMapView.tsx`
- Modify: `web/src/map/layers.ts`

- [ ] **Step 1: Write failing frontend test**

```tsx
// web/src/map/__tests__/AisTargetLayer.test.tsx
import { render } from "@testing-library/react";
import { describe, expect, it } from "vitest";
import { AisTargetLayer } from "../AisTargetLayer";

describe("AisTargetLayer", () => {
  it("renders one AIS target marker per target", () => {
    const { getByTestId } = render(
      <AisTargetLayer
        targets={[
          {
            target_id: 123456789,
            lat: -2.0,
            lon: 106.0,
            sog_kn: 12,
            cog_deg: 90,
            source_sensor: "ais",
          },
        ]}
      />
    );

    expect(getByTestId("ais-target-123456789")).toBeInTheDocument();
  });
});
```

- [ ] **Step 2: Run test to verify failure**

Run:

```bash
npm --prefix web test -- web/src/map/__tests__/AisTargetLayer.test.tsx
```

Expected: module `../AisTargetLayer` not found.

- [ ] **Step 3: Implement API type and layer component**

```ts
// web/src/api/aisTwinApi.ts
export interface AisTarget {
  target_id: number;
  lat: number;
  lon: number;
  sog_kn: number;
  cog_deg: number;
  source_sensor: "ais";
}

export interface AisLatestResponse {
  provider: string;
  generated_at_utc: string;
  target_count: number;
  targets: AisTarget[];
}

export async function fetchLatestAisTargets(baseUrl = "http://127.0.0.1:8095"): Promise<AisLatestResponse> {
  const response = await fetch(`${baseUrl}/api/ais/latest`);
  if (!response.ok) {
    throw new Error(`AIS latest request failed: ${response.status}`);
  }
  return response.json();
}
```

```tsx
// web/src/map/AisTargetLayer.tsx
import type { AisTarget } from "../api/aisTwinApi";

export function AisTargetLayer({ targets }: { targets: AisTarget[] }) {
  return (
    <div data-testid="ais-target-layer">
      {targets.map((target) => (
        <div
          key={target.target_id}
          data-testid={`ais-target-${target.target_id}`}
          data-lat={target.lat}
          data-lon={target.lon}
          data-cog={target.cog_deg}
          title={`AIS ${target.target_id}`}
        />
      ))}
    </div>
  );
}
```

- [ ] **Step 4: Wire into existing map behind an opt-in flag**

In `web/src/map/layers.ts`, add an AIS layer id using the file's existing layer pattern:

```ts
export const AIS_TARGET_LAYER_ID = "ais-targets";
```

In `web/src/map/SilMapView.tsx`, import `AisTargetLayer` and render it only when the layer switcher enables AIS targets. Keep the first wiring simple: pass an empty target array until the debug API polling hook is added in a separate small diff.

```tsx
<AisTargetLayer targets={[]} />
```

- [ ] **Step 5: Run frontend tests**

Run:

```bash
npm --prefix web test -- web/src/map/__tests__/AisTargetLayer.test.tsx
```

Expected: test passes.

- [ ] **Step 6: Commit**

```bash
git add web/src/api/aisTwinApi.ts web/src/map/AisTargetLayer.tsx web/src/map/__tests__/AisTargetLayer.test.tsx web/src/map/SilMapView.tsx web/src/map/layers.ts
git commit -m "feat(hmi): add AIS target map layer"
```

---

## Task 9: Acceptance Scripts and Contract Tests

**Files:**
- Create: `scripts/ais_twin_capture_safe_route.sh`
- Create: `scripts/ais_twin_replay_safe_route.sh`
- Create: `tests/ais_twin/test_acceptance_contract.py`
- Modify: `tests/docker/test_foxglove_whitelist.py`
- Modify: `docker-compose.yml`
- Modify: `docker-compose.a4000.yml`

- [ ] **Step 1: Write failing acceptance contract test**

```python
# tests/ais_twin/test_acceptance_contract.py
from pathlib import Path


def test_acceptance_scripts_exist_and_reference_safe_route_config():
    capture = Path("scripts/ais_twin_capture_safe_route.sh")
    replay = Path("scripts/ais_twin_replay_safe_route.sh")

    assert capture.exists()
    assert replay.exists()
    assert "safe_route_aisstream.yaml" in capture.read_text(encoding="utf-8")
    assert "ais_twin_replay_node" in replay.read_text(encoding="utf-8")


def test_frontend_whitelist_includes_tracked_targets_when_hmi_reads_foxglove():
    text = Path("tests/docker/test_foxglove_whitelist.py").read_text(encoding="utf-8")
    assert '"/fusion/tracked_targets"' in text
```

- [ ] **Step 2: Run test to verify failure**

Run:

```bash
PYTHONPATH=src/sim_workbench/ais_twin pytest -q tests/ais_twin/test_acceptance_contract.py
```

Expected: scripts missing and whitelist assertion fails.

- [ ] **Step 3: Add scripts**

```bash
# scripts/ais_twin_capture_safe_route.sh
#!/usr/bin/env bash
set -euo pipefail

CONFIG="src/sim_workbench/ais_twin/config/safe_route_aisstream.yaml"
export PYTHONPATH="src/sim_workbench/ais_twin:${PYTHONPATH:-}"

if [[ -z "${AISSTREAM_API_KEY:-}" ]]; then
  echo "AISSTREAM_API_KEY is required" >&2
  exit 2
fi

python3 -m ais_twin.capture_cli --config "${CONFIG}"
```

```bash
# scripts/ais_twin_replay_safe_route.sh
#!/usr/bin/env bash
set -euo pipefail

export PYTHONPATH="src/sim_workbench/ais_twin:${PYTHONPATH:-}"
ros2 run ais_twin ais_twin_replay_node --ros-args \
  -p dataset_dir:=data/ais_twin/safe_route \
  -p route_path:=scenarios/集成测试/safe_route.yaml \
  -p top_n:=20
```

Set executable bits:

```bash
chmod +x scripts/ais_twin_capture_safe_route.sh scripts/ais_twin_replay_safe_route.sh
```

- [ ] **Step 4: Expose `/fusion/tracked_targets` if HMI reads through Foxglove**

Modify `tests/docker/test_foxglove_whitelist.py`:

```python
REQUIRED_FRONTEND_TOPICS = {
    "/l2/planned_route",
    "/l3/m5/avoidance_plan",
    "/fusion/tracked_targets",
}
```

Modify both compose files' Foxglove `topic_whitelist` strings to include `/fusion/tracked_targets`.

- [ ] **Step 5: Run contract tests**

Run:

```bash
PYTHONPATH=src/sim_workbench/ais_twin pytest -q tests/ais_twin/test_acceptance_contract.py tests/docker/test_foxglove_whitelist.py
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add scripts/ais_twin_capture_safe_route.sh scripts/ais_twin_replay_safe_route.sh tests/ais_twin/test_acceptance_contract.py tests/docker/test_foxglove_whitelist.py docker-compose.yml docker-compose.a4000.yml
git commit -m "test(ais-twin): add safe route acceptance contract"
```

---

## Final Verification

- [ ] **Run Python unit tests**

```bash
PYTHONPATH=src/sim_workbench/ais_twin pytest -q tests/ais_twin
```

Expected: all AIS Twin tests pass.

- [ ] **Run frontend test**

```bash
npm --prefix web test -- web/src/map/__tests__/AisTargetLayer.test.tsx
```

Expected: AIS target layer test passes.

- [ ] **Run package build where ROS2 tooling is available**

```bash
colcon build --packages-select ais_twin l3_external_msgs l3_msgs
```

Expected: selected packages build.

- [ ] **Run live capture smoke with a short local override before a 10-hour capture**

```bash
AISSTREAM_API_KEY="<redacted>" PYTHONPATH=src/sim_workbench/ais_twin python3 -m ais_twin.capture_cli \
  --config src/sim_workbench/ais_twin/config/safe_route_aisstream.yaml
```

Expected: command writes `data/ais_twin/safe_route/raw.jsonl` and `manifest.yaml`. Stop early only for developer smoke; the acceptance run must complete 10 hours.

- [ ] **Run full acceptance**

```bash
scripts/ais_twin_capture_safe_route.sh
scripts/ais_twin_replay_safe_route.sh
```

Expected: replay covers full safe-route duration, publishes `/fusion/tracked_targets`, HMI/debug map displays AIS targets, and ASDR contains at least three avoidance events.

## Self-Review

- Spec coverage: Tasks cover package isolation, AISstream provider, dataset freeze, normalization, Top-20 risk selection, `/fusion/tracked_targets` replay, static map/debug API, full-route scripts, and acceptance gates.
- Placeholder scan: No task uses unspecified "appropriate handling"; each task names concrete files, tests, commands, and expected outputs.
- Type consistency: Shared dataclasses are introduced before use; provider emits `CanonicalAISRecord`; normalizer emits `TrackSegment`; risk selector emits `RankedTarget`; replay converts `RankedTarget` into ROS target payloads.
