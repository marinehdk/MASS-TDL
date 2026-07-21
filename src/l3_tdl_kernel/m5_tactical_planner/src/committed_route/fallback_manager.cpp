#include "m5_tactical_planner/committed_route/fallback_manager.hpp"

namespace mass_l3::m5::committed_route {

void FallbackManager::record_final_degrade(
    const std::uint32_t bc_override_no_improve_count,
    const std::uint32_t mid_unrecovered_count,
    const double bc_worst_case_cpa_m,
    const bool mid_last_converged,
    std::string safety_concern_event,
    std::string lifecycle_state,
    std::string suggested_action,
    const double timestamp_s) {
  evidence_.triggered = true;
  evidence_.timestamp_s = timestamp_s;
  evidence_.bc_override_no_improve_count = bc_override_no_improve_count;
  evidence_.mid_unrecovered_count = mid_unrecovered_count;
  evidence_.bc_worst_case_cpa_m = bc_worst_case_cpa_m;
  evidence_.mid_last_converged = mid_last_converged;
  evidence_.safety_concern_event = std::move(safety_concern_event);
  evidence_.lifecycle_state = std::move(lifecycle_state);
  evidence_.suggested_action = std::move(suggested_action);
}

}  // namespace mass_l3::m5::committed_route
