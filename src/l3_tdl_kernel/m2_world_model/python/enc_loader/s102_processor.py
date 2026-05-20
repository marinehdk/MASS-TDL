"""S-102 High-Resolution Bathymetric Surface Grid Processor.

Parses S-102 HDF5 formatted water depth gridded layers, extracting
grid resolution, depth values, and uncertainties.
"""

from __future__ import annotations

import warnings
from pathlib import Path

try:
    from osgeo import gdal
    HAS_GDAL = True
except ImportError:
    HAS_GDAL = False
    gdal = None


class S102Processor:
    """Reads S-102 bathymetric surface files and retrieves local depth/uncertainty."""

    def __init__(self, *, strict: bool = True) -> None:
        self._strict = strict
        self._ds = None
        self._depth_band = None
        self._uncertainty_band = None
        self._geotransform = None
        self._inv_geotransform = None

    def load(self, file_path: Path) -> bool:
        """Load an S-102 HDF5 grid file using GDAL's S102 raster driver."""
        if not HAS_GDAL:
            warnings.warn("GDAL bindings not available. S102Processor running in simulation mode.", ImportWarning, stacklevel=2)
            return False

        try:
            # S-102 relies on libhdf5 under the hood in GDAL 3.8+
            gdal.UseExceptions()
            self._ds = gdal.Open(str(file_path))
            if self._ds is None:
                raise RuntimeError(f"GDAL failed to open S-102 file: {file_path}")

            # S-102 Driver features Band 1 = Depth, Band 2 = Uncertainty
            self._depth_band = self._ds.GetRasterBand(1)
            self._uncertainty_band = self._ds.GetRasterBand(2)
            self._geotransform = self._ds.GetGeoTransform()

            if self._geotransform:
                self._inv_geotransform = gdal.InvGeoTransform(self._geotransform)
            return True
        except Exception as e:
            if self._strict:
                raise
            warnings.warn(f"Failed to open S-102 file: {e}. Falling back to simulation mode.", RuntimeWarning, stacklevel=2)
            return False

    def query_point(self, lon: float, lat: float) -> tuple[float, float]:
        """Query depth and uncertainty at (lon, lat) WGS 84.

        Returns:
            tuple: (depth_m, uncertainty_m) where positive depth is below vertical datum.
        """
        if self._ds is None or self._geotransform is None or self._inv_geotransform is None:
            # Simulation mode fallback: return smooth mock bathymetry based on coordinate
            mock_depth = 15.0 + 5.0 * (lon % 0.1) / 0.1
            mock_uncertainty = 0.2 + 0.05 * (lat % 0.1) / 0.1
            return mock_depth, mock_uncertainty

        # Project lon, lat to pixel space using inverse geotransform
        # Note: assumes S-102 spatial reference matches query EPSG coordinates (typically EPSG:4326/WGS84)
        inv_gt = self._inv_geotransform

        # Calculate pixel coordinates
        px = int(inv_gt[0] + inv_gt[1] * lon + inv_gt[2] * lat)
        py = int(inv_gt[3] + inv_gt[4] * lon + inv_gt[5] * lat)

        # Bounding check
        width = self._ds.RasterXSize
        height = self._ds.RasterYSize
        if 0 <= px < width and 0 <= py < height:
            depth_val = float(self._depth_band.ReadAsArray(px, py, 1, 1)[0][0])
            unc_val = float(self._uncertainty_band.ReadAsArray(px, py, 1, 1)[0][0])
            return depth_val, unc_val

        # Out of bounds fallback
        return 0.0, 999.0
