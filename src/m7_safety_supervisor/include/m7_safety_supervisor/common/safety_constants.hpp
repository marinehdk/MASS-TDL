#ifndef M7_SAFETY_SUPERVISOR_COMMON_SAFETY_CONSTANTS_HPP_
#define M7_SAFETY_SUPERVISOR_COMMON_SAFETY_CONSTANTS_HPP_

#include <cstdint>

namespace mass_l3::m7::common {

// CCS i-Ship §9 + v1.1.2 §11 — fundamental safety thresholds.
// All [TBD-HAZID] values are defaults; production values injected via m7_params.yaml
// after RUN-001 calibration (HAZID §11, scheduled 2026-05-13 kickoff).

constexpr double kDefaultFusionConfidenceLow{0.5};          // [TBD-HAZID-SOTIF-001]
constexpr double kDefaultMotionRmseThresholdM{50.0};        // [TBD-HAZID-SOTIF-002]
constexpr double kDefaultMaxBlindZoneFraction{0.20};        // [TBD-HAZID-SOTIF-003]
constexpr std::uint32_t kDefaultColregsFailureCount{3};     // [TBD-HAZID-SOTIF-004]
constexpr double kDefaultRttThresholdS{2.0};                // [TBD-HAZID-SOTIF-005]
constexpr double kDefaultPacketLossPctThreshold{20.0};      // [TBD-HAZID-SOTIF-006]
constexpr double kCheckerVetoRateThreshold{0.20};           // RFC-003 LOCKED — do not change
constexpr std::uint32_t kSlidingWindowCapacity{100};        // RFC-003 LOCKED — do not change
constexpr double kColregsConfidenceFailThreshold{0.3};      // [TBD-HAZID]

}  // namespace mass_l3::m7::common

#endif  // M7_SAFETY_SUPERVISOR_COMMON_SAFETY_CONSTANTS_HPP_
