// include/m8_hmi_transparency_bridge/sat_aggregator.hpp
#ifndef MASS_L3_M8_SAT_AGGREGATOR_HPP_
#define MASS_L3_M8_SAT_AGGREGATOR_HPP_

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "l3_msgs/msg/sat_data.hpp"
#include "l3_msgs/msg/sat1_data.hpp"
#include "l3_msgs/msg/sat2_data.hpp"
#include "l3_msgs/msg/sat3_data.hpp"

namespace mass_l3::m8 {

/// 多源 SAT_DataMsg 聚合器
class SatAggregator final {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  enum class SourceModule : uint8_t {
    kM1 = 0,
    kM2 = 1,
    kM4 = 2,
    kM6 = 3,
    kM7 = 4,
    kCount = 5
  };

  SatAggregator() = default;
  ~SatAggregator() = default;
  SatAggregator(const SatAggregator&) = delete;
  SatAggregator& operator=(const SatAggregator&) = delete;

  void ingest(const l3_msgs::msg::SATData& msg, TimePoint receive_time);

  std::optional<l3_msgs::msg::SAT1Data> latest_sat1(SourceModule src) const;
  std::optional<l3_msgs::msg::SAT2Data> latest_sat2(SourceModule src) const;
  std::optional<l3_msgs::msg::SAT3Data> latest_sat3(SourceModule src) const;

  double age_seconds(SourceModule src, TimePoint now) const;
  bool is_stale(SourceModule src, TimePoint now, double stale_threshold_s) const;

  static std::string to_string(SourceModule src);
  static std::optional<SourceModule> from_string(const std::string& name);

 private:
  struct PerSourceCache {
    l3_msgs::msg::SAT1Data sat1{};
    l3_msgs::msg::SAT2Data sat2{};
    l3_msgs::msg::SAT3Data sat3{};
    TimePoint last_receive_time{};
    bool has_data{false};
  };

  mutable std::mutex mutex_;
  std::map<SourceModule, PerSourceCache> cache_{};
};

}  // namespace mass_l3::m8

#endif  // MASS_L3_M8_SAT_AGGREGATOR_HPP_
