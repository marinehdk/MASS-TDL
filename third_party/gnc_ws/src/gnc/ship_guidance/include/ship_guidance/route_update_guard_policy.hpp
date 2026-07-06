#ifndef SHIP_GUIDANCE_ROUTE_UPDATE_GUARD_POLICY_HPP
#define SHIP_GUIDANCE_ROUTE_UPDATE_GUARD_POLICY_HPP

#include <cmath>

namespace ship_guidance {

inline bool should_enforce_route_update_interval(
    bool has_emergency_avoidance,
    bool emergency_avoidance_relax_update_guard)
{
    return !(has_emergency_avoidance && emergency_avoidance_relax_update_guard);
}

inline bool should_bypass_dynamic_update_guard_for_post_colregs_rejoin(
    bool incoming_colregs_protected,
    bool previous_colregs_protected,
    double current_cross_track_error_m,
    double max_allowed_cross_track_m)
{
    if (incoming_colregs_protected || !previous_colregs_protected) {
        return false;
    }
    if (!std::isfinite(current_cross_track_error_m) ||
        !std::isfinite(max_allowed_cross_track_m) ||
        max_allowed_cross_track_m <= 0.0) {
        return false;
    }
    return std::abs(current_cross_track_error_m) <= max_allowed_cross_track_m;
}

}  // namespace ship_guidance

#endif  // SHIP_GUIDANCE_ROUTE_UPDATE_GUARD_POLICY_HPP
