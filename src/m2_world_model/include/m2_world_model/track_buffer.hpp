#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "l3_external_msgs/msg/tracked_target_array.hpp"
#include "m2_world_model/types.hpp"

namespace mass_l3::m2 {

class TrackBuffer final {
 public:
  struct Config {
    int32_t max_targets;                    // [TBD-HAZID] 256 — pre-allocation cap
    int32_t disappearance_periods;          // [TBD-HAZID] 10 — evict after N periods
    double max_target_age_s;                // [TBD-HAZID] 5.0 — max age before eviction
  };

  explicit TrackBuffer(Config cfg);
  ~TrackBuffer() = default;

  // Delete copy, keep move
  TrackBuffer(const TrackBuffer&) = delete;
  TrackBuffer& operator=(const TrackBuffer&) = delete;

  /// Upsert a batch of targets from Fusion TrackedTargetArray.
  void update(const l3_external_msgs::msg::TrackedTargetArray& msg,
              std::chrono::steady_clock::time_point now);

  /// Get latest snapshot for one target.
  [[nodiscard]] std::optional<TargetSnapshot> get(uint64_t target_id) const;

  /// Get all active targets within disappearance_periods.
  [[nodiscard]] std::vector<TargetSnapshot> active_targets() const;

  /// Evict targets that have exceeded disappearance_periods.
  void evict_stale(std::chrono::steady_clock::time_point now);

  /// Current buffer occupancy.
  [[nodiscard]] size_t size() const;

  /// Maximum capacity.
  [[nodiscard]] int32_t capacity() const { return cfg_.max_targets; }

 private:
  struct TrackEntry {
    TargetSnapshot snapshot;
    std::string classification_storage;  // backs the string_view in snapshot
    std::chrono::steady_clock::time_point last_seen;
    int32_t miss_count;  // consecutive periods without update
  };

  void evict_oldest_locked();

  static TargetSnapshot snapshot_from_msg(
      const l3_external_msgs::msg::TrackedTargetArray& msg,
      size_t index,
      std::chrono::steady_clock::time_point now);

  Config cfg_;
  std::unordered_map<uint64_t, TrackEntry> buffer_;
  mutable std::mutex mutex_;
};

}  // namespace mass_l3::m2
