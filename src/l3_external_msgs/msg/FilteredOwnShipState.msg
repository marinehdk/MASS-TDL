# Multimodal Fusion Nav Filter → M2 World Model (50 Hz)
# Per architecture v1.1.2 §15.1 + §6 + RFC-002 resolution
# Wave 0 fix: F-IMP-B-008 — added nav_mode (RFC-002 required), current_speed/direction
builtin_interfaces/Time stamp

geographic_msgs/GeoPoint position

# Over-ground velocity (used for CPA computation by M2)
float64 sog_kn
float64 cog_deg

# True heading
float64 heading_deg

# Through-water velocity (used for hydrodynamic state — M5 MPC dynamics)
float64 u_water                           # longitudinal (m/s)
float64 v_water                           # lateral (m/s)

# Yaw rate (M5 ROT control + M1 maneuvering envelope)
float64 r_dot_deg_s

# Sea current estimate (RFC-002: M2 subtracts current to derive own velocity through water)
float64 current_speed_kn
float64 current_direction_deg

# Auxiliary motion (debug + ASDR record; not required by M2 algorithm path)
float64 roll_deg
float64 pitch_deg

# Position + velocity covariance (6x6 row-major: pos×3, vel×3)
float64[36] covariance

# Navigation mode (RFC-002 required field; M2 uses to gate confidence)
# Values: "OPTIMAL" | "DR_SHORT" | "DR_LONG" | "DEGRADED"
string nav_mode

float32 confidence
string rationale
