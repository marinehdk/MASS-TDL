# ModulePulse — heartbeat/health from each M1-M8 node
# Mirrors sil.ModulePulse proto
# Enums flattened as uint8

builtin_interfaces/Time stamp

# ModuleId: 0=UNSPECIFIED 1=M1 2=M2 3=M3 4=M4 5=M5 6=M6 7=M7 8=M8
uint8 module_id
# HealthState: 0=UNSPECIFIED 1=GREEN 2=AMBER 3=RED
uint8 state

uint32 latency_ms
uint32 message_drops
