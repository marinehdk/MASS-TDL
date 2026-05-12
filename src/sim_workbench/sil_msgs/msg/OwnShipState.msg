# OwnShipState — 50Hz from ship_dynamics_node
# Mirrors sil.OwnShipState proto
# Flattened: Pose, Kinematics, ControlState nested messages inlined

builtin_interfaces/Time stamp

# Pose
float64 lat
float64 lon
float64 heading

# Kinematics
float64 sog
float64 cog
float64 rot
float64 u
float64 v
float64 r

# ControlState
float64 rudder_angle
float64 throttle
