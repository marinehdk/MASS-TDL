# Y-axis Reflex Arc → M1 ODD Manager (event-driven)
# Per RFC-005 resolution
builtin_interfaces/Time stamp

bool is_active                            # true = reflex engaged, false = disengaged
string activation_reason                  # "collision_imminent" | "grounding_risk" | ...
float32 estimated_bypass_duration_s
uint8 bypassed_layers                     # bitmask: bit0=M3, bit1=M4, bit2=M5, bit3=all

float32 confidence
string rationale
