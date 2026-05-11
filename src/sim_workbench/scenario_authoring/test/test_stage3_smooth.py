"""Tests for Stage 3: Savitzky-Golay smoothing."""
import numpy as np
from scenario_authoring.pipeline.stage2_resample import TrajectorySegment
from scenario_authoring.pipeline.stage3_smooth import smooth_segment


def _make_segment(sog: np.ndarray, cog: np.ndarray, mmsi: int = 111111111) -> TrajectorySegment:
    n = len(sog)
    t = np.arange(0.0, n * 0.5, 0.5)[:n]
    return TrajectorySegment(
        mmsi=mmsi, t_start=0.0, t_end=t[-1],
        t=t, lat=np.ones(n) * 63.0, lon=np.ones(n) * 5.0,
        sog_kn=sog, cog_deg=cog, heading_deg=cog.copy(),
    )


def test_smoothing_reduces_sog_variance():
    """SG filter reduces SOG variance compared to raw noisy signal."""
    np.random.seed(42)
    t = np.arange(0, 100.0, 0.5)
    sog_true = 10.0 + 0.5 * np.sin(2 * np.pi * t / 60.0)
    sog_noisy = sog_true + np.random.normal(0, 0.3, len(t))

    seg = _make_segment(sog_noisy, np.full(len(t), 90.0))
    smoothed = smooth_segment(seg)

    raw_std = np.std(sog_noisy)
    smooth_std = np.std(smoothed.sog_kn)
    assert smooth_std < raw_std  # smoothing reduces noise


def test_smooth_preserves_cog_0_360_range():
    """Smoothed COG stays in [0, 360)."""
    np.random.seed(42)
    n = 500
    t = np.arange(0, n * 0.5, 0.5)
    cog = 90.0 + 5.0 * np.sin(2 * np.pi * t / 30.0) + np.random.normal(0, 0.5, n)
    cog = cog % 360.0

    seg = _make_segment(np.full(n, 10.0), cog)
    smoothed = smooth_segment(seg)

    assert np.all(smoothed.cog_deg >= 0)
    assert np.all(smoothed.cog_deg < 360)
    assert not np.any(np.isnan(smoothed.cog_deg))
