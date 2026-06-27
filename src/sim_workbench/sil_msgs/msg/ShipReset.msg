# ShipReset.msg — GNC plant runtime reset command (L3-side mirror).
# Orchestrator publishes this on /l3/sim/reset_own_ship (domain 42);
# gnc_bridge translates to ship_interfaces::ShipReset on domain 50.
std_msgs/Header header
float64 latitude    # new origin latitude (deg), = scenario ownShip.initial.position.latitude
float64 longitude   # new origin longitude (deg), = scenario ownShip.initial.position.longitude
float64 heading_deg # own-ship initial heading (deg), = scenario ownShip.initial.heading
float64 sog_kn      # own-ship initial speed over ground (kn), = scenario ownShip.initial.sog
