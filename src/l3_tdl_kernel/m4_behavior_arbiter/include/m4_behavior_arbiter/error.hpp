#pragma once

/// @file error.hpp
/// @brief M4 Behavior Arbiter — module-level error codes.
///
/// These codes are distinct from the IvP-level ErrorCode defined in
/// ivp_function.hpp and cover dictionary loading, arbitration, and
/// active-behaviour selection failures.

#include <cstdint>

namespace mass_l3::m4 {

/// Module-level error / status codes for M4 Behavior Arbiter.
enum class M4ErrorCode : int32_t {
  kOk = 0,
  kNoActiveBehavior = 4001,
  kIvPInfeasible = 4002,
  kDictionaryLoadFailed = 4003,
  kPriorityConflict = 4004,
};

}  // namespace mass_l3::m4
