import tempfile
from pathlib import Path
from scenario_authoring.authoring.ais_source import load_ais_dataset


def test_load_noaa_csv_produces_records():
    """load_ais_dataset reads NOAA-format CSV and returns TimedAISRecord list."""
    csv_content = "MMSI,BaseDateTime,LAT,LON,SOG,COG,Heading,VesselType\r\n"
    csv_content += "123456789,2024-01-15T08:00:00,63.0,5.0,12.0,90.0,90.0,70\r\n"
    csv_content += "123456789,2024-01-15T08:00:30,63.001,5.001,12.1,91.0,91.0,70\r\n"
    csv_content += "987654321,2024-01-15T08:00:15,63.05,5.05,8.0,180.0,180.0,80\r\n"

    with tempfile.TemporaryDirectory() as tmpdir:
        csv_path = Path(tmpdir) / "test_ais.csv"
        csv_path.write_text(csv_content)
        records = load_ais_dataset(Path(tmpdir))

    assert len(records) == 3
    assert records[0].mmsi == 123456789
    assert records[0].lat == 63.0
    assert records[0].sog_kn == 12.0
    assert records[0].timestamp > 0


from scenario_authoring.authoring.ais_source import latlon_to_ne, ne_to_latlon
import numpy as np


def test_latlon_to_ne_roundtrip():
    """Round-trip WGS84 -> NE -> WGS84 preserves position within 1m."""
    lat, lon = 63.0, 5.0
    n, e = latlon_to_ne(np.array([lat]), np.array([lon]))
    lat2, lon2 = ne_to_latlon(lat, lon, float(n[0]), float(e[0]))
    assert abs(lat2 - lat) < 0.00001  # ~1m at equator
    assert abs(lon2 - lon) < 0.00001
