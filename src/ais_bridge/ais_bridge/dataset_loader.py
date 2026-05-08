# SPDX-License-Identifier: Proprietary
"""Load NOAA AccessAIS CSV or DMA NMEA 0183 AIS files."""
from __future__ import annotations

import csv
from collections.abc import Generator

from ais_bridge.nmea_decoder import AISRecord, decode_file


def load_noaa_csv(filepath: str) -> Generator[AISRecord, None, None]:
    """Parse NOAA marinecadastre.gov AccessAIS CSV format."""
    with open(filepath, encoding='utf-8-sig') as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                lat = float(row['LAT'])
                lon = float(row['LON'])
                mmsi = int(row['MMSI'])
            except (KeyError, ValueError):
                continue

            sog = float(row.get('SOG') or 0.0)
            cog = float(row.get('COG') or 0.0)
            raw_hdg = row.get('Heading', '511')
            try:
                hdg_val = float(raw_hdg)
            except ValueError:
                hdg_val = 511.0
            hdg = hdg_val if hdg_val != 511.0 else cog

            ship_type = 0
            try:
                ship_type = int(float(row.get('VesselType') or 0))
            except ValueError:
                pass

            yield AISRecord(
                mmsi=mmsi, lat=lat, lon=lon,
                sog_kn=sog, cog_deg=cog,
                heading_deg=hdg,
                ship_type=ship_type,
            )


def load_dma_nmea(filepath: str) -> Generator[AISRecord, None, None]:
    """Parse DMA AIS NMEA 0183 file via pyais FileReaderStream."""
    yield from decode_file(filepath)
