#include "m7_safety_supervisor/mrm/mrm_03_heave_to.hpp"

#include <cstdint>
#include <string>

namespace mass_l3::m7::mrm {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

Mrm03HeaveTo::Mrm03HeaveTo(Mrm03Params const& params) noexcept
  : params_{params}
{}

// ---------------------------------------------------------------------------
// is_applicable()
// Heave-to when >= 2 targets are within 1 NM CPA.
// ---------------------------------------------------------------------------

bool Mrm03HeaveTo::is_applicable(
    l3_msgs::msg::WorldState const& world) const noexcept
{
  constexpr double kCpaThresholdM = 1852.0;   // 1.0 NM [TBD-HAZID]
  constexpr std::uint32_t kMinTargets = 2u;   // [TBD-HAZID]
  std::uint32_t close_count = 0u;
  for (auto const& t : world.targets) {
    if (t.cpa_m < kCpaThresholdM) {
      ++close_count;
    }
  }
  return close_count >= kMinTargets;
}

// ---------------------------------------------------------------------------
// rationale()
// ---------------------------------------------------------------------------

std::string Mrm03HeaveTo::rationale() const noexcept {
  return std::string{"MRM-03 HEAVE-TO: multiple targets in close proximity"};
}

}  // namespace mass_l3::m7::mrm
