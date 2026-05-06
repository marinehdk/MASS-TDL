#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

namespace mass_l3::m3 {

enum class MissionState : uint8_t {
  Init = 0,
  Idle = 1,
  TaskValidation = 2,
  AwaitingRoute = 3,
  Active = 4,
  ReplanWait = 5,
  MrcTransit = 6,
};

enum class ReplanReason : uint8_t {
  OddExit = 0,
  MissionInfeasible = 1,
  MrcRequired = 2,
  Congestion = 3,
};

struct EtaProjection {
  double eta_s;                          // seconds to destination
  double distance_remaining_nm;
  double uncertainty_s;                  // ETA uncertainty (from current/sea state)
  std::chrono::steady_clock::time_point computed_at;
};

struct MissionStats {
  double total_distance_nm;
  double distance_covered_nm;
  double progress_pct;
  int64_t elapsed_s;
};

}  // namespace mass_l3::m3
