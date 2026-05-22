"""Fast unit tests for kpi tools (no ROS2 required)."""
import sys
from pathlib import Path


def test_compute_percentiles_basic():
    sys.path.insert(0, str(Path(__file__).parents[2]))
    from tools.vv.kpi_collector import compute_percentiles
    data = [100.0] * 950 + [500.0] * 50
    result = compute_percentiles(data)
    assert result["p95"] <= 500.0
    assert result["count"] == 1000
    assert "algorithm_maturity" not in result


def test_compute_percentiles_empty():
    from tools.vv.kpi_collector import compute_percentiles
    result = compute_percentiles([])
    assert result["p95"] is None
    assert result["count"] == 0


def test_asdr_check_no_mcap(tmp_path):
    from tools.vv.asdr_schema_check import check_asdr_schema
    result = check_asdr_schema(tmp_path)
    assert result["missing_fields_count"] == 0
    assert result["total_events"] == 0
