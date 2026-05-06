#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <Eigen/Core>

namespace mass_l3::m2 {

// ── Zone / ODD ──
enum class OddZone : uint8_t { A = 0, B = 1, C = 2, D = 3 };

// ── Health state per view ──
enum class ViewHealth : uint8_t { Full = 0, Degraded = 1, Critical = 2, Lost = 3 };

// ── View identifiers ──
enum class ViewId : uint8_t { DV = 0, EV = 1, SV = 2, Count = 3 };

// ── Coordinate snapshots ──
struct OwnShipSnapshot {
  double sog_kn;
  double cog_deg;
  double heading_deg;
  double u_water;           // m/s
  double v_water;           // m/s
  double current_speed_kn;
  double current_direction_deg;
  double latitude_deg;
  double longitude_deg;
  Eigen::Matrix<double, 6, 6> covariance;  // pos(3) x vel(3)
  std::chrono::steady_clock::time_point stamp;
};

struct TargetSnapshot {
  uint64_t target_id;
  double latitude_deg;
  double longitude_deg;
  double sog_kn;
  double cog_deg;
  double heading_deg;
  Eigen::Matrix<double, 3, 3> covariance;  // lat/lon/heading
  std::string_view classification;
  float classification_confidence;
  std::chrono::steady_clock::time_point stamp;
};

struct ZoneSnapshot {
  std::string zone_type;
  bool in_tss;
  bool in_narrow_channel;
  double min_water_depth_m;
  std::chrono::steady_clock::time_point stamp;
};

struct OddSnapshot {
  OddZone current_zone;
  uint8_t auto_level;
  uint8_t health;
  uint8_t envelope_state;
  float conformance_score;
  float tmr_s;
  float tdl_s;
  std::chrono::steady_clock::time_point stamp;
};

struct AggregatedHealth {
  double dv_confidence;  // [0,1]
  double ev_confidence;
  double sv_confidence;
  double aggregated;       // min of three
  ViewHealth dv_health; ViewHealth ev_health; ViewHealth sv_health;
};

// ── CPA / TCPA 结果 ──
struct CpaUncertainty {
  double cpa_sigma_m;
  double tcpa_sigma_s;
};

struct CpaResult {
  double cpa_m;
  double tcpa_s;
  CpaUncertainty uncertainty;
};

}  // namespace mass_l3::m2
