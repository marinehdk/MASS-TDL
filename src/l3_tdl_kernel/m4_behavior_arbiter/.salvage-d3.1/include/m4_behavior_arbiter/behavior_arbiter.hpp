#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "m4_behavior_arbiter/asdr_logger.hpp"
#include "m4_behavior_arbiter/behavior_activation.hpp"
#include "m4_behavior_arbiter/behavior_dictionary.hpp"
#include "m4_behavior_arbiter/behavior_priority.hpp"
#include "m4_behavior_arbiter/ivp_combine.hpp"
#include "m4_behavior_arbiter/ivp_domain.hpp"
#include "m4_behavior_arbiter/ivp_solver.hpp"
#include "m4_behavior_arbiter/types.hpp"

namespace mass_l3::m4 {

/**
 * @brief Pure-logic arbitration result (no ROS2 dependency).
 */
struct ArbitrationResult {
  BehaviorType primary{BehaviorType::MrcDrift};
  double heading_min_deg{0.0};
  double heading_max_deg{359.0};  // [TBD-HAZID] default full-range
  double speed_min_kn{0.0};
  double speed_max_kn{15.0};     // [TBD-HAZID] default max from ODD-A
  double confidence{1.0};
  std::string rationale{};
};

/**
 * @brief Pure-logic arbitration engine. No ROS2 dependency.
 *
 * Orchestrates: BehaviorActivationCondition → IvPSolver → BehaviorPriority.
 * Phase E1: IvP functions are stubs (empty weighted_fns → nullopt from solver →
 * full-range heading/speed defaults). IvP utility construction is Phase E2+.
 */
class BehaviorArbiter {
 public:
  /**
   * @brief Construct arbiter with all sub-components.
   * @param dict Loaded BehaviorDictionary; must outlive this object.
   * @param h_domain IvP heading discretization.
   * @param s_domain IvP speed discretization.
   * @param ivp_timeout Solver timeout; fires after this duration.
   * @pre dict.is_loaded() == true.
   * @throws std::invalid_argument if strategy is null (forwarded from IvPSolver).
   */
  BehaviorArbiter(const BehaviorDictionary& dict,
                  IvPHeadingDomain h_domain,
                  IvPSpeedDomain s_domain,
                  std::chrono::milliseconds ivp_timeout);

  /**
   * @brief Run one arbitration cycle.
   * @param inputs Snapshot of all input streams at this cycle.
   * @return ArbitrationResult with primary behavior and heading/speed interval.
   * @post result.primary is always valid (MrcDrift as safe fallback).
   */
  ArbitrationResult run(const ArbitrationInputs& inputs) const;

 private:
  /**
   * @brief Build IvP hard constraints from inputs (Phase E1: speed bounds only).
   * @param inputs Current arbitration inputs.
   * @return Constraints with default speed bounds; heading unconstrained.
   */
  IvPHardConstraints build_constraints(const ArbitrationInputs& inputs) const;

  /**
   * @brief Build SAT-2 rationale string listing active behaviors.
   * @param active Active behavior set for this cycle.
   * @return Rationale string in format "active:{...} IvP:stub(E1)".
   */
  std::string build_rationale(const std::vector<BehaviorType>& active) const;

  BehaviorActivationCondition activation_;
  IvPSolver solver_;
  BehaviorPriority priority_;
  AsdrLogger asdr_logger_;
  // Mutable cache: run() is logically const (pure arbitration) but must
  // track the previous primary to emit ASDR behavior-change records.
  mutable BehaviorType prev_primary_{BehaviorType::MrcDrift};
};

}  // namespace mass_l3::m4
