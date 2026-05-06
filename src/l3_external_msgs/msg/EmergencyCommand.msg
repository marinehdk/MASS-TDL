# Y-axis Reflex Arc → L5 Control (event-driven, < 500 ms)
# Per RFC-005 resolution
builtin_interfaces/Time stamp

string emergency_type                     # "COLLISION_IMMINENT" | "GROUNDING" | "PERSON_OVERBOARD"
float32 heading_cmd_deg
float32 speed_cmd_kn
float32 rot_cmd_deg_s
float32 duration_s
bool maintain_after_expiry                # hold last cmd after duration expires
