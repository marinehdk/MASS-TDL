#ifndef MASS_L3_M5_GNC_PREFLIGHT_HPP_
#define MASS_L3_M5_GNC_PREFLIGHT_HPP_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "m5_tactical_planner/tail_builder/tail_builder.hpp"

namespace mass_l3::m5::gnc_preflight {

struct GncPreflightRoute {
  std::vector<double> latitude_deg;
  std::vector<double> longitude_deg;
  std::vector<double> x_m;
  std::vector<double> y_m;
  std::vector<double> speed_mps;
  std::vector<std::uint8_t> segment_source;
};

struct GncPreflightProtection {
  bool enabled{false};
  mass_l3::m5::tail_builder::ColregSide side{mass_l3::m5::tail_builder::ColregSide::NONE};
  std::string metadata;
};

struct GncPreflightInput {
  GncPreflightRoute route;
  std::optional<GncPreflightRoute> previous_route;
  mass_l3::m5::tail_builder::GncExecutionOdd odd;
  double update_interval_s{0.0};
  double current_x_m{0.0};
  double current_y_m{0.0};
  GncPreflightProtection protection;
};

struct GncPreflightResult {
  bool accepted{false};
  std::string reject_reason;
};

[[nodiscard]] GncPreflightResult validate(const GncPreflightInput& input);

}  // namespace mass_l3::m5::gnc_preflight

#endif  // MASS_L3_M5_GNC_PREFLIGHT_HPP_
