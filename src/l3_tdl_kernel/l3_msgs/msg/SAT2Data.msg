uint16 schema_version  # 113 = v1.1.3 D2.4 patch
builtin_interfaces/Time stamp
float32 confidence  # [0, 1]
string rationale    # SAT-2 summary

# SAT2Data — Reasoning
string trigger_reason
string reasoning_chain
float32 system_confidence

# D2.4 NEW: structured COLREGs chain
ColregsChainLayer[] colregs_chain
string colregs_chain_target_id
float32 reasoning_latency_ms

# D3.4 M4 BehaviorArbiter: IvP contribution vector
float32[6] ivp_contributions
string[6] ivp_labels
