# Y-axis Reflex Arc → M1 ODD/Envelope Manager (event-driven)
# Per architecture v1.1.2 §15.1 ReflexActivationNotification + RFC-005 resolution
# Wave 0 fix: F-CRIT-B-005 — fields rewritten to match v1.1.2 IDL
#                            critical: l3_freeze_required is required by M1 state machine (§11.8)
string schema_version  # default: "v1.1.2"
builtin_interfaces/Time activation_time

string reason                             # "cpa_emergency" | "grounding_risk" | "collision_imminent"

# Tells M1 whether L3 must enter OVERRIDDEN mode (architecture §11.8)
bool l3_freeze_required
