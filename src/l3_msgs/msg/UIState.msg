# M8 HMI Bridge → ROC HMI frontend (50 Hz)
# Contains rendering data for operator display
builtin_interfaces/Time stamp

string view_mode                      # "chart" | "radar" | "dashboard"
float64 ship_latitude_deg
float64 ship_longitude_deg
float64 ship_heading_deg
float64 ship_sog_kn
string auto_level_text

uint8 active_alert_count
uint8 critical_alert_count

float32 confidence
string rationale
