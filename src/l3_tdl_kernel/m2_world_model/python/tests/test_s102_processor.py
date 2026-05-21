"""Tests for S-102 bathymetric grid processor (s102_processor.py).

Adapted to the actual API: S102Processor.load() / query_point().
Mocks gdal at the module level instead of _open_grid which doesn't exist in source.
"""

from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from enc_loader.s102_processor import S102Processor


class MockS102Band:
    """Mimics GDAL raster band for S-102 depth/uncertainty grids."""

    def __init__(self, width: int, height: int, fill_value: float = 15.0):
        self._width = width
        self._height = height
        self._fill_value = fill_value

    def ReadAsArray(self, xoff: int, yoff: int, xsize: int, ysize: int) -> np.ndarray:
        return np.full((ysize, xsize), self._fill_value, dtype=np.float64)


class MockS102Dataset:
    """Mimics gdal.Dataset for S-102 HDF5 grid files."""

    def __init__(
        self,
        width: int = 2000,
        height: int = 2000,
        geotransform: tuple[float, ...] | None = None,
        depth_value: float = 15.0,
        unc_value: float = 0.5,
    ):
        self.RasterXSize = width
        self.RasterYSize = height
        self._geotransform = geotransform or (121.0, 0.001, 0.0, 31.0, 0.0, -0.001)
        self._depth_band = MockS102Band(width, height, fill_value=depth_value)
        self._uncertainty_band = MockS102Band(width, height, fill_value=unc_value)

    def GetGeoTransform(self):
        return self._geotransform

    def GetRasterBand(self, i: int):
        return self._depth_band if i == 1 else self._uncertainty_band

    def __enter__(self):
        return self

    def __exit__(self, *args):
        pass


@pytest.fixture
def processor() -> S102Processor:
    return S102Processor(strict=True)


class TestReadDepth:
    """Loading a grid and querying depth at a known coordinate."""

    def test_read_depth(self, processor, mocker):
        # Grid: lon [121.0, 121.1), lat [30.9, 31.0), 100x100 pixels, 0.001 deg res
        width, height = 100, 100
        gt = (121.0, 0.001, 0.0, 31.0, 0.0, -0.001)
        dataset = MockS102Dataset(width=width, height=height, geotransform=gt, depth_value=25.0)

        mocker.patch("enc_loader.s102_processor.HAS_GDAL", True)
        mock_gdal = mocker.patch("enc_loader.s102_processor.gdal", autospec=False)
        mock_gdal.Open.return_value = dataset
        mock_gdal.InvGeoTransform.return_value = (
            -121000.0, 1000.0, 0.0,
            31000.0, 0.0, -1000.0,
        )

        loaded = processor.load(Path("test.h5"))
        assert loaded is True

        # (lon=121.05, lat=30.95) → pixel (50, 50), within 100x100 bounds
        depth, unc = processor.query_point(121.05, 30.95)
        assert depth == 25.0
        assert unc == 0.5


class TestNoGdalFallback:
    """Graceful fallback when GDAL is unavailable."""

    def test_no_gdal_fallback(self, processor, mocker):
        mocker.patch("enc_loader.s102_processor.HAS_GDAL", False)

        loaded = processor.load(Path("nonexistent.h5"))
        assert loaded is False

        depth, unc = processor.query_point(30.0, 122.0)
        assert depth > 0
        assert unc > 0


class TestQueryOutsideBounds:
    """Querying a coordinate outside the loaded grid returns (0.0, 999.0)."""

    def test_query_outside_bounds(self, processor, mocker):
        width, height = 1, 1  # Only 1 pixel covering one location
        gt = (122.0, 0.001, 0.0, 30.001, 0.0, -0.001)
        dataset = MockS102Dataset(width=width, height=height, geotransform=gt)

        mocker.patch("enc_loader.s102_processor.HAS_GDAL", True)
        mock_gdal = mocker.patch("enc_loader.s102_processor.gdal", autospec=False)
        mock_gdal.Open.return_value = dataset
        mock_gdal.InvGeoTransform.return_value = (
            -122000.0, 1000.0, 0.0,
            30001.0, 0.0, -1000.0,
        )

        processor.load(Path("small.h5"))

        depth, unc = processor.query_point(30.0, 121.0)
        assert depth == 0.0
        assert unc == 999.0


class TestResolutionAccess:
    """Geotransform resolution is correctly used for pixel coordinate calculation."""

    def test_resolution_access(self, processor, mocker):
        width, height = 100, 100
        geotransform = (121.0, 0.001, 0.0, 31.0, 0.0, -0.001)
        dataset = MockS102Dataset(width=width, height=height, geotransform=geotransform)

        mocker.patch("enc_loader.s102_processor.HAS_GDAL", True)
        mock_gdal = mocker.patch("enc_loader.s102_processor.gdal", autospec=False)
        mock_gdal.Open.return_value = dataset
        mock_gdal.InvGeoTransform.return_value = (
            -121000.0, 1000.0, 0.0,
            31000.0, 0.0, -1000.0,
        )

        processor.load(Path("res_test.h5"))

        depth, unc = processor.query_point(121.005, 30.995)
        assert depth == pytest.approx(15.0)
        assert unc == pytest.approx(0.5)
