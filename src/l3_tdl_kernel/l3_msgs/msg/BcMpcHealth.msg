# P6 BC-MPC health metrics, consumed by mid_mpc_node for Condition A counter
# and committed_route FinalDegrade evaluation.
# Frequency: same as WorldState (BC-MPC event-driven)
builtin_interfaces/Time stamp

bool override_active                      # BC is actively issuing Override commands
float32 worst_case_cpa_m                  # Current solve worst-case CPA [m]
float32 predicted_short_horizon_cpa_m     # M2 short-horizon predicted CPA (BC inner gate input) [m]
uint32 override_no_improve_count          # Condition A counter: consecutive Override ticks with no CPA improvement
uint32 consecutive_failures               # BC-MPC solver consecutive failures counter
float32 confidence                        # BC-MPC solution confidence [0,1]
string rationale                          # SAT-2 summary
