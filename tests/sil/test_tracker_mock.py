"""Tests for tracker_mock node — God pass-through + KF tracker stubs."""
import math
from tracker_mock.node import TrackerMockNode, KalmanFilter2D


# ---------------------------------------------------------------------------
# God tracker
# ---------------------------------------------------------------------------

def test_god_tracker_passthrough():
    node = TrackerMockNode("god")
    targets = [{
        "mmsi": 1, "lat": 63.4, "lon": 10.4,
        "sog": 5.0, "cog": 0.5, "heading": 0.0,
    }]
    result = node.track(targets)
    assert result[0]["lat"] == 63.4
    assert result[0]["lon"] == 10.4
    assert result[0]["sog"] == 5.0
    assert result[0]["cog"] == 0.5


def test_god_tracker_multiple_targets():
    node = TrackerMockNode("god")
    targets = [
        {"mmsi": 1, "lat": 63.4, "lon": 10.4, "sog": 5.0, "cog": 0.5, "heading": 0.0},
        {"mmsi": 2, "lat": 63.5, "lon": 10.5, "sog": 3.0, "cog": 1.2, "heading": 0.0},
    ]
    results = node.track(targets)
    assert len(results) == 2


# ---------------------------------------------------------------------------
# KF tracker
# ---------------------------------------------------------------------------

def test_kf_init_tracks():
    node = TrackerMockNode("kf")
    targets = [{
        "mmsi": 1, "lat": 63.4, "lon": 10.4,
        "sog": 5.0, "cog": 0.5, "heading": 0.0,
    }]
    result = node.track(targets)
    assert len(result) == 1
    # KF initializes at measurement position
    assert abs(result[0]["lat"] - 63.4) < 0.1
    assert abs(result[0]["lon"] - 10.4) < 0.1


def test_kf_persists_track_per_mmsi():
    node = TrackerMockNode("kf")
    # Two consecutive updates for the same target
    for _ in range(3):
        node.track([{
            "mmsi": 1, "lat": 63.4, "lon": 10.4,
            "sog": 5.0, "cog": 0.5, "heading": 0.0,
        }])
    result = node.track([{
        "mmsi": 1, "lat": 63.41, "lon": 10.41,
        "sog": 5.0, "cog": 0.5, "heading": 0.0,
    }])
    # After several predict+update cycles the position should track the measurements
    lat = result[0]["lat"]
    lon = result[0]["lon"]
    assert abs(lat - 63.41) < 0.02
    assert abs(lon - 10.41) < 0.02


# ---------------------------------------------------------------------------
# KalmanFilter2D unit
# ---------------------------------------------------------------------------

def test_kalman_filter_init():
    kf = KalmanFilter2D()
    kf.init(100.0, 200.0, 1.0, 0.0)
    s = kf.get_state()
    assert s["x"] == 100.0
    assert s["y"] == 200.0
    assert s["vx"] == 1.0
    assert s["vy"] == 0.0


def test_kalman_filter_predict():
    kf = KalmanFilter2D(dt=0.5)
    kf.init(0.0, 0.0, 2.0, 1.0)
    kf.predict()
    s = kf.get_state()
    # After 0.5s at vx=2, vy=1
    assert s["x"] == 1.0
    assert s["y"] == 0.5
    assert s["vx"] == 2.0
    assert s["vy"] == 1.0


def test_kalman_filter_init_on_first_update():
    kf = KalmanFilter2D()
    assert kf.x is None
    kf.update(10.0, 20.0)
    assert kf.x is not None
    s = kf.get_state()
    assert s["x"] == 10.0
    assert s["y"] == 20.0
    assert s["vx"] == 0.0
    assert s["vy"] == 0.0


def test_kalman_filter_update_overwrites_position():
    kf = KalmanFilter2D(dt=0.1)
    kf.init(100.0, 200.0, 1.0, 0.0)
    kf.predict()  # x → 100.1
    kf.update(105.0, 205.0)
    s = kf.get_state()
    assert s["x"] == 105.0  # overwritten by measurement
    assert s["y"] == 205.0
