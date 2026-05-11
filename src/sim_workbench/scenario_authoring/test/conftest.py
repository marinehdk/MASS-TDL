from dataclasses import dataclass
import pytest


@dataclass
class TimedAISRecord:
    """AIS record with timestamp for pipeline processing."""
    mmsi: int
    lat: float
    lon: float
    sog_kn: float
    cog_deg: float
    heading_deg: float
    ship_type: int
    timestamp: float  # Unix epoch seconds


@pytest.fixture
def sample_records_one_vessel():
    """12 records for one vessel, 30s apart, no gaps."""
    base_ts = 1716150000.0
    return [
        TimedAISRecord(mmsi=123456789, lat=63.0, lon=5.0, sog_kn=12.0, cog_deg=90.0, heading_deg=90.0, ship_type=70, timestamp=base_ts + i*30)
        for i in range(12)
    ]


@pytest.fixture
def sample_records_two_vessels():
    """Two vessels, 5 records each, interleaved timestamps."""
    base_ts = 1716150000.0
    v1 = [TimedAISRecord(mmsi=111111111, lat=63.0, lon=5.0, sog_kn=10.0, cog_deg=0.0, heading_deg=0.0, ship_type=70, timestamp=base_ts + i*60) for i in range(5)]
    v2 = [TimedAISRecord(mmsi=222222222, lat=63.1, lon=5.1, sog_kn=8.0, cog_deg=180.0, heading_deg=180.0, ship_type=80, timestamp=base_ts + 30 + i*60) for i in range(5)]
    return v1 + v2


@pytest.fixture
def sample_records_sparse_vessel():
    """One vessel with 14 records, another with only 3 (<10 threshold)."""
    base_ts = 1716150000.0
    v1 = [TimedAISRecord(mmsi=111111111, lat=63.0, lon=5.0, sog_kn=10.0, cog_deg=0.0, heading_deg=0.0, ship_type=70, timestamp=base_ts + i*30) for i in range(14)]
    v2 = [TimedAISRecord(mmsi=222222222, lat=63.1, lon=5.1, sog_kn=8.0, cog_deg=180.0, heading_deg=180.0, ship_type=80, timestamp=base_ts + i*30) for i in range(3)]
    return v1 + v2
