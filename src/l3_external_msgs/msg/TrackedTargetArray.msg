# Multimodal Fusion → M2 World Model (2 Hz)
# Per architecture v1.1.2 §15.1 TrackedTargetArray + RFC-002 resolution
# Wave 0 fix: F-IMP-B-003 — switched from SoA to AoS using l3_msgs/TrackedTarget[]
#                            so M2 has full per-target context (covariance / encounter / cpa_m / source_sensor)
builtin_interfaces/Time stamp

# Array of fully-populated targets (each carries full TrackedTarget fields:
# stamp / target_id / position / sog_kn / cog_deg / heading_deg / covariance /
# classification / classification_confidence / cpa_m / tcpa_s / encounter / source_sensor / confidence)
l3_msgs/TrackedTarget[] targets

float32 confidence
string rationale
