# M2 World Model → TDL threat state
uint16 schema_version  # 112 = v1.1.2
builtin_interfaces/Time stamp

string cpa_status              # "closing", "sustained", "cleared"
string target_relative_position # "ahead", "astern", "port", "starboard", etc.

float32 confidence
string rationale
