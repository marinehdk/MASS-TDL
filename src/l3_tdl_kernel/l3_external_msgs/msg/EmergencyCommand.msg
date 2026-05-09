# Y-axis Reflex Arc → L5 Control Allocation (event-driven, < 500 ms)
# Per architecture v1.1.2 §15.1 EmergencyCommand + RFC-005 resolution
# Wave 0 fix: F-CRIT-B-004 — fields rewritten to match v1.1.2 IDL (action enum)
string schema_version  # default: "v1.1.2"
builtin_interfaces/Time trigger_time

float32 cpa_at_trigger_m                  # CPA at the moment Reflex Arc triggered
float32 range_at_trigger_m                # Range at the moment Reflex Arc triggered
string sensor_source                      # "fusion" | "lidar_emergency" | "radar_emergency"

# Action enum (per architecture §15.1 EmergencyCommand IDL)
uint8 action
uint8 ACTION_STOP = 0
uint8 ACTION_REVERSE = 1
uint8 ACTION_HARD_TURN = 2

float32 confidence
