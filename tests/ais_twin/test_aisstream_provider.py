from datetime import timezone

from ais_twin.aisstream_provider import (
    build_subscription_message,
    parse_aisstream_json,
    parse_aisstream_message,
)
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


def test_parse_non_position_report_returns_none():
    assert parse_aisstream_message({"MessageType": "ShipStaticData"}) is None


def test_parse_invalid_position_report_returns_none():
    raw = {"MessageType": "PositionReport", "Message": {"PositionReport": {"Valid": False}}}
    assert parse_aisstream_message(raw) is None


def test_parse_out_of_range_position_returns_none():
    raw = {
        "MessageType": "PositionReport",
        "Message": {
            "PositionReport": {
                "UserID": 259000420,
                "Latitude": 91.0,
                "Longitude": 181.0,
                "Sog": 12.0,
                "Cog": 90.0,
                "TrueHeading": 90,
                "Valid": True,
            }
        },
    }
    assert parse_aisstream_message(raw) is None


def test_parse_malformed_numeric_fields_returns_record_with_quality_flags():
    raw = {
        "MessageType": "PositionReport",
        "Message": {
            "PositionReport": {
                "UserID": 259000420,
                "Latitude": -2.25,
                "Longitude": 106.5,
                "Sog": "fast",
                "Cog": "east",
                "TrueHeading": -1,
                "Valid": True,
            }
        },
        "MetaData": {"time_utc": "not-a-time"},
    }
    record = parse_aisstream_message(raw)
    assert record is not None
    assert record.sog_kn is None
    assert record.cog_deg is None
    assert record.heading_deg is None
    assert {"missing_sog", "missing_cog", "missing_heading", "missing_ais_time"}.issubset(
        record.quality_flags
    )


def test_parse_bad_json_returns_none():
    assert parse_aisstream_json("not-json") is None
