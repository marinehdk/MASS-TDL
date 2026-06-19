#pragma once

#include <cstddef>
#include <deque>

namespace mass_l3::m2 {

struct TargetComplianceSample {
  double t_s{0.0};
  double cpa_m{0.0};
  double tcpa_s{0.0};
  double range_m{0.0};
  double heading_deg{0.0};
};

class WoernerComplianceScorer final {
 public:
  explicit WoernerComplianceScorer(double history_window_s);

  void add_sample(const TargetComplianceSample& sample);

  [[nodiscard]] double score() const;
  [[nodiscard]] std::size_t sample_count() const noexcept;

 private:
  void prune_();

  double history_window_s_;
  std::deque<TargetComplianceSample> history_;
};

}  // namespace mass_l3::m2
