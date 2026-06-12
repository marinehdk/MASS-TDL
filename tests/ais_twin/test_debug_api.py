import json
import asyncio
import os
import subprocess
import sys
from datetime import datetime, timezone

import pytest

from ais_twin import capture_cli, debug_api
from ais_twin.capture_cli import _record_for_normalization, _strip_raw_json, main
from ais_twin.debug_api import current_targets, latest_targets_response, load_latest_targets_from_tracks
from ais_twin.model import CanonicalAISRecord


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


def test_latest_targets_response_has_count_and_generated_time():
    response = latest_targets_response([], provider="aisstream", api_key=None)
    assert response["target_count"] == 0
    assert response["generated_at_utc"].endswith("+00:00")


def test_capture_cli_requires_api_key(monkeypatch):
    monkeypatch.delenv("AISSTREAM_API_KEY", raising=False)
    with pytest.raises(KeyError):
        main(["--config", "src/sim_workbench/ais_twin/config/safe_route_aisstream.yaml"])


def test_capture_cli_module_entrypoint_requires_api_key():
    env = os.environ.copy()
    env.pop("AISSTREAM_API_KEY", None)

    result = subprocess.run(
        [
            sys.executable,
            "-m",
            "ais_twin.capture_cli",
            "--config",
            "src/sim_workbench/ais_twin/config/safe_route_aisstream.yaml",
        ],
        env=env,
        check=False,
        capture_output=True,
        text=True,
    )

    assert result.returncode != 0
    assert "AISSTREAM_API_KEY" in result.stderr


def test_debug_api_module_entrypoint_exposes_help():
    result = subprocess.run(
        [sys.executable, "-m", "ais_twin.debug_api", "--help"],
        check=False,
        capture_output=True,
        text=True,
    )

    assert result.returncode == 0
    assert "--tracks" in result.stdout
    assert "--port" in result.stdout
    assert "--provider" in result.stdout
    assert "--bbox" in result.stdout
    assert "--api-key-env" in result.stdout


def test_debug_api_parse_args_does_not_default_to_demo_tracks():
    args = debug_api.parse_args([])

    assert args.tracks is None
    assert args.provider == "aisstream"


def test_parse_bbox_arg_uses_lat_lon_bounds():
    bbox = debug_api.parse_bbox_arg("-4.6,104.7,-1.1,108.7")

    assert bbox.lat_min == -4.6
    assert bbox.lon_min == 104.7
    assert bbox.lat_max == -1.1
    assert bbox.lon_max == 108.7


def test_live_ais_target_store_keeps_latest_records_without_raw_json():
    store = debug_api.LiveAisTargetStore(limit=2)
    older = CanonicalAISRecord(
        provider="aisstream",
        received_at_utc=datetime(2026, 6, 12, 0, 0, 0, tzinfo=timezone.utc),
        ais_time_utc=datetime(2026, 6, 12, 0, 0, 0, tzinfo=timezone.utc),
        mmsi=525200401,
        lat=-2.37,
        lon=104.74,
        sog_kn=None,
        cog_deg=360.0,
        heading_deg=None,
        nav_status=None,
        ship_name=None,
        ship_type=None,
        raw_message_type="PositionReport",
        raw_json={"secret": "raw-payload"},
        quality_flags=frozenset({"missing_sog"}),
    )
    newer = CanonicalAISRecord(
        provider="aisstream",
        received_at_utc=datetime(2026, 6, 12, 0, 0, 5, tzinfo=timezone.utc),
        ais_time_utc=datetime(2026, 6, 12, 0, 0, 5, tzinfo=timezone.utc),
        mmsi=525200401,
        lat=-2.38,
        lon=104.75,
        sog_kn=8.0,
        cog_deg=12.0,
        heading_deg=10.0,
        nav_status="under_way",
        ship_name="REAL AIS",
        ship_type=None,
        raw_message_type="PositionReport",
        raw_json={"secret": "raw-payload"},
        quality_flags=frozenset(),
    )

    store.update(older)
    store.update(newer)

    targets = store.targets()
    assert targets[0].items() >= {
        "target_id": 525200401,
        "lat": -2.38,
        "lon": 104.75,
        "sog_kn": 8.0,
        "cog_deg": 12.0,
        "heading_deg": 10.0,
        "source_sensor": "ais",
        "received_at_utc": "2026-06-12T00:00:05+00:00",
    }.items()
    assert "raw-payload" not in json.dumps(targets)


