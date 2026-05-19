#pragma once

#include <vector>

#include "m4_behavior_arbiter/behavior_activation.hpp"
#include "m4_behavior_arbiter/behavior_dictionary.hpp"
#include "m4_behavior_arbiter/ivp_solver.hpp"

namespace mass_l3::m4 {

/**
 * @brief Priority arbitration rules applied AFTER IvP solving.
 *
 * Detailed design §5.2 + §7.1.3: although IvP avoids "winner-take-all" within
 * normal operation, certain meta-rules override IvP entirely:
 *   1. CRITICAL health → force MRC_DRIFT regardless of IvP (IEC 61508 SIL-2 path).
 *   2. Any MRC_* behavior active → freeze IvP, output the MRC behavior unchanged.
 *   3. ColregAvoid active → override mission behaviors.
 *
 * NORMAL path returns the first active behavior; this class only steps in for
 * safety overrides.
 */
class BehaviorPriority {
 public:
  /**
   * @brief Choose the primary behavior to publish this arbitration cycle.
   * @param active_set Active behaviors from BehaviorActivationCondition::compute_active_set().
   * @param ivp_solution Optimal IvP solution (used downstream for heading/speed; not used here).
   * @param inputs Arbitration inputs providing health and ODD envelope state.
   * @return Primary BehaviorType to execute this cycle.
   * @post Return value is always a valid BehaviorType (MrcDrift as safe fallback if active_set empty).
   */
  BehaviorType select_primary(const std::vector<BehaviorType>& active_set,
                              const IvPSolution& ivp_solution,
                              const ArbitrationInputs& inputs) const;

  /**
   * @brief Test whether any MRC behavior is present in the active set.
   * @param active_set Behaviors to search.
   * @return True if MrcDrift, MrcAnchor, or MrcHeaveTo is in active_set.
   */
  static bool has_mrc(const std::vector<BehaviorType>& active_set);
};

}  // namespace mass_l3::m4
