#include "m8_hmi_transparency_bridge/sat_aggregator.hpp"

#include <limits>

namespace mass_l3::m8 {

// ---------------------------------------------------------------------------
// ingest
// ---------------------------------------------------------------------------

void SatAggregator::ingest(const l3_msgs::msg::SATData& msg,
                            TimePoint receive_time)
{
  auto maybe_src = from_string(msg.source_module);
  if (!maybe_src.has_value()) {
    return;
  }
  std::lock_guard<std::mutex> lock{mutex_};
  auto& entry = cache_[*maybe_src];
  entry.sat1 = msg.sat1;
  entry.sat2 = msg.sat2;
  entry.sat3 = msg.sat3;
  entry.last_receive_time = receive_time;
  entry.has_data = true;
}

// ---------------------------------------------------------------------------
// latest_sat1 / latest_sat2 / latest_sat3
// ---------------------------------------------------------------------------

std::optional<l3_msgs::msg::SAT1Data>
SatAggregator::latest_sat1(SourceModule src) const
{
  std::lock_guard<std::mutex> lock{mutex_};
  auto it = cache_.find(src);
  if (it == cache_.end() || !it->second.has_data) {
    return std::nullopt;
  }
  return it->second.sat1;
}

std::optional<l3_msgs::msg::SAT2Data>
SatAggregator::latest_sat2(SourceModule src) const
{
  std::lock_guard<std::mutex> lock{mutex_};
  auto it = cache_.find(src);
  if (it == cache_.end() || !it->second.has_data) {
    return std::nullopt;
  }
  return it->second.sat2;
}

std::optional<l3_msgs::msg::SAT3Data>
SatAggregator::latest_sat3(SourceModule src) const
{
  std::lock_guard<std::mutex> lock{mutex_};
  auto it = cache_.find(src);
  if (it == cache_.end() || !it->second.has_data) {
    return std::nullopt;
  }
  return it->second.sat3;
}

// ---------------------------------------------------------------------------
// age_seconds / is_stale
// ---------------------------------------------------------------------------

double SatAggregator::age_seconds(SourceModule src, TimePoint now) const
{
  std::lock_guard<std::mutex> lock{mutex_};
  auto it = cache_.find(src);
  if (it == cache_.end() || !it->second.has_data) {
    return std::numeric_limits<double>::infinity();
  }
  return std::chrono::duration<double>(now - it->second.last_receive_time).count();
}

bool SatAggregator::is_stale(SourceModule src, TimePoint now,
                              double stale_threshold_s) const
{
  return age_seconds(src, now) > stale_threshold_s;
}

// ---------------------------------------------------------------------------
// to_string / from_string
// ---------------------------------------------------------------------------

std::string SatAggregator::to_string(SourceModule src)
{
  switch (src) {
    case SourceModule::kM1: return "M1";
    case SourceModule::kM2: return "M2";
    case SourceModule::kM4: return "M4";
    case SourceModule::kM6: return "M6";
    case SourceModule::kM7: return "M7";
    default:                return "UNKNOWN";
  }
}

std::optional<SatAggregator::SourceModule>
SatAggregator::from_string(const std::string& name)
{
  if (name == "M1") return SourceModule::kM1;
  if (name == "M2") return SourceModule::kM2;
  if (name == "M4") return SourceModule::kM4;
  if (name == "M6") return SourceModule::kM6;
  if (name == "M7") return SourceModule::kM7;
  return std::nullopt;
}

}  // namespace mass_l3::m8
