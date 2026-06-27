# ShipReset.msg — GNC plant runtime reset command.
# Sent by the orchestrator on every scenario configure (via gnc_bridge).
# On receipt: coordinate_transform resets origin to (latitude, longitude),
# ship_dynamics resets eta_ to (0,0) and sets heading/speed.
std_msgs/Header header
float64 latitude    # new origin latitude (deg), = scenario ownShip.initial.position.latitude
float64 longitude   # new origin longitude (deg), = scenario ownShip.initial.position.longitude
float64 heading_deg # own-ship initial heading (deg), = scenario ownShip.initial.heading
float64 sog_kn      # own-ship initial speed over ground (kn), = scenario ownShip.initial.sog
