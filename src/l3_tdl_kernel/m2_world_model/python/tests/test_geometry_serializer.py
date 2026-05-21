"""Tests for geometry serializer.

Adapted to the actual API: serialize() writes Boost.Geometry-compatible JSON to disk.
"""

from __future__ import annotations

import json
import tempfile
from pathlib import Path

from enc_loader.s57_parser import ChartBounds, EncPolygon
from enc_loader.geometry_serializer import serialize


class TestSerializePolygonRoundtrip:
    """EncPolygon → serialize → read-back JSON validates structure."""

    def test_polygon_to_geojson_roundtrip(self):
        poly = EncPolygon(
            name="test_depare",
            feature_class="DEPARE",
            coordinates=(
                (121.0, 30.0),
                (121.1, 30.0),
                (121.1, 30.1),
                (121.0, 30.1),
                (121.0, 30.0),
            ),
        )
        bounds = ChartBounds(lat_min=30.0, lat_max=31.0, lon_min=121.0, lon_max=122.0)

        with tempfile.NamedTemporaryFile(suffix=".json", mode="w", delete=False) as f:
            out_path = Path(f.name)

        try:
            serialize("test_chart", bounds, [poly], out_path)
            data = json.loads(out_path.read_text(encoding="utf-8"))

            assert data["name"] == "test_chart"
            assert data["bounds"]["lat_min"] == 30.0
            assert data["bounds"]["lat_max"] == 31.0
            # DEPARE goes into "hazards" array
            assert len(data["hazards"]) == 1
            assert len(data["hazards"][0]) == 5
            assert data["hazards"][0][0] == [121.0, 30.0]
        finally:
            out_path.unlink(missing_ok=True)


class TestEmptyPolygonList:
    """Edge case: empty polygon list."""

    def test_empty_polygon_list(self):
        bounds = ChartBounds(lat_min=30.0, lat_max=31.0, lon_min=121.0, lon_max=122.0)

        with tempfile.NamedTemporaryFile(suffix=".json", mode="w", delete=False) as f:
            out_path = Path(f.name)

        try:
            serialize("empty_chart", bounds, [], out_path)
            data = json.loads(out_path.read_text(encoding="utf-8"))

            assert data["name"] == "empty_chart"
            assert len(data["hazards"]) == 0
            assert len(data["tss_lanes"]) == 0
        finally:
            out_path.unlink(missing_ok=True)


class TestTssLanesArray:
    """TSSLPT polygons go into tss_lanes array."""

    def test_tss_lanes_array(self):
        poly_a = EncPolygon(
            name="lane_a", feature_class="TSSLPT",
            coordinates=((121.8, 30.3), (122.2, 30.3), (122.2, 30.7), (121.8, 30.7), (121.8, 30.3)),
        )
        poly_b = EncPolygon(
            name="lane_b", feature_class="TSSLPT",
            coordinates=((121.9, 30.4), (122.1, 30.4), (122.1, 30.6), (121.9, 30.6), (121.9, 30.4)),
        )
        # Also include a DEPARE that should go to hazards, not tss_lanes
        depare = EncPolygon(
            name="test_depare", feature_class="DEPARE",
            coordinates=((121.0, 30.0), (121.1, 30.0), (121.1, 30.1), (121.0, 30.1), (121.0, 30.0)),
        )
        bounds = ChartBounds(lat_min=30.0, lat_max=31.0, lon_min=121.0, lon_max=123.0)

        with tempfile.NamedTemporaryFile(suffix=".json", mode="w", delete=False) as f:
            out_path = Path(f.name)

        try:
            serialize("tss_chart", bounds, [poly_a, depare, poly_b], out_path)
            data = json.loads(out_path.read_text(encoding="utf-8"))

            assert len(data["tss_lanes"]) == 2
            assert len(data["hazards"]) == 1
        finally:
            out_path.unlink(missing_ok=True)


class TestLargeCoordinatePrecision:
    """Coordinate precision must survive JSON serialization."""

    def test_large_coordinate_precision(self):
        precise_coords = (
            (121.12345678, 30.12345678),
            (121.12345679, 30.12345679),
            (121.12345680, 30.12345680),
            (121.12345678, 30.12345678),
        )
        poly = EncPolygon(
            name="precise", feature_class="DEPARE",
            coordinates=precise_coords,
        )
        bounds = ChartBounds(lat_min=30.0, lat_max=31.0, lon_min=121.0, lon_max=122.0)

        with tempfile.NamedTemporaryFile(suffix=".json", mode="w", delete=False) as f:
            out_path = Path(f.name)

        try:
            serialize("precise_chart", bounds, [poly], out_path)
            data = json.loads(out_path.read_text(encoding="utf-8"))

            for coord, original in zip(data["hazards"][0], precise_coords):
                assert abs(coord[0] - original[0]) < 1e-8
                assert abs(coord[1] - original[1]) < 1e-8
        finally:
            out_path.unlink(missing_ok=True)
