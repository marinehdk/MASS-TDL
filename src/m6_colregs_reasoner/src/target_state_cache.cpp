#include "m6_colregs_reasoner/target_state_cache.hpp"

namespace mass_l3::m6_colregs {

TargetStateCache::TargetStateCache(Config cfg) : cfg_(cfg) {}

void TargetStateCache::update(const std::vector<TargetGeometricState>& targets) {
  std::lock_guard<std::mutex> lock(mutex_);
  cache_.clear();
  for (const auto& t : targets) {
    if (static_cast<int32_t>(cache_.size()) < cfg_.max_targets) {
      cache_[t.target_id] = t;
    }
  }
}

std::optional<TargetGeometricState> TargetStateCache::get(uint64_t target_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = cache_.find(target_id);
  if (it != cache_.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::vector<TargetGeometricState> TargetStateCache::all_targets() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<TargetGeometricState> result;
  result.reserve(cache_.size());
  for (const auto& pair : cache_) {
    result.push_back(pair.second);
  }
  return result;
}

}  // namespace mass_l3::m6_colregs
