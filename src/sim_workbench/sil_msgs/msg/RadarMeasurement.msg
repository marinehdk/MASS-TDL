# RadarMeasurement — from radar_sensor_node
# Mirrors sil.RadarMeasurement proto
# Flattened: PolarTarget repeated message serialised as parallel arrays
# Phase 2: replace with proto2ros-generated message for proper nested handling

builtin_interfaces/Time stamp

# PolarTarget parallel arrays (index-aligned)
float64[] range
float64[] bearing
float64[] rcs

# Clutter cardinality (Poisson-distributed count of false echoes)
uint32 clutter_cardinality
