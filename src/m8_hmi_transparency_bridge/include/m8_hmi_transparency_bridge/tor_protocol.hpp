// include/m8_hmi_transparency_bridge/tor_protocol.hpp
#ifndef MASS_L3_M8_TOR_PROTOCOL_HPP_
#define MASS_L3_M8_TOR_PROTOCOL_HPP_

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace mass_l3::m8 {

/// ToR state machine — v1.1.2 §12.4 + §12.4.1 IMO MASS Code C interaction verification
class TorProtocol final {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  enum class State : uint8_t {
    kIdle = 0,
    kRequested = 1,       // ToR request pushed, waiting for operator
    kAcknowledged = 2,    // Operator clicked "Acknowledge" — takeover authorized
    kTimeoutMrc = 3,      // 60s elapsed without acknowledgment → MRC preparation
  };

  enum class Reason : uint8_t {
    kOddExit = 0,
    kManualRequest = 1,
    kSafetyAlert = 2,
  };

  struct Config {
    double deadline_s{60.0};           // [R4] Veitch 2024 — [TBD-HAZID]
    double sat1_min_display_s{5.0};    // button enabled after this duration
    double retry_interval_s{30.0};     // [NOT-YET: retry scheduling deferred to Phase E2]
    int max_retries{1};                // [NOT-YET: retry scheduling deferred to Phase E2]
  };

  /// Snapshot written to ASDR when operator clicks Acknowledge
  struct AcknowledgmentSnapshot {
    TimePoint click_time{};
    double sat1_display_duration_s{0.0};
    std::vector<std::string> sat1_threats_visible{};
    std::string odd_zone_at_click{};
    float conformance_score_at_click{0.0F};
    std::string operator_id{};
  };

  explicit TorProtocol(Config cfg) noexcept : config_(cfg) {}

  /// Trigger a new ToR cycle. Returns true if entered REQUESTED; false if already in progress.
  [[nodiscard]] bool trigger(Reason reason, TimePoint now);

  /// True when SAT-1 has been displayed ≥ sat1_min_display_s (button may be enabled)
  [[nodiscard]] bool is_acknowledgment_button_enabled(TimePoint now) const;

  /// Operator clicked. Returns nullopt if button not enabled or state != REQUESTED.
  [[nodiscard]] std::optional<AcknowledgmentSnapshot> on_acknowledgment_clicked(
      TimePoint now,
      const std::vector<std::string>& current_sat1_threats,
      const std::string& current_odd_zone,
      float current_conformance_score,
      const std::string& operator_id);

  /// Periodic tick (call at ~2 Hz). Returns true when state just transitioned to kTimeoutMrc.
  [[nodiscard]] bool tick(TimePoint now);

  /// Reset to IDLE (called after takeover complete)
  void reset() noexcept;

  // Accessors
  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] Reason last_reason() const noexcept { return last_reason_; }
  [[nodiscard]] TimePoint requested_time() const noexcept { return requested_time_; }
  [[nodiscard]] double remaining_deadline_s(TimePoint now) const;

 private:
  Config config_;
  State state_{State::kIdle};
  Reason last_reason_{Reason::kOddExit};
  TimePoint requested_time_{};
  int retries_{0};
};

}  // namespace mass_l3::m8

#endif  // MASS_L3_M8_TOR_PROTOCOL_HPP_
