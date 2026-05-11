"""Tests for Stage 5: maritime-schema YAML export."""
import tempfile
from pathlib import Path

import numpy as np
import yaml
from scenario_authoring.pipeline.stage4_filter import Encounter
from scenario_authoring.pipeline.stage5_export import export_encounter_to_yaml


def _make_dummy_encounter() -> Encounter:
    n = 100
    t = np.linspace(1716150000.0, 1716150300.0, n)
    os_traj = np.column_stack([
        t,
        np.linspace(63.0, 63.005, n),   # lat
        np.linspace(5.0, 5.005, n),     # lon
        np.full(n, 12.0),               # sog
        np.full(n, 90.0),               # cog
    ])
    ts_traj = np.column_stack([
        t,
        np.linspace(63.01, 63.015, n),
        np.linspace(5.01, 5.015, n),
        np.full(n, 10.0),
        np.full(n, 270.0),
    ])
    return Encounter(
        own_mmsi=123456789, target_mmsi=987654321,
        encounter_type="HEAD_ON",
        min_dcpa_m=200.0, min_tcpa_s=150.0,
        t_encounter_start=t[0], t_encounter_end=t[-1],
        own_trajectory=os_traj, target_trajectory=ts_traj,
    )


def test_export_creates_valid_yaml():
    """Exported YAML has correct structure and metadata fields."""
    with tempfile.TemporaryDirectory() as tmpdir:
        output_dir = Path(tmpdir)
        yaml_path = export_encounter_to_yaml(
            _make_dummy_encounter(),
            geo_origin=(63.0, 5.0),
            output_dir=output_dir,
        )

        assert yaml_path.exists()
        data = yaml.safe_load(yaml_path.read_text())

        # Core maritime-schema fields
        assert "own_ship" in data
        assert "target_ships" in data
        assert len(data["target_ships"]) == 1

        # Metadata extensions
        meta = data["metadata"]
        assert meta["scenario_source"] == "ais_derived"
        assert meta["scenario_id"].startswith("ais-")
        assert "ais_source_hash" in meta
        assert meta["ais_mmsi"] == [123456789, 987654321]
        assert "ais_time_window" in meta


def test_exported_yaml_passes_cerberus_validation():
    """AIS-derived YAML has all required Cerberus schema fields."""
    yaml_path = export_encounter_to_yaml(
        _make_dummy_encounter(),
        geo_origin=(63.0, 5.0),
    )
    data = yaml.safe_load(yaml_path.read_text())

    # Check required metadata fields are present (Cerberus schema compatibility)
    meta = data["metadata"]
    required_meta_fields = [
        "schema_version", "scenario_id", "scenario_source",
        "ais_source_hash", "ais_mmsi", "ais_time_window",
        "vessel_class", "odd_zone", "geo_origin",
        "encounter", "disturbance_model", "pass_criteria",
        "simulation",
    ]
    for field in required_meta_fields:
        assert field in meta, f"Missing required metadata field: {field}"

    assert meta["schema_version"] == "2.0"
    assert len(meta["ais_mmsi"]) == 2
