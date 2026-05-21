"""Tests for S-57 ENC parser (s57_parser.py).

Adapted to the actual API: S57Parser.parse() returns (ChartBounds, list[EncPolygon]).
Uses mocks at the ogr module level instead of _open_dataset which doesn't exist in source.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from enc_loader.s57_parser import EncPolygon, S57Parser

# ---------------------------------------------------------------------------
# Mock GDAL/OGR classes
# ---------------------------------------------------------------------------

# GDAL geometry type constants (as defined in ogr core)
wkbPoint = 1
wkbLineString = 2
wkbPolygon = 3
wkbMultiPoint = 4
wkbMultiLineString = 5
wkbMultiPolygon = 6
wkbGeometryCollection = 7


class MockGeometry:
    """Mimics ogr.Geometry for unit-testing geometry extraction."""

    def __init__(self, geom_type: int, points=None, sub_geoms=None):
        self._type = geom_type
        self._points = points or []
        self._sub_geoms = sub_geoms or []

    def GetGeometryType(self) -> int:
        return self._type

    def GetPointCount(self) -> int:
        return len(self._points)

    def GetX(self, i: int = 0) -> float:
        return self._points[i][0]

    def GetY(self, i: int = 0) -> float:
        return self._points[i][1]

    def GetGeometryRef(self, i: int):
        if self._sub_geoms:
            return self._sub_geoms[i]
        return None

    def GetGeometryCount(self) -> int:
        return len(self._sub_geoms)


class MockFeature:
    """Mimics ogr.Feature for unit-testing feature iteration."""

    def __init__(self, geom):
        self._geom = geom

    def GetGeometryRef(self):
        return self._geom

    def GetFieldAsString(self, name: str) -> str:
        return ""

    def GetFieldAsDouble(self, name: str) -> float:
        return 0.0


class MockLayer:
    """Mimics ogr.Layer — iterable of MockFeature."""

    def __init__(self, name: str, features: list[MockFeature]):
        self._name = name
        self._features = features
        self._iter = None

    def GetName(self) -> str:
        return self._name

    def GetExtent(self):
        return (121.0, 122.0, 30.0, 31.0)

    def __iter__(self):
        self._iter = iter(self._features)
        return self

    def __next__(self):
        return next(self._iter)


class MockDataset:
    """Mimics ogr.DataSource."""

    def __init__(self, layers: list[MockLayer]):
        self._layers = layers

    def GetLayerCount(self) -> int:
        return len(self._layers)

    def GetLayer(self, i: int) -> MockLayer:
        return self._layers[i]

    def __enter__(self):
        return self

    def __exit__(self, *args):
        pass


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
def parser() -> S57Parser:
    """Return a default S57Parser instance."""
    return S57Parser(strict=True)


def _make_polygon_geom(coords: list[tuple[float, float]]) -> MockGeometry:
    """Create a polygon geometry with a single outer ring."""
    ring = MockGeometry(wkbLineString, points=coords)
    return MockGeometry(wkbPolygon, sub_geoms=[ring])


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class TestParseDepare:
    """S-57 DEPARE (depth area) parsing."""

    def test_parse_depare(self, parser, mocker):
        """mock ogr, verify DEPARE polygons have correct feature_class."""
        # Arrange — 3 polygon features in a DEPARE layer
        coords_a = [(121.0, 30.0), (121.1, 30.0), (121.1, 30.1), (121.0, 30.1), (121.0, 30.0)]
        coords_b = [(121.2, 30.2), (121.3, 30.2), (121.3, 30.3), (121.2, 30.3), (121.2, 30.2)]
        coords_c = [(121.4, 30.4), (121.5, 30.4), (121.5, 30.5), (121.4, 30.5), (121.4, 30.4)]

        features = [
            MockFeature(_make_polygon_geom(coords_a)),
            MockFeature(_make_polygon_geom(coords_b)),
            MockFeature(_make_polygon_geom(coords_c)),
        ]
        layer = MockLayer("DEPARE", features)
        dataset = MockDataset([layer])

        # Mock ogr module at the s57_parser level
        mocker.patch("enc_loader.s57_parser.HAS_GDAL", True)
        mock_ogr = mocker.patch("enc_loader.s57_parser.ogr", autospec=False)
        mock_ogr.GetDriverByName.return_value = object()  # driver exists
        mock_ogr.Open.return_value = dataset
        mock_ogr.SetConfigFileOption = lambda k, v: None
        mock_ogr.UseExceptions = lambda: None
        # Attach geometry type constants to the mock
        mock_ogr.wkbPoint = wkbPoint
        mock_ogr.wkbLineString = wkbLineString
        mock_ogr.wkbPolygon = wkbPolygon
        mock_ogr.wkbMultiPoint = wkbMultiPoint
        mock_ogr.wkbMultiLineString = wkbMultiLineString
        mock_ogr.wkbMultiPolygon = wkbMultiPolygon
        mock_ogr.wkbGeometryCollection = wkbGeometryCollection

        # Act
        bounds, polygons = parser.parse(Path("fake.000"))

        # Assert
        assert len(polygons) == 3
        for poly in polygons:
            assert poly.feature_class == "DEPARE"
            assert len(poly.coordinates) >= 3  # valid polygon


class TestParseObstrn:
    """S-57 OBSTRN (obstruction) parsing — typically point features."""

    def test_parse_obstrn(self, parser, mocker):
        """mock ogr, verify OBSTRN polygons have correct feature_class."""
        # Arrange — obstructions as point geometries
        pt_a = MockGeometry(wkbPoint, points=[(122.0, 30.5)])
        pt_b = MockGeometry(wkbPoint, points=[(122.1, 30.6)])

        features = [
            MockFeature(pt_a),
            MockFeature(pt_b),
        ]
        layer = MockLayer("OBSTRN", features)
        dataset = MockDataset([layer])

        mocker.patch("enc_loader.s57_parser.HAS_GDAL", True)
        mock_ogr = mocker.patch("enc_loader.s57_parser.ogr", autospec=False)
        mock_ogr.GetDriverByName.return_value = object()
        mock_ogr.Open.return_value = dataset
        mock_ogr.SetConfigFileOption = lambda k, v: None
        mock_ogr.UseExceptions = lambda: None
        mock_ogr.wkbPoint = wkbPoint
        mock_ogr.wkbLineString = wkbLineString
        mock_ogr.wkbPolygon = wkbPolygon
        mock_ogr.wkbMultiPoint = wkbMultiPoint
        mock_ogr.wkbMultiLineString = wkbMultiLineString
        mock_ogr.wkbMultiPolygon = wkbMultiPolygon
        mock_ogr.wkbGeometryCollection = wkbGeometryCollection

        # Act
        bounds, polygons = parser.parse(Path("fake.000"))

        # Assert
        assert len(polygons) == 2
        for poly in polygons:
            assert poly.feature_class == "OBSTRN"


class TestParseTsslpt:
    """S-57 TSSLPT (Traffic Separation Scheme lane part) parsing."""

    def test_parse_tsslpt(self, parser, mocker):
        """mock ogr, verify TSSLPT polygons are returned."""
        # Arrange — TSS lane polygons
        coords_a = [(121.8, 30.3), (122.2, 30.3), (122.2, 30.7), (121.8, 30.7), (121.8, 30.3)]
        coords_b = [(121.9, 30.4), (122.1, 30.4), (122.1, 30.6), (121.9, 30.6), (121.9, 30.4)]

        features = [
            MockFeature(_make_polygon_geom(coords_a)),
            MockFeature(_make_polygon_geom(coords_b)),
        ]
        layer = MockLayer("TSSLPT", features)
        dataset = MockDataset([layer])

        mocker.patch("enc_loader.s57_parser.HAS_GDAL", True)
        mock_ogr = mocker.patch("enc_loader.s57_parser.ogr", autospec=False)
        mock_ogr.GetDriverByName.return_value = object()
        mock_ogr.Open.return_value = dataset
        mock_ogr.SetConfigFileOption = lambda k, v: None
        mock_ogr.UseExceptions = lambda: None
        mock_ogr.wkbPoint = wkbPoint
        mock_ogr.wkbLineString = wkbLineString
        mock_ogr.wkbPolygon = wkbPolygon
        mock_ogr.wkbMultiPoint = wkbMultiPoint
        mock_ogr.wkbMultiLineString = wkbMultiLineString
        mock_ogr.wkbMultiPolygon = wkbMultiPolygon
        mock_ogr.wkbGeometryCollection = wkbGeometryCollection

        # Act
        bounds, polygons = parser.parse(Path("fake.000"))

        # Assert
        assert len(polygons) == 2
        for poly in polygons:
            assert poly.feature_class == "TSSLPT"
            assert len(poly.coordinates) >= 3


class TestParseLndare:
    """S-57 LNDARE (land area) parsing — exclusion zone polygons."""

    def test_parse_lndare(self, parser, mocker):
        """mock ogr, verify LNDARE exclusion zone polygons."""
        # Arrange — land area as polygon
        coords = [(121.5, 30.5), (122.0, 30.5), (122.0, 31.0), (121.5, 31.0), (121.5, 30.5)]
        features = [MockFeature(_make_polygon_geom(coords))]
        layer = MockLayer("LNDARE", features)

        # Also include a DEPARE layer that should be filtered out
        depare = MockLayer("DEPARE", [])
        dataset = MockDataset([layer, depare])

        mocker.patch("enc_loader.s57_parser.HAS_GDAL", True)
        mock_ogr = mocker.patch("enc_loader.s57_parser.ogr", autospec=False)
        mock_ogr.GetDriverByName.return_value = object()
        mock_ogr.Open.return_value = dataset
        mock_ogr.SetConfigFileOption = lambda k, v: None
        mock_ogr.UseExceptions = lambda: None
        mock_ogr.wkbPoint = wkbPoint
        mock_ogr.wkbLineString = wkbLineString
        mock_ogr.wkbPolygon = wkbPolygon
        mock_ogr.wkbMultiPoint = wkbMultiPoint
        mock_ogr.wkbMultiLineString = wkbMultiLineString
        mock_ogr.wkbMultiPolygon = wkbMultiPolygon
        mock_ogr.wkbGeometryCollection = wkbGeometryCollection

        # Act
        bounds, polygons = parser.parse(Path("fake.000"))

        # Assert — only LNDARE should be returned (DEPARE layer is empty)
        assert len(polygons) == 1
        assert polygons[0].feature_class == "LNDARE"
        # Verify it's a proper polygon (closed ring)
        assert polygons[0].coordinates[0] == polygons[0].coordinates[-1]


class TestNoGdalFallback:
    """Parser behavior when GDAL is not available."""

    def test_no_gdal_fallback(self, parser, mocker):
        """when GDAL unavailable, parser should return mock data gracefully."""
        # Arrange — simulate no GDAL
        mocker.patch("enc_loader.s57_parser.HAS_GDAL", False)

        # Act — should not crash
        bounds, polygons = parser.parse(Path("nonexistent.000"))

        # Assert — returns mock data with valid EncPolygon objects
        assert isinstance(polygons, list)
        assert len(polygons) > 0
        for poly in polygons:
            assert isinstance(poly, EncPolygon)
            assert poly.feature_class in {"DEPARE", "OBSTRN", "TSSLPT"}
            assert len(poly.coordinates) >= 1
