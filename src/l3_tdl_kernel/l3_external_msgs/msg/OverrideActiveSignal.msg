# Hardware Override Arbiter → M1 ODD/Envelope Manager (event-driven)
# Per architecture v1.1.2 §15.1 OverrideActiveSignal
# Wave 0 fix: F-CRIT-B-006 — field renamed override_source → activation_source per spec
string schema_version  # default: "v1.1.2"
builtin_interfaces/Time stamp

bool override_active                       # true = hardware override engaged; false = released
string activation_source                   # "manual_button" | "automatic_trigger" | "joystick" | "emergency_stop"
