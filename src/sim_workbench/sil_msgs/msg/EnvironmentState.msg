# EnvironmentState — from environment_server_node
# Mirrors sil.EnvironmentState proto
# Flattened: Wind, Current nested messages inlined

builtin_interfaces/Time stamp

# Wind
float64 wind_direction
float64 wind_speed_mps

# Current
float64 current_direction
float64 current_speed_mps

# Visibility
float64 visibility_nm

# Sea state (Beaufort scale 0-12)
uint32 sea_state_beaufort
