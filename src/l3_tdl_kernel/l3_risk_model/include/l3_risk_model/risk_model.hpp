#pragma once

#include <cstdint>
#include <string>

namespace mass_l3::risk {

enum class ColregsDuty : std::uint8_t { Free, StandOnHold, GiveWay, BothGiveWay, Rule17Action };

enum class RiskPhase : std::uint8_t { Clear, Monitor, Warning, Danger, Critical };

struct OwnShipInput {
  double x_m{0.0};
  double y_m{0.0};
  double heading_rad{0.0};
  double sog_mps{0.0};
  double loa_m{46.0};
  double confidence{1.0};
  bool odd_degraded{false};
};

struct TargetInput {
  std::string id;
  double x_m{0.0};
  double y_m{0.0};
  double cog_rad{0.0};
  double sog_mps{0.0};
  double cpa_m{0.0};
  double tcpa_s{0.0};
  double confidence{1.0};
};

struct DomainAxes {
  double forward_m{0.0};
  double astern_m{0.0};
  double starboard_m{0.0};
  double port_m{0.0};
};

struct DomainConfig {
  double superellipse_power{2.5};
  double warning_scale{1.8};
  double action_horizon_s{600.0};
  double emergency_horizon_s{180.0};
  double critical_horizon_s{60.0};
};

struct RiskVector {
  std::string target_id;
  double range_m{0.0};
  double relative_bearing_deg{0.0};
  double closing_speed_mps{0.0};
  double dcpa_m{0.0};
  double tcpa_s{0.0};
  double warning_margin_m{0.0};
  double danger_margin_m{0.0};
  double warning_ddv{0.0};
  double danger_ddv{0.0};
  double tdv_warning_s{0.0};
  double tdv_danger_s{0.0};
  double tde_warning_s{0.0};
  double tde_danger_s{0.0};
  ColregsDuty colregs_duty{ColregsDuty::Free};
  RiskPhase risk_phase{RiskPhase::Clear};
  double risk_score{0.0};
};

DomainAxes danger_axes(const OwnShipInput & own);
DomainAxes warning_axes(const OwnShipInput & own, const DomainConfig & config = {});
RiskVector evaluate_target(
  const OwnShipInput & own,
  const TargetInput & target,
  ColregsDuty duty,
  const DomainConfig & config = {});
const char * to_string(RiskPhase phase) noexcept;
const char * to_string(ColregsDuty duty) noexcept;

}  // namespace mass_l3::risk
