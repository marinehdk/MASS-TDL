from datetime import datetime, timedelta, timezone
import csv
import json

import pytest
import yaml

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
        raw_json={"MessageType": "PositionReport", "nested": {"mmsi": mmsi}, "items": [1, 2]},
        quality_flags=frozenset(),
    )


def test_dataset_store_writes_raw_tracks_manifest_and_no_secret(tmp_path):
    store = DatasetStore(tmp_path)
    bbox = BBox(-4.5, -1.1, 104.7, 108.5)
    records = [_record(1, 0, -2.0, 106.0), _record(1, 10, -2.0, 106.01), _record(1, 20, -2.0, 106.02)]
    for record in records:
        store.write_raw(record)
    segments = normalize_records(records, min_points=2)
    tracks_path = store.write_tracks(segments)
    manifest = store.write_manifest(
        provider="aisstream",
        bbox=bbox,
        route_path="scenarios/集成测试/safe_route.yaml",
        capture_duration_hours=10.0,
        records_written=len(records),
        segments_written=len(segments),
        api_key="secret-key",
    )

    assert (tmp_path / "raw.jsonl").exists()
    raw_line = json.loads((tmp_path / "raw.jsonl").read_text(encoding="utf-8").splitlines()[0])
    assert raw_line["mmsi"] == 1
    assert raw_line["raw_json"]["nested"]["mmsi"] == 1

    rows = list(csv.DictReader(tracks_path.open(encoding="utf-8")))
    assert rows[0]["mmsi"] == "1"
    assert rows[0]["lat"] == "-2.0"

    text = manifest.read_text(encoding="utf-8")
    assert "secret-key" not in text
    assert "time_alignment: real_time_trim" in text
    assert "raw_sha256:" in text
    assert "tracks_sha256:" in text


def test_normalizer_dedupes_sorts_and_splits_gaps():
    records = [
        _record(1, 700, -2.0, 106.5),
        _record(1, 0, -2.0, 106.0),
        _record(1, 10, -2.0, 106.01),
        _record(1, 0, -2.0, 106.0),
        _record(1, 710, -2.0, 106.51),
    ]

    segments = normalize_records(records, max_gap_s=300.0, min_points=2)

    assert len(segments) == 2
    assert [p.t_s for p in segments[0].points] == [0.0, 10.0]
    assert [p.t_s for p in segments[1].points] == [700.0, 710.0]


def test_dataset_store_refuses_reused_output_dir_without_overwrite(tmp_path):
    store = DatasetStore(tmp_path)
    store.write_raw(_record(1, 0, -2.0, 106.0))

    with pytest.raises(FileExistsError):
        DatasetStore(tmp_path)

    clean = DatasetStore(tmp_path, overwrite=True)
    clean.write_raw(_record(2, 0, -2.0, 106.0))
    lines = (tmp_path / "raw.jsonl").read_text(encoding="utf-8").splitlines()
    assert len(lines) == 1
    assert json.loads(lines[0])["mmsi"] == 2


def test_normalizer_accepts_identical_duplicate_regardless_order():
    records = [_record(1, 0, -2.0, 106.0), _record(1, 0, -2.0, 106.0), _record(1, 10, -2.1, 106.1)]

    segments = normalize_records(list(reversed(records)), min_points=2)

    assert [p.t_s for p in segments[0].points] == [0.0, 10.0]
    assert [(p.lat, p.lon) for p in segments[0].points] == [(-2.0, 106.0), (-2.1, 106.1)]


def test_normalizer_rejects_conflicting_duplicate_timestamp():
    records = [_record(1, 0, -2.0, 106.0), _record(1, 0, -3.0, 107.0), _record(1, 10, -2.1, 106.1)]

    with pytest.raises(ValueError, match="conflicting duplicate AIS record"):
        normalize_records(records, min_points=2)


def test_normalizer_emits_segments_sorted_by_mmsi():
    records = [
        _record(9, 0, -2.0, 106.0),
        _record(9, 10, -2.0, 106.01),
        _record(3, 0, -3.0, 107.0),
        _record(3, 10, -3.0, 107.01),
    ]

    segments = normalize_records(records, min_points=2)

    assert [segment.mmsi for segment in segments] == [3, 9]


@pytest.mark.parametrize(
    "route_path",
    [
        "scenarios/foo # note.yaml",
        "scenarios/foo: bar.yaml",
        "yes",
    ],
)
def test_manifest_route_path_round_trips_as_string(tmp_path, route_path):
    store = DatasetStore(tmp_path)
    manifest = store.write_manifest(
        provider="aisstream",
        bbox=BBox(-4.5, -1.1, 104.7, 108.5),
        route_path=route_path,
        capture_duration_hours=10.0,
        records_written=0,
        segments_written=0,
        api_key=None,
    )

    loaded = yaml.safe_load(manifest.read_text(encoding="utf-8"))

    assert loaded["route_path"] == route_path
    assert isinstance(loaded["route_path"], str)
