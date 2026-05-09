// SPDX-License-Identifier: Proprietary
// 4th-order Runge-Kutta integrator for the FCB MMG model.
#ifndef FCB_SIMULATOR_RK4_INTEGRATOR_HPP_
#define FCB_SIMULATOR_RK4_INTEGRATOR_HPP_

#include "fcb_simulator/types.hpp"

namespace fcb_sim {

FcbState rk4_step(const FcbState& state,
                  double delta_rad,
                  double n_rps,
                  const MmgParams& params,
                  double dt);

}  // namespace fcb_sim

#endif  // FCB_SIMULATOR_RK4_INTEGRATOR_HPP_
