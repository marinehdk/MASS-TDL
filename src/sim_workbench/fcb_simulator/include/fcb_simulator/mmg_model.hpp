// SPDX-License-Identifier: Proprietary
// Yasukawa & Yoshimura 2015 [R7] 4-DOF MMG equations of motion.
#ifndef FCB_SIMULATOR_MMG_MODEL_HPP_
#define FCB_SIMULATOR_MMG_MODEL_HPP_

#include "fcb_simulator/types.hpp"

namespace fcb_sim {

// Compute time derivative dX/dt of the 8-state vector given current state,
// rudder angle delta (rad), propeller speed n (rev/s), and MMG parameters.
// Returns derivatives packed back into FcbState (members are dx/dt, etc.).
FcbState compute_derivatives(const FcbState& state,
                             double delta_rad,
                             double n_rps,
                             const MmgParams& p);

}  // namespace fcb_sim

#endif  // FCB_SIMULATOR_MMG_MODEL_HPP_
