#ifndef M7_SAFETY_SUPERVISOR_COMMON_ERROR_HPP_
#define M7_SAFETY_SUPERVISOR_COMMON_ERROR_HPP_

#include <cstdint>
#include <string_view>
#include <variant>

namespace mass_l3::m7::common {

enum class ErrorCode : std::uint8_t {
  kOk = 0,
  kInputStale,
  kInputOutOfRange,
  kInputInvalidEnum,
  kHeartbeatLost,
  kInternalConsistencyFail,
  kArbitratorOverflow,
  kSlidingWindowUnderflow,
  kMrmUnavailable,
  kSelfFault
};

[[nodiscard]] constexpr std::string_view to_string(ErrorCode ec) noexcept {
  switch (ec) {
    case ErrorCode::kOk:                      return "Ok";
    case ErrorCode::kInputStale:              return "InputStale";
    case ErrorCode::kInputOutOfRange:         return "InputOutOfRange";
    case ErrorCode::kInputInvalidEnum:        return "InputInvalidEnum";
    case ErrorCode::kHeartbeatLost:           return "HeartbeatLost";
    case ErrorCode::kInternalConsistencyFail: return "InternalConsistencyFail";
    case ErrorCode::kArbitratorOverflow:      return "ArbitratorOverflow";
    case ErrorCode::kSlidingWindowUnderflow:  return "SlidingWindowUnderflow";
    case ErrorCode::kMrmUnavailable:          return "MrmUnavailable";
    case ErrorCode::kSelfFault:               return "SelfFault";
  }
  return "Unknown";
}

template <typename T>
class NoExceptionResult {
public:
  [[nodiscard]] static NoExceptionResult ok(T value) noexcept {
    NoExceptionResult r;
    r.data_ = std::move(value);
    return r;
  }
  [[nodiscard]] static NoExceptionResult err(ErrorCode ec) noexcept {
    NoExceptionResult r;
    r.data_ = ec;
    return r;
  }
  [[nodiscard]] bool has_value() const noexcept {
    return std::holds_alternative<T>(data_);
  }
  [[nodiscard]] T const& value() const noexcept {
    return std::get<T>(data_);
  }
  [[nodiscard]] ErrorCode error() const noexcept {
    return std::get<ErrorCode>(data_);
  }
private:
  NoExceptionResult() = default;
  std::variant<T, ErrorCode> data_{ErrorCode::kOk};
};

template <>
class NoExceptionResult<void> {
public:
  [[nodiscard]] static NoExceptionResult ok() noexcept {
    NoExceptionResult r;
    r.ec_ = ErrorCode::kOk;
    return r;
  }
  [[nodiscard]] static NoExceptionResult err(ErrorCode ec) noexcept {
    NoExceptionResult r;
    r.ec_ = ec;
    return r;
  }
  [[nodiscard]] bool has_value() const noexcept { return ec_ == ErrorCode::kOk; }
  [[nodiscard]] ErrorCode error() const noexcept { return ec_; }
private:
  ErrorCode ec_{ErrorCode::kOk};
};

}  // namespace mass_l3::m7::common

#endif  // M7_SAFETY_SUPERVISOR_COMMON_ERROR_HPP_
