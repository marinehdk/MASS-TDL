#include "m5_tactical_planner/avoidance_route_hash.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace mass_l3::m5 {
namespace {

void fnv1a_update(std::uint32_t& hash, const void* data, std::size_t size) {
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
}

void fnv1a_update_double(std::uint32_t& hash, double value) {
  static_assert(sizeof(double) == sizeof(std::uint64_t));
  std::uint64_t bits = 0u;
  std::memcpy(&bits, &value, sizeof(double));
  fnv1a_update(hash, &bits, sizeof(bits));
}

void fnv1a_update_string(std::uint32_t& hash, const std::string& value) {
  fnv1a_update(hash, value.data(), value.size());
  const char nul = '\0';
  fnv1a_update(hash, &nul, sizeof(nul));
}

}  // namespace

std::uint32_t avoidance_route_hash(const l3_msgs::msg::AvoidancePlan& plan) {
  std::uint32_t hash = 2166136261u;
  // plan_id is a per-publication message identifier for optimized routes; route_hash
  // represents execution route content/revision semantics and must stay stable when
  // unchanged geometry is republished with a fresh plan_id.
  fnv1a_update_string(hash, plan.parent_route_id);
  fnv1a_update_string(hash, plan.behavior_mode);
  fnv1a_update_string(hash, plan.command_source);
  for (const double value : plan.latitude) {
    fnv1a_update_double(hash, value);
  }
  for (const double value : plan.longitude) {
    fnv1a_update_double(hash, value);
  }
  for (const double value : plan.command_speed_mps) {
    fnv1a_update_double(hash, value);
  }
  for (const auto& value : plan.navigation_mode) {
    fnv1a_update_string(hash, value);
  }
  for (const auto value : plan.segment_source) {
    fnv1a_update(hash, &value, sizeof(value));
  }
  fnv1a_update(hash, &plan.allow_degraded_execution, sizeof(plan.allow_degraded_execution));
  fnv1a_update(hash, &plan.has_return_to_route_point, sizeof(plan.has_return_to_route_point));
  fnv1a_update_double(hash, plan.return_latitude);
  fnv1a_update_double(hash, plan.return_longitude);
  fnv1a_update(hash, &plan.nlp_solver_status, sizeof(plan.nlp_solver_status));
  fnv1a_update(hash, &plan.nlp_tail_gate_failed, sizeof(plan.nlp_tail_gate_failed));
  return hash;
}

}  // namespace mass_l3::m5
