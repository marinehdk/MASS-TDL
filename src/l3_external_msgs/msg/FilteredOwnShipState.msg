# Multimodal Fusion Nav Filter → M2 World Model (50 Hz)
# Per v1.1.2 §15.1 + RFC-002 resolution
builtin_interfaces/Time stamp

geographic_msgs/GeoPoint position
float64 sog_kn
float64 cog_deg
float64 heading_deg
float64 u_water                           # longitudinal water speed (m/s)
float64 v_water                           # lateral water speed (m/s)
float64 roll_deg
float64 pitch_deg
float64 yaw_rate_deg_s
float64[36] covariance                    # 6x6 row-major

string nav_filter_status                  # "OPTIMAL" | "CONVERGING" | "DIVERGED"

float32 confidence
string rationale