def test_live_ais_target_store_merges_ship_static_data():
    store = debug_api.LiveAisTargetStore(limit=2)
    static = {
        "MessageType": "ShipStaticData",
        "Message": {
            "ShipStaticData": {
                "UserID": 525200401,
                "Name": "CHITOSE",
                "Type": 70,
                "Destination": "PELINTUNG",
                "Dimension": {"A": 120, "B": 60, "C": 15, "D": 15},
            }
        },
        "MetaData": {"MMSI": 525200401},
    }
    position = CanonicalAISRecord(
        provider="aisstream",
        received_at_utc=datetime(2026, 6, 12, 0, 0, 5, tzinfo=timezone.utc),
        ais_time_utc=datetime(2026, 6, 12, 0, 0, 5, tzinfo=timezone.utc),
        mmsi=525200401,
        lat=-2.38,
        lon=104.75,
        sog_kn=6.0,
        cog_deg=89.0,
        heading_deg=88.0,
        nav_status="under_way",
        ship_name=None,
        ship_type=None,
        raw_message_type="PositionReport",
        raw_json={},
        quality_flags=frozenset(),
    )

    store.update_static(static)
    store.update(position)

    target = store.targets()[0]
    assert target["ship_name"] == "CHITOSE"
    assert target["ship_type"] == "cargo"
    assert target["destination"] == "PELINTUNG"
    assert target["vessel_length_m"] == 180


def test_load_latest_targets_from_tracks_keeps_latest_per_mmsi(tmp_path):
    tracks_path = tmp_path / "tracks.csv"
    tracks_path.write_text(
        "\n".join(
            [
                "mmsi,t_s,lat,lon,sog_kn,cog_deg,heading_deg",
                "200,1.0,-2.0,106.0,9.0,80.0,81.0",
                "100,5.0,-3.0,107.0,10.0,90.0,",
                "200,7.0,-2.2,106.2,11.0,82.0,83.0",
            ]
        )
        + "\n",
        encoding="utf-8",
    )

    targets = load_latest_targets_from_tracks(tracks_path)

    assert [target["target_id"] for target in targets] == [100, 200]
    assert targets[0] == {
        "target_id": 100,
        "lat": -3.0,
        "lon": 107.0,
        "sog_kn": 10.0,
        "cog_deg": 90.0,
        "heading_deg": None,
        "source_sensor": "ais",
    }
    assert targets[1]["lat"] == -2.2
    assert targets[1]["heading_deg"] == 83.0


def test_current_targets_reloads_tracks_after_file_is_created(tmp_path):
    tracks_path = tmp_path / "tracks.csv"

    assert current_targets(tracks_path, limit=200, fallback_targets=[]) == []

    tracks_path.write_text(
        "\n".join(
            [
                "mmsi,t_s,lat,lon,sog_kn,cog_deg,heading_deg",
                "300,1.0,-4.0,108.0,12.0,100.0,101.0",
            ]
        )
        + "\n",
        encoding="utf-8",
    )

    assert current_targets(tracks_path, limit=200, fallback_targets=[]) == [
        {
            "target_id": 300,
            "lat": -4.0,
            "lon": 108.0,
            "sog_kn": 12.0,
            "cog_deg": 100.0,
            "heading_deg": 101.0,
            "source_sensor": "ais",
        }
    ]


def test_current_targets_uses_fallback_when_no_tracks_path():
    fallback = [{"target_id": 1, "lat": -2.0}]

    assert current_targets(None, limit=200, fallback_targets=fallback) == fallback


