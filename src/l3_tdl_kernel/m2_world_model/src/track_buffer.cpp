#include "m2_world_model/track_buffer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mass_l3::m2 {

// ── Constructor ──────────────────────────────────────────────────────────────

TrackBuffer::TrackBuffer(Config cfg) : cfg_(cfg) {
  // Reserve space to avoid frequent re-hashing.
  buffer_.reserve(static_cast<size_t>(cfg_.max_targets));
}

// ── Public API ───────────────────────────────────────────────────────────────

void TrackBuffer::update(const l3_external_msgs::msg::TrackedTargetArray& msg,
                         std::chrono::steady_clock::time_point now) {
  std::lock_guard<std::mutex> lock(mutex_);

  for (const auto& tgt : msg.targets) {
    const uint64_t id = tgt.target_id;
    auto it = buffer_.find(id);

    if (it != buffer_.end()) {
      // Update existing entry.
      auto& entry = it->second;
      entry.classification_storage = tgt.classification;
      entry.snapshot = snapshot_from_msg(tgt, now, cfg_.position_default_sigma_m);
      entry.snapshot.classification = entry.classification_storage;
      entry.last_seen = now;
      entry.miss_count = 0;
    } else {
      // Insert new entry, evicting oldest if at capacity.
      if (buffer_.size() >= static_cast<size_t>(cfg_.max_targets)) {
        evict_oldest_locked();
      }
      TrackEntry entry;
      entry.classification_storage = tgt.classification;
      entry.snapshot = snapshot_from_msg(tgt, now, cfg_.position_default_sigma_m);
      entry.snapshot.classification = entry.classification_storage;
      entry.last_seen = now;
      entry.miss_count = 0;
      buffer_[id] = std::move(entry);
    }
  }
}

std::optional<TargetSnapshot> TrackBuffer::get(uint64_t target_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = buffer_.find(target_id);
  if (it == buffer_.end()) {
    return std::nullopt;
  }
  // Return a copy.  The string_view in the copy still points into this
  // TrackEntry's classification_storage, which is stable for the lifetime
  // of the entry in the map.
  return it->second.snapshot;
}

std::vector<TargetSnapshot> TrackBuffer::active_targets() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<TargetSnapshot> result;
  result.reserve(buffer_.size());
  for (const auto& [id, entry] : buffer_) {
    static_cast<void>(id);
    if (entry.miss_count < cfg_.disappearance_periods) {
      result.push_back(entry.snapshot);
    }
  }
  return result;
}

std::vector<TargetSnapshot> TrackBuffer::snapshot_aligned_to(
    std::chrono::steady_clock::time_point align_t) const {
  constexpr double kKnToMps = 0.514444;
  constexpr double kLatM = 111320.0;
  constexpr double kDegToRad = M_PI / 180.0;

  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<TargetSnapshot> result;
  result.reserve(buffer_.size());

  for (const auto& [id, entry] : buffer_) {
    static_cast<void>(id);
    if (entry.miss_count >= cfg_.disappearance_periods) {
      continue;
    }
    TargetSnapshot snap = entry.snapshot;
    const double dt_s =
        std::chrono::duration<double>(align_t - entry.last_seen).count();
    if (dt_s > 0.0) {
      const double speed_mps = snap.sog_kn * kKnToMps;
      const double cog_rad = snap.cog_deg * kDegToRad;
      const double dx_m = speed_mps * dt_s * std::sin(cog_rad);
      const double dy_m = speed_mps * dt_s * std::cos(cog_rad);
      snap.latitude_deg += dy_m / kLatM;
      const double lon_m_per_deg =
          kLatM * std::cos(snap.latitude_deg * kDegToRad);
      snap.longitude_deg +=
          (lon_m_per_deg > 1.0) ? dx_m / lon_m_per_deg : 0.0;
      snap.stamp = align_t;
    }
    result.push_back(snap);
  }
  return result;
}

int32_t TrackBuffer::active_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  int32_t count = 0;
  for (const auto& [id, entry] : buffer_) {
    static_cast<void>(id);
    if (entry.miss_count < cfg_.disappearance_periods) {
      ++count;
    }
  }
  return count;
}

void TrackBuffer::evict_stale(std::chrono::steady_clock::time_point now) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = buffer_.begin();
  while (it != buffer_.end()) {
    it->second.miss_count++;

    const auto age_s =
        std::chrono::duration<double>(now - it->second.last_seen).count();

    if (it->second.miss_count >= cfg_.disappearance_periods ||
        age_s >= cfg_.max_target_age_s) {
      it = buffer_.erase(it);
    } else {
      ++it;
    }
  }
}

size_t TrackBuffer::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return buffer_.size();
}

// ── Private helpers ──────────────────────────────────────────────────────────

void TrackBuffer::evict_oldest_locked() {
  // Linear scan to find the entry with the oldest last_seen time.
  // Ties are broken by smallest target_id to ensure deterministic eviction order.
  auto oldest_it = buffer_.begin();
  for (auto it = buffer_.begin(); it != buffer_.end(); ++it) {
    const bool older_time = it->second.last_seen < oldest_it->second.last_seen;
    const bool same_time  = it->second.last_seen == oldest_it->second.last_seen;
    const bool smaller_id = it->first < oldest_it->first;
    if (older_time || (same_time && smaller_id)) {
      oldest_it = it;
    }
  }
  buffer_.erase(oldest_it);
}

// static
TargetSnapshot TrackBuffer::snapshot_from_msg(
    const l3_msgs::msg::TrackedTarget& tgt,
    std::chrono::steady_clock::time_point now,
    double position_default_sigma_m) {
  constexpr double kLatM = 111320.0;
  constexpr double kDegToRad = M_PI / 180.0;

  TargetSnapshot snap;
  snap.target_id = tgt.target_id;
  snap.latitude_deg = tgt.position.latitude;
  snap.longitude_deg = tgt.position.longitude;
  snap.sog_kn = tgt.sog_kn;
  snap.cog_deg = tgt.cog_deg;
  snap.heading_deg = tgt.heading_deg;
  snap.classification_confidence = tgt.classification_confidence;
  snap.stamp = now;

  // Use per-target covariance from message (float64[9] row-major 3×3).
  // Fall back to default position σ when the message carries a zero matrix.
  // CpaTcpaCalculator reads covariance(0,0) as lat_var_deg² and (1,1) as lon_var_deg².
  const bool kCovValid = (tgt.covariance[0] > 0.0) || (tgt.covariance[4] > 0.0);
  if (kCovValid) {
    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < 3; ++col) {
        snap.covariance(row, col) =
            tgt.covariance[static_cast<size_t>(row * 3 + col)];
      }
    }
  } else {
    const double kLatSigmaDeg = position_default_sigma_m / kLatM;
    const double kLonMPerDeg = kLatM * std::cos(snap.latitude_deg * kDegToRad);
    const double kLonSigmaDeg = (kLonMPerDeg > 1.0)
        ? position_default_sigma_m / kLonMPerDeg : kLatSigmaDeg;
    snap.covariance.setZero();
    snap.covariance(0, 0) = kLatSigmaDeg * kLatSigmaDeg;
    snap.covariance(1, 1) = kLonSigmaDeg * kLonSigmaDeg;
    snap.covariance(2, 2) = 1.0;
  }

  // classification string_view is set by the caller after backing storage.
  return snap;
}

}  // namespace mass_l3::m2
