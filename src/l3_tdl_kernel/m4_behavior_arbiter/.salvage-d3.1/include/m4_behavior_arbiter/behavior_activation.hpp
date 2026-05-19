#pragma once

#include <vector>

#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "l3_msgs/msg/mission_goal.hpp"
#include "l3_msgs/msg/mode_cmd.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "m4_behavior_arbiter/behavior_dictionary.hpp"

namespace mass_l3::m4 {

struct ArbitrationInputs {
  l3_msgs::msg::ODDState           odd_state;
  l3_msgs::msg::ModeCmd            mode_cmd;
  l3_msgs::msg::WorldState         world_state;
  l3_msgs::msg::MissionGoal        mission_goal;
  l3_msgs::msg::COLREGsConstraint  colregs_constraint;
  bool                             m1_fresh{false};
  bool                             m2_fresh{false};
  bool                             m3_fresh{false};
  bool                             m6_fresh{false};
  double                           cpa_safe_m{1852.0};  // [TBD-HAZID] from params
};

class BehaviorActivationCondition {
 public:
  /**
   * @brief Construct activation condition checker.
   * @param dict Loaded BehaviorDictionary; must outlive this object.
   * @pre dict.is_loaded() == true.
   */
  explicit BehaviorActivationCondition(const BehaviorDictionary& dict);

  /**
   * @brief Test whether a behavior is active under current inputs.
   * @param behavior The behavior to evaluate.
   * @param inputs Snapshot of all input streams at the current arbitration cycle.
   * @return true if all activation predicates pass for the given behavior.
   */
  bool is_active(BehaviorType behavior, const ArbitrationInputs& inputs) const;

  /**
   * @brief Compute the set of all currently active behaviors.
   * @param inputs Snapshot of all input streams.
   * @return Vector of active BehaviorType values; may be empty.
   */
  std::vector<BehaviorType> compute_active_set(const ArbitrationInputs& inputs) const;

 private:
  bool predicate_transit(const ArbitrationInputs& in) const;
  bool predicate_colreg_avoid(const ArbitrationInputs& in) const;
  bool predicate_dp_hold(const ArbitrationInputs& in) const;
  bool predicate_berth(const ArbitrationInputs& in) const;
  bool predicate_mrc(BehaviorType type, const ArbitrationInputs& in) const;

  const BehaviorDictionary& dict_;
};

}  // namespace mass_l3::m4
