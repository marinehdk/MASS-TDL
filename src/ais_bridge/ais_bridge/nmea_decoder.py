# SPDX-License-Identifier: Proprietary
"""Decode AIVDM/AIVDO sentences to AISRecord dataclass using pyais."""
from __future__ import annotations

import warnings
from dataclasses import dataclass
from typing import Optional, Generator

from pyais.stream import FileReaderStream

@dataclass
class AISRecord:
    """Decoded AIS position report from a single target vessel."""
    mmsi: int
    lat: float
    lon: float
    sog_kn: float
    cog_deg: float
    heading_deg: float  # COG used as fallback when AIS reports 511 (unavailable)
    ship_type: int      # AIS VesselType byte; 0=unknown, 20-99=vessels, 100+=other

# AIS message types carrying position
_POSITION_TYPES = frozenset((1, 2, 3, 18))


def decode_file(filepath: str) -> Generator[AISRecord, None, None]:
    """Yield AISRecord for each valid position report in a NMEA AIS file.
    Uses pyais FileReaderStream which handles multi-part AIVDM reassembly.
    """
    for msg in FileReaderStream(filepath):
        try:
            decoded = msg.decode()
        except Exception as e:
            warnings.warn(f"AIS decode failed: {e}", stacklevel=2)
            continue

        if decoded.msg_type not in _POSITION_TYPES:
            continue

        try:
            lat = float(decoded.lat)
            lon = float(decoded.lon)
        except (TypeError, ValueError):
            continue

        sog = float(decoded.speed or 0.0)
        cog = float(decoded.course or 0.0)

        raw_hdg = getattr(decoded, 'heading', 511)
        hdg = float(raw_hdg) if raw_hdg not in (511, None) else cog

        ship_type = int(getattr(decoded, 'ship_type', 0) or 0)

        yield AISRecord(
            mmsi=int(decoded.mmsi),
            lat=lat, lon=lon,
            sog_kn=sog, cog_deg=cog,
            heading_deg=hdg,
            ship_type=ship_type,
        )
