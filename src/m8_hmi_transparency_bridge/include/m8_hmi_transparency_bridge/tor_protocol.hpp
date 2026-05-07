#ifndef MASS_L3_M8_TOR_PROTOCOL_HPP_
#define MASS_L3_M8_TOR_PROTOCOL_HPP_

#include <chrono>
#include <cstdint>
#include <string>

namespace mass_l3::m8 {

class TorProtocol final {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  enum class State : uint8_t {
    kIdle = 0,
    kPending = 1,
    kAcknowledged = 2,
    kExpired = 3,
  };

  struct Config {
    double deadline_s{60.0};
    double sat1_min_display_s{5.0};
    double retry_interval_s{30.0};
    int max_retries{1};
  };

  explicit TorProtocol(const Config& config = {});
  ~TorProtocol() = default;
  TorProtocol(const TorProtocol&) = delete;
  TorProtocol& operator=(const TorProtocol&) = delete;

  void issue(const std::string& reason, TimePoint now);
  void acknowledge(TimePoint now);
  void tick(TimePoint now);
  [[nodiscard]] State state() const;
  [[nodiscard]] double remaining_s(TimePoint now) const;
  [[nodiscard]] int retry_count() const;

 private:
  Config config_;
  State state_{State::kIdle};
  TimePoint issued_at_{};
  TimePoint last_retry_at_{};
  int retry_count_{0};
};

}  // namespace mass_l3::m8

#endif  // MASS_L3_M8_TOR_PROTOCOL_HPP_
