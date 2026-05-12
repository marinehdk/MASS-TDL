from sensor_mock.node import SensorMockNode


def test_ais_no_drop():
    node = SensorMockNode(ais_drop_pct=0)
    msg = node.generate_ais(63.4, 10.4, {"mmsi": 1, "sog": 5.0, "cog": 0.5, "lat": 63.41, "lon": 10.41, "heading": 0.5})
    assert msg is not None
    assert msg["dropout_flag"] is False


def test_ais_100pct_drop():
    node = SensorMockNode(ais_drop_pct=100)
    msg = node.generate_ais(63.4, 10.4, {"mmsi": 1, "sog": 5.0, "cog": 0.5, "lat": 63.41, "lon": 10.41, "heading": 0.5})
    assert msg is None


def test_radar_detects_nearby_target():
    node = SensorMockNode()
    result = node.generate_radar(63.4, 10.4, [{"lat": 63.401, "lon": 10.401, "heading": 0.0}])
    assert len(result["polar_targets"]) == 1
    assert "clutter_cardinality" in result


def test_radar_ignores_far_target():
    node = SensorMockNode(radar_max_range=100)
    result = node.generate_radar(63.4, 10.4, [{"lat": 64.0, "lon": 11.0, "heading": 0.0}])
    assert len(result["polar_targets"]) == 0
