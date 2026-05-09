"""S-57 ENC chart parser for M2 World Model.

Offline tool: parses ENC files and emits JSON polygons consumable by the
C++ EncLoader. Not loaded into the ROS2 runtime.

Per detailed design §5.1.4. Uses pyproj for coordinate handling and
optionally GDAL/OGR for S-57 IO; only pure-python deps are required for
unit tests.
"""

from __future__ import annotations

from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path

import pyproj


@dataclass(frozen=True, slots=True)
class EncPolygon:
    name: str
    feature_class: str       # "DEPARE" | "TSSLPT" | "OBSTRN" | ...
    coordinates: tuple[tuple[float, float], ...]  # (lon, lat) in degrees


@dataclass(frozen=True, slots=True)
class ChartBounds:
    lat_min: float
    lat_max: float
    lon_min: float
    lon_max: float


class S57Parser:
    """Parse S-57 ENC files (.000) into EncPolygon iterables.

    Per detailed design §5.1.4 + architecture v1.1.2 §6.4 EnvironmentState.
    """

    WGS84: pyproj.CRS = pyproj.CRS.from_epsg(4326)

    def __init__(self, *, strict: bool = True) -> None:
        self._strict = strict

    def parse(self, s57_path: Path) -> tuple[ChartBounds, list[EncPolygon]]:
        """Parse a single S-57 file. Returns (bounds, polygons)."""
        # Implementation TODO: use OGR if available; fall back to pure-Python
        # SCIBR record parser for tests that do not have GDAL installed.
        raise NotImplementedError("S57Parser.parse — implement in Phase E1")

    def parse_directory(
        self, directory: Path
    ) -> Iterable[tuple[ChartBounds, list[EncPolygon]]]:
        for s57_file in sorted(directory.glob("*.000")):
            yield self.parse(s57_file)
