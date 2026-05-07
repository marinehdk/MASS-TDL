#ifndef MASS_L3_M5_COMMON_ERROR_HPP_
#define MASS_L3_M5_COMMON_ERROR_HPP_

// M5 Tactical Planner — Error codes and Result type
// PATH-D (MISRA C++:2023): no bare new/delete, no C-style casts,
// all integers use <cstdint> types, [[nodiscard]] on computed returns.
//
// Error codes occupy range 5000–5999 (M5 reservation per system error table).

#include <cstdint>
#include <string_view>
#include <variant>

namespace mass_l3::m5 {

// ---------------------------------------------------------------------------
// ErrorCode — M5-scoped error enumeration (range 5000–5999)
// ---------------------------------------------------------------------------
enum class ErrorCode : std::uint16_t {
  // 5000: Success / no-error sentinel
  kOk = 5000u,
  // 5001–5099: Input / message errors
  kInputStale       = 5001u,
  kInputOutOfRange  = 5002u,
  kInputMissing     = 5003u,
  kInputInvalidEnum = 5004u,
  // 5100–5199: Solver errors (Mid-MPC / BC-MPC)
  kSolverTimeout     = 5100u,
  kSolverInfeasible  = 5101u,
  kSolverNumerical   = 5102u,
  kSolverNotInit     = 5103u,
  // 5200–5299: Capability / Manifest errors
  kManifestMissing       = 5200u,
  kManifestInvalidField  = 5201u,
  kManifestVersionMismatch = 5202u,
  // 5300–5399: Dynamics / CPA errors
  kDynamicsStepNegativeDt = 5300u,
  kCpaUndefined           = 5301u,
  kTrajectoryEmpty        = 5302u,
  // 5900–5999: Internal / catch-all
  kInternalConsistencyFail = 5900u,
  kNotImplemented          = 5901u,
};

// ---------------------------------------------------------------------------
// to_string — compile-time label for logging / ASDR audit records
// [[nodiscard]] enforces intentional discard checks.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr std::string_view to_string(ErrorCode ec) noexcept {
  switch (ec) {
    case ErrorCode::kOk:                    return "Ok";
    case ErrorCode::kInputStale:            return "InputStale";
    case ErrorCode::kInputOutOfRange:       return "InputOutOfRange";
    case ErrorCode::kInputMissing:          return "InputMissing";
    case ErrorCode::kInputInvalidEnum:      return "InputInvalidEnum";
    case ErrorCode::kSolverTimeout:         return "SolverTimeout";
    case ErrorCode::kSolverInfeasible:      return "SolverInfeasible";
    case ErrorCode::kSolverNumerical:       return "SolverNumerical";
    case ErrorCode::kSolverNotInit:         return "SolverNotInit";
    case ErrorCode::kManifestMissing:       return "ManifestMissing";
    case ErrorCode::kManifestInvalidField:  return "ManifestInvalidField";
    case ErrorCode::kManifestVersionMismatch: return "ManifestVersionMismatch";
    case ErrorCode::kDynamicsStepNegativeDt: return "DynamicsStepNegativeDt";
    case ErrorCode::kCpaUndefined:          return "CpaUndefined";
    case ErrorCode::kTrajectoryEmpty:       return "TrajectoryEmpty";
    case ErrorCode::kInternalConsistencyFail: return "InternalConsistencyFail";
    case ErrorCode::kNotImplemented:        return "NotImplemented";
  }
  return "UnknownM5Error";
}

// ---------------------------------------------------------------------------
// M5Error — lightweight result wrapper (no exceptions; variant-based)
// Mirrors M7 NoExceptionResult pattern for consistency across PATH-D modules.
// ---------------------------------------------------------------------------
template <typename T>
class M5Result {
 public:
  [[nodiscard]] static M5Result ok(T value) noexcept {
    M5Result r;
    r.data_ = std::move(value);
    return r;
  }
  [[nodiscard]] static M5Result err(ErrorCode ec) noexcept {
    M5Result r;
    r.data_ = ec;
    return r;
  }
  [[nodiscard]] bool has_value() const noexcept {
    return std::holds_alternative<T>(data_);
  }
  [[nodiscard]] T const& value() const noexcept { return std::get<T>(data_); }
  [[nodiscard]] ErrorCode error() const noexcept {
    return std::get<ErrorCode>(data_);
  }

 private:
  M5Result() = default;
  std::variant<T, ErrorCode> data_{ErrorCode::kOk};
};

// Void specialisation — used for side-effect-only operations.
template <>
class M5Result<void> {
 public:
  [[nodiscard]] static M5Result ok() noexcept {
    M5Result r;
    r.ec_ = ErrorCode::kOk;
    return r;
  }
  [[nodiscard]] static M5Result err(ErrorCode ec) noexcept {
    M5Result r;
    r.ec_ = ec;
    return r;
  }
  [[nodiscard]] bool has_value() const noexcept {
    return ec_ == ErrorCode::kOk;
  }
  [[nodiscard]] ErrorCode error() const noexcept { return ec_; }

 private:
  ErrorCode ec_{ErrorCode::kOk};
};

// Convenience alias used by most M5 callers.
template <typename T>
using M5Error = M5Result<T>;

}  // namespace mass_l3::m5

#endif  // MASS_L3_M5_COMMON_ERROR_HPP_
