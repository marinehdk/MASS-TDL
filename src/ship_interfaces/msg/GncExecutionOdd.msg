# GncExecutionOdd.msg — GNC execution ODD contract for TDL consumption.
#
# Published latched (transient_local) by active_route_manager_node as the single
# execution-ODD source for the L3 Tactical Decision Layer. M5 uses these limits
# to generate reachable avoidance geometry (lateral offset, turn radius) instead
# of hardcoding them. Active_route_manager is the single publisher; it declares
# the speed-envelope parameters too (duplicating ship_guidance_node) so the
# whole contract lives in one msg on one topic. Both nodes read the same overlay
# param block, so the duplicated declarations stay in sync via overlay discipline.
#
# Field units follow the GNC internal convention (m/s, m/s^2, m, deg/s).

std_msgs/Header header

# --- Speed envelope (m/s) ---
float64 emergency_avoidance_speed_cap_mps    # own speed cap during emergency_avoidance nav mode (3.2)
float64 cruise_min_speed_mps                  # speed floor in cruise/transit mode (3.8)
float64 max_transit_speed_mps                 # upper target speed (3.0)

# --- Maneuverability limits ---
float64 max_lateral_accel_mps2                # lateral acceleration cap (0.25)
float64 max_decel_mps2                        # deceleration cap (0.08)
float64 emergency_min_turn_radius_m           # geometric minimum turn radius in emergency mode (45)
float64 cruise_max_yaw_rate_deg_s             # yaw rate cap in cruise/transit mode (1.2) — planning ROT baseline
float64 emergency_max_yaw_rate_deg_s          # yaw rate cap in emergency mode (2.0)

string schema_version   # "1.1"
