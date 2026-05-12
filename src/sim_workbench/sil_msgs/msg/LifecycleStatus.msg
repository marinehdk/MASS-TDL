# LifecycleStatus — from lifecycle_manager_node
# Mirrors sil.LifecycleStatus proto
# LifecycleState enum flattened as uint8

builtin_interfaces/Time stamp

# LifecycleState: 0=UNSPECIFIED 1=UNCONFIGURED 2=INACTIVE 3=ACTIVE 4=DEACTIVATING 5=FINALIZED
uint8 current_state
string scenario_id
string scenario_hash
float64 sim_time
float64 wall_time
float64 sim_rate
