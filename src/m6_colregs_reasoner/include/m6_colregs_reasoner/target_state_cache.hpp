#pragma once

#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs {

class TargetStateCache {
 public:
  struct Config {
    int32_t max_targets;
  };

  explicit TargetStateCache(Config cfg);

  void update(const std::vector<TargetGeometricState>& targets);
  std::optional<TargetGeometricState> get(uint64_t target_id) const;
  std::vector<TargetGeometricState> all_targets() const;

  size_t size() const { return cache_.size(); }
  int32_t capacity() const { return cfg_.max_targets; }

 private:
  Config cfg_;
  std::unordered_map<uint64_t, TargetGeometricState> cache_;
  mutable std::mutex mutex_;
};

}  // namespace mass_l3::m6_colregs
