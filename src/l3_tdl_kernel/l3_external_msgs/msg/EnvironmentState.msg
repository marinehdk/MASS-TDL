# Multimodal Fusion → M2 World Model (0.2 Hz)
# Per v1.1.2 §15.1 EnvironmentState
string schema_version  # default: "v1.1.2"
builtin_interfaces/Time stamp

float64 wind_speed_kn
float64 wind_direction_deg                # true wind direction
float64 wave_height_m
float64 wave_direction_deg
float64 current_speed_kn
float64 current_direction_deg
float64 visibility_range_nm
string weather_source                     # "sensor" | "forecast"

float32 confidence
string rationale
