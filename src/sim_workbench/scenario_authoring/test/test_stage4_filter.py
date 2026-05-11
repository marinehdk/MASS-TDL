"""Tests for Stage 4: DCPA/TCPA filtering and COLREGs geometric classification."""
import numpy as np
from scenario_authoring.pipeline.stage4_filter import (
    classify_colreg_encounter,
    compute_dcpa_tcpa_relative_motion,
)


def test_dcpa_zero_for_collision_course():
    """Two ships on collision course -> DCPA ~0."""
    n_pts = 100
    os_lat = np.full(n_pts, 63.0)
    os_lon = np.linspace(5.0, 5.05, n_pts)  # moving east
    os_sog = np.full(n_pts, 10.0)
    os_cog = np.full(n_pts, 90.0)

    ts_lat = np.full(n_pts, 63.0)
    ts_lon = np.linspace(5.05, 5.0, n_pts)  # moving west
    ts_sog = np.full(n_pts, 10.0)
    ts_cog = np.full(n_pts, 270.0)

    dcpa, tcpa = compute_dcpa_tcpa_relative_motion(
        os_lat, os_lon, os_sog, os_cog,
        ts_lat, ts_lon, ts_sog, ts_cog,
    )
    assert dcpa < 50.0  # near-collision


def test_dcpa_large_for_parallel_courses():
    """Two ships on parallel courses -> DCPA stays large."""
    n_pts = 100
    os_lat = np.full(n_pts, 63.0)
    os_lon = np.linspace(5.0, 5.05, n_pts)
    os_sog = np.full(n_pts, 10.0)
    os_cog = np.full(n_pts, 90.0)

    ts_lat = np.full(n_pts, 63.01)  # ~1.1km north
    ts_lon = np.linspace(5.0, 5.05, n_pts)
    ts_sog = np.full(n_pts, 10.0)
    ts_cog = np.full(n_pts, 90.0)

    dcpa, tcpa = compute_dcpa_tcpa_relative_motion(
        os_lat, os_lon, os_sog, os_cog,
        ts_lat, ts_lon, ts_sog, ts_cog,
    )
    assert dcpa > 500.0  # safe passage


def test_classify_head_on():
    """Target bearing near 0° with opposing heading -> HEAD_ON."""
    result = classify_colreg_encounter(
        bearing_deg=2.0, target_heading_deg=180.0, own_heading_deg=0.0,
    )
    assert result == "HEAD_ON"


def test_classify_overtaking():
    """Target bearing in stern sector [112.5°, 247.5°] -> OVERTAKING."""
    result = classify_colreg_encounter(
        bearing_deg=180.0, target_heading_deg=90.0, own_heading_deg=90.0,
    )
    assert result == "OVERTAKING"


def test_classify_crossing():
    """Target bearing on beam -> CROSSING."""
    result = classify_colreg_encounter(
        bearing_deg=45.0, target_heading_deg=90.0, own_heading_deg=0.0,
    )
    assert result == "CROSSING"
