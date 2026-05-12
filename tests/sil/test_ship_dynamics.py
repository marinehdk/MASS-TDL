from ship_dynamics.node import ShipDynamicsNode

def test_initial_state():
    node = ShipDynamicsNode()
    s = node.get_state()
    assert s["u"] == 10.0
    assert s["v"] == 0.0
    assert s["r"] == 0.0

def test_step_moves_forward():
    node = ShipDynamicsNode()
    node.step(throttle=1.0)
    s = node.get_state()
    assert s["x"] > 0
    assert s["y"] == 0.0  # heading=0, no lateral

def test_rudder_turns():
    node = ShipDynamicsNode()
    node.step(rudder_angle=0.5, throttle=0.0)
    assert node.get_state()["r"] != 0.0  # yaw rate changed
