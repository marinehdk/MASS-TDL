# TargetVesselState — 10Hz from target_vessel_node
# Mirrors sil.TargetVesselState proto
# Flattened: Pose, Kinematics nested messages inlined; enums as uint8

builtin_interfaces/Time stamp
uint32 mmsi

# Pose
float64 lat
float64 lon
float64 heading

# Kinematics
float64 sog
float64 cog
float64 rot

# Enums as uint8
# ShipType: 0=UNSPECIFIED 1=CARGO 2=TANKER 3=PASSENGER 4=FISHING 5=TUG 6=PLEASURE
uint8 ship_type
# TargetMode: 0=UNSPECIFIED 1=REPLAY 2=NCDM 3=INTELLIGENT
uint8 mode
