#ifndef M7_SAFETY_SUPERVISOR_CORE_MRM_CHAIN_EXECUTOR_HPP_
#define M7_SAFETY_SUPERVISOR_CORE_MRM_CHAIN_EXECUTOR_HPP_

#include "l3_msgs/msg/safety_alert.hpp"
#include "m7_safety_supervisor/core/hard_constraint_cpa.hpp"
#include "m7_safety_supervisor/core/hard_constraint_colregs.hpp"
#include "m7_safety_supervisor/core/hard_constraint_speed.hpp"
#include "m7_safety_supervisor/core/hard_constraint_rot.hpp"
#include "m7_safety_supervisor/core/hard_constraint_watchdog.hpp"
#include "m7_safety_supervisor/core/hard_constraint_dc.hpp"

namespace mass_l3::m7::core {

// Build Safety_AlertMsg from 6 hard constraint results.
// Priority (highest first): multi-watchdog > DC fail > CPA > COLREGs > ROT > speed > single-watchdog
// Maps violations to MRM commands per spec §8.3 field assignment rules.
l3_msgs::msg::SafetyAlert build_safety_alert_from_hard_constraints(
    CpaConsistencyResult const& cpa,
    ColregsGeometryResult const& colregs,
    SpeedLimitResult const& speed,
    RotLimitResult const& rot,
    WatchdogConstraintResult const& wd,
    DcConstraintResult const& dc) noexcept;

}  // namespace mass_l3::m7::core

#endif
