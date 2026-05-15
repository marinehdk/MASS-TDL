#pragma once

/// @file behavior_activation.hpp
/// @brief M4 Behavior Arbiter — behavior activation logic.
///
/// Determines which behaviors from the BehaviorDictionary are applicable
/// given the current system state (ODD, world, COLREGs, mode, mission).

#include <vector>
#include <string>

#include "m4_behavior_arbiter/behavior_dictionary.hpp"
#include "m4_behavior_arbiter/types.hpp"

namespace mass_l3::m4 {

/// @brief Aggregated input snapshot consumed by the arbitration logic.
struct ArbitrationInputs {
  ODDStateMsg            odd_state;
  WorldStateMsg          world_state;
  COLREGsConstraintMsg   colregs_constraint;
  ModeCmdMsg             mode_cmd;
  MissionGoalMsg         mission_goal;
};

/// @brief Static activation predicates for each behaviour.
///
/// The activation model uses the dictionary's ODD/mode pre-filter
/// (get_active_subset) then applies behaviour-specific predicates here.
class BehaviorActivationCondition {
public:
  /// @brief Compute the current set of applicable behaviours.
  /// @param inputs    Arbitration input snapshot.
  /// @param dictionary Loaded behaviour dictionary.
  /// @return Subset of dictionary behaviours that pass both ODD/mode and
  ///         behaviour-specific activation predicates.
  static std::vector<BehaviorDescriptor> compute_active_set(
      const ArbitrationInputs& inputs,
      const BehaviorDictionary& dictionary);

  /// @brief Transit applicable when envelope IN or EDGE.
  static bool is_transit_applicable(const ArbitrationInputs& inputs);

  /// @brief COLREG avoid applicable when COLREGs constraint has active rules
  ///        AND conflict_detected is true.
  static bool is_colreg_avoid_applicable(const ArbitrationInputs& inputs);
};

}  // namespace mass_l3::m4