def test_capture_cli_passes_overwrite_flag(monkeypatch):
    calls = []

    async def fake_run_capture(config_path, api_key, overwrite=False):
        calls.append((str(config_path), api_key, overwrite))
        return 0

    monkeypatch.setenv("AISSTREAM_API_KEY", "secret-key")
    monkeypatch.setattr(capture_cli, "run_capture", fake_run_capture)

    main(["--config", "src/sim_workbench/ais_twin/config/safe_route_aisstream.yaml"])
    main(["--config", "src/sim_workbench/ais_twin/config/safe_route_aisstream.yaml", "--overwrite"])

    assert calls == [
        ("src/sim_workbench/ais_twin/config/safe_route_aisstream.yaml", "secret-key", False),
        ("src/sim_workbench/ais_twin/config/safe_route_aisstream.yaml", "secret-key", True),
    ]


def test_run_capture_exits_when_provider_is_quiet(tmp_path, monkeypatch):
    class QuietProvider:
        def __init__(self, api_key):
            self.api_key = api_key

        async def records(self, bbox):
            await asyncio.Event().wait()
            if False:
                yield

    output_dir = tmp_path / "dataset"
    config_path = tmp_path / "config.yaml"
    config_path.write_text(
        "\n".join(
            [
                "provider: aisstream",
                "route_path: scenarios/集成测试/safe_route.yaml",
                "bbox:",
                "  lat_min: -4.5",
                "  lat_max: -1.1",
                "  lon_min: 104.7",
                "  lon_max: 108.6",
                "capture_duration_hours: 0.0000003",
                "risk_top_n: 20",
                f"output_dir: {output_dir}",
            ]
        )
        + "\n",
        encoding="utf-8",
    )
    monkeypatch.setattr(capture_cli, "AISstreamProvider", QuietProvider)

    count = asyncio.run(capture_cli.run_capture(config_path, api_key="secret", overwrite=True))

    assert count == 0
    assert (output_dir / "tracks.csv").read_text(encoding="utf-8").startswith("mmsi,t_s,lat,lon")
    assert "records_written: 0" in (output_dir / "manifest.yaml").read_text(encoding="utf-8")


def test_strip_raw_json_keeps_original_record_raw_json():
    record = CanonicalAISRecord(
        provider="aisstream",
        received_at_utc=datetime(2026, 1, 1, tzinfo=timezone.utc),
        ais_time_utc=datetime(2026, 1, 1, tzinfo=timezone.utc),
        mmsi=123456789,
        lat=-2.0,
        lon=106.0,
        sog_kn=10.0,
        cog_deg=90.0,
        heading_deg=91.0,
        nav_status="under_way",
        ship_name="TEST",
        ship_type="cargo",
        raw_message_type="PositionReport",
        raw_json={"secret": "large-payload"},
        quality_flags=frozenset({"ok"}),
    )

    stripped = _record_for_normalization(record)

    assert stripped is not record
    assert stripped.raw_json == {}
    assert record.raw_json["secret"] == "large-payload"
    assert stripped.mmsi == record.mmsi
    assert stripped.nav_status is None
    assert stripped.ship_name is None
    assert stripped.ship_type is None
    assert stripped.raw_message_type == ""
    assert stripped.quality_flags == frozenset()


def test_strip_raw_json_returns_new_record_without_raw_json():
    record = CanonicalAISRecord(
        provider="aisstream",
        received_at_utc=datetime(2026, 1, 1, tzinfo=timezone.utc),
        ais_time_utc=datetime(2026, 1, 1, tzinfo=timezone.utc),
        mmsi=987654321,
        lat=-2.0,
        lon=106.0,
        sog_kn=10.0,
        cog_deg=90.0,
        heading_deg=None,
        nav_status=None,
        ship_name=None,
        ship_type=None,
        raw_message_type="PositionReport",
        raw_json={"payload": {"nested": True}},
        quality_flags=frozenset(),
    )

    stripped = _strip_raw_json(record)

    assert stripped is not record
    assert stripped.raw_json == {}
    assert record.raw_json["payload"]["nested"] is True
