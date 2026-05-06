#include "m2_world_model/track_buffer.hpp"

#include <algorithm>
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

  for (size_t i = 0; i < msg.target_ids.size(); ++i) {
    const uint64_t id = msg.target_ids[i];
    auto it = buffer_.find(id);

    if (it != buffer_.end()) {
      // Update existing entry.
      auto& entry = it->second;
      entry.classification_storage = msg.classifications[i];
      entry.snapshot = snapshot_from_msg(msg, i, now);
      entry.snapshot.classification = entry.classification_storage;
      entry.last_seen = now;
      entry.miss_count = 0;
    } else {
      // Insert new entry, evicting oldest if at capacity.
      if (buffer_.size() >= static_cast<size_t>(cfg_.max_targets)) {
        evict_oldest_locked();
      }
      TrackEntry entry;
      entry.classification_storage = msg.classifications[i];
      entry.snapshot = snapshot_from_msg(msg, i, now);
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
  auto oldest_it = buffer_.begin();
  for (auto it = buffer_.begin(); it != buffer_.end(); ++it) {
    if (it->second.last_seen < oldest_it->second.last_seen) {
      oldest_it = it;
    }
  }
  buffer_.erase(oldest_it);
}

// static
TrackBuffer::TargetSnapshot TrackBuffer::snapshot_from_msg(
    const l3_external_msgs::msg::TrackedTargetArray& msg,
    size_t index,
    std::chrono::steady_clock::time_point now) {
  TargetSnapshot snap;
  snap.target_id = msg.target_ids[index];
  snap.latitude_deg = msg.positions[index].latitude;
  snap.longitude_deg = msg.positions[index].longitude;
  snap.sog_kn = msg.sog_kn[index];
  snap.cog_deg = msg.cog_deg[index];
  snap.heading_deg = msg.heading_deg[index];
  snap.classification_confidence = msg.classification_confidences[index];
  snap.stamp = now;
  snap.covariance.setZero();
  // classification string_view is set by the caller after backing storage.
  return snap;
}

}  // namespace mass_l3::m2
