# Hardware Override Arbiter → M1 ODD Manager (event-driven)
# Per v1.1.2 §15.1 + RFC-005
builtin_interfaces/Time stamp

bool override_active                       # true = hardware override engaged
string override_source                    # "helm" | "throttle" | "joystick" | "emergency_stop"
builtin_interfaces/Time engaged_stamp
string expected_duration                  # "momentary" | "extended" | "indefinite"

float32 confidence
string rationale
