# Multimodal Fusion → M2 World Model (2 Hz)
# Per v1.1.2 §15.1 TrackedTargetArray + RFC-002 resolution
builtin_interfaces/Time stamp

uint64[] target_ids
geographic_msgs/GeoPoint[] positions
float64[] sog_kn
float64[] cog_deg
float64[] heading_deg
string[] classifications
float32[] classification_confidences
uint8[] status                            # 0=new, 1=tracked, 2=coasting, 3=lost

float32 confidence
string rationale
