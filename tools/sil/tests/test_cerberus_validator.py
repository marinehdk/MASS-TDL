"""Tests for the cerberus scenario YAML validator."""

from pathlib import Path
import sys
import copy

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from cerberus_validator import validate_yaml


_VALID_DOC = {
    "title": "Test Scenario",
    "startTime": "2026-01-01T00:00:00Z",
    "ownShip": {
        "static": {"id": 1, "shipType": "Cargo", "name": "Test Ship", "mmsi": 123456789},
        "initial": {
            "position": {"latitude": 63.44, "longitude": 10.38},
            "cog": 0.0,
            "sog": 10.0,
            "heading": 0.0,
            "navStatus": "Under way using engine",
        },
        "model": "fcb_mmg_vessel",
        "controller": "psbmpc_wrapper",
    },
    "targetShips": [
        {
            "id": "ts1",
            "static": {"id": 2, "mmsi": 100000001},
            "initial": {
                "position": {"latitude": 63.55, "longitude": 10.38},
                "cog": 180.0,
                "sog": 10.0,
                "heading": 180.0,
            },
            "model": "ais_replay_vessel",
        }
    ],
    "environment": {
        "wind": {"dir_deg": 0.0, "speed_mps": 0.0},
        "current": {"dir_deg": 0.0, "speed_mps": 0.0},
        "visibility_nm": 5.4,
    },
    "metadata": {
        "schema_version": "3.0",
        "scenario_id": "test-v1.0",
        "vessel_class": "FCB",
    },
}


def _without_key(d, key):
    result = {k: v for k, v in d.items() if k != key}
    return result


class TestCerberusValidator:
    def test_valid_document_passes(self):
        validate_yaml(_VALID_DOC)

    def test_missing_own_ship_raises(self):
        doc = _without_key(_VALID_DOC, "ownShip")
        with pytest.raises(ValueError, match="ownShip"):
            validate_yaml(doc)

    def test_missing_metadata_raises(self):
        doc = _without_key(_VALID_DOC, "metadata")
        with pytest.raises(ValueError, match="metadata"):
            validate_yaml(doc)

    def test_latitude_out_of_range_raises(self):
        doc = copy.deepcopy(_VALID_DOC)
        doc["ownShip"]["initial"]["position"]["latitude"] = 100.0
        with pytest.raises(ValueError):
            validate_yaml(doc)

    def test_missing_title_raises(self):
        doc = _without_key(_VALID_DOC, "title")
        with pytest.raises(ValueError, match="title"):
            validate_yaml(doc)

    def test_extra_fields_allowed(self):
        doc = dict(_VALID_DOC)
        doc["extra_field"] = "hello"
        doc["another_extra"] = 42
        validate_yaml(doc)
