"""Tests for encounter_extractor full pipeline orchestrator."""
import tempfile
from pathlib import Path

from scenario_authoring.authoring.encounter_extractor import extract_encounters


def test_full_pipeline_with_synthetic_data():
    """End-to-end: synthetic CSV -> encounter YAML files."""
    csv_content = "MMSI,BaseDateTime,LAT,LON,SOG,COG,Heading,VesselType\r\n"
    # Own ship: 25 records heading east at 10kn from 63.0N 5.0E
    for i in range(25):
        ts = f"2024-01-15T08:{i:02d}:00"
        lon = 5.0 + i * 0.0061
        csv_content += (
            f"111111111,{ts},63.0,{lon:.5f},10.0,90.0,90.0,70\r\n"
        )
    # Target ship: 25 records heading west at 10kn towards own ship
    for i in range(25):
        ts = f"2024-01-15T08:{i:02d}:30"
        lon = 5.15 - i * 0.0061
        csv_content += (
            f"222222222,{ts},63.0,{lon:.5f},10.0,270.0,270.0,80\r\n"
        )

    with tempfile.TemporaryDirectory() as tmpdir:
        csv_path = Path(tmpdir) / "test.csv"
        csv_path.write_text(csv_content)
        yaml_paths = extract_encounters(
            data_dir=Path(tmpdir),
            geo_origin=(63.0, 5.0),
            own_mmsi=111111111,
        )
        # Two approaching vessels on reciprocal courses -> at least 1 encounter
        assert len(yaml_paths) >= 1, f"Expected >=1 encounter, got {len(yaml_paths)}"


def test_empty_data_dir_returns_empty():
    """Empty directory produces no encounters."""
    with tempfile.TemporaryDirectory() as tmpdir:
        paths = extract_encounters(
            data_dir=Path(tmpdir),
            geo_origin=(63.0, 5.0),
            own_mmsi=111111111,
        )
        assert paths == []


def test_single_vessel_returns_empty():
    """Single vessel (<2 groups) produces no encounters."""
    csv_content = "MMSI,BaseDateTime,LAT,LON,SOG,COG,Heading,VesselType\r\n"
    for i in range(10):
        ts = f"2024-01-15T08:{i:02d}:00"
        csv_content += (
            f"111111111,{ts},63.0,5.0,10.0,90.0,90.0,70\r\n"
        )

    with tempfile.TemporaryDirectory() as tmpdir:
        csv_path = Path(tmpdir) / "test.csv"
        csv_path.write_text(csv_content)
        paths = extract_encounters(
            data_dir=Path(tmpdir),
            geo_origin=(63.0, 5.0),
            own_mmsi=111111111,
        )
        assert paths == []
