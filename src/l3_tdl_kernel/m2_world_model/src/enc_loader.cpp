#include "m2_world_model/enc_loader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace mass_l3::m2 {

// ── Haversine distance helper (spherical Earth approximation) ──────────────
// Returns great-circle distance in metres between two WGS84 points.
static double haversine_m_(double lat1_deg, double lon1_deg,
                            double lat2_deg, double lon2_deg) {
  constexpr double kR = 6'371'000.0;      // Earth mean radius [m]
  constexpr double kD2R = M_PI / 180.0;

  const double dlat = (lat2_deg - lat1_deg) * kD2R;
  const double dlon = (lon2_deg - lon1_deg) * kD2R;
  const double a = std::sin(dlat / 2.0) * std::sin(dlat / 2.0) +
                   std::cos(lat1_deg * kD2R) * std::cos(lat2_deg * kD2R) *
                   std::sin(dlon / 2.0) * std::sin(dlon / 2.0);
  return kR * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

// ── Bounding-box overlaps circle? ──────────────────────────────────────────
// The horizon circle centred at (cx, cy) with radius r_m overlaps the
// axis-aligned bbox [lat_min, lat_max] x [lon_min, lon_max] if the closest
// point on the bbox to (cx, cy) is within r_m.
static bool bbox_overlaps_circle_(double cx, double cy, double r_m,
                                   double lat_min, double lat_max,
                                   double lon_min, double lon_max) {
  // Clamp centre to nearest point on bbox (in degrees), then compute
  // haversine distance from original centre to that clamped point.
  const double nx = std::max(lat_min, std::min(cx, lat_max));
  const double ny = std::max(lon_min, std::min(cy, lon_max));
  const double d = haversine_m_(cx, cy, nx, ny);
  return d <= r_m;
}

// ── Constructor ────────────────────────────────────────────────────────────

EncLoader::EncLoader(Config cfg) : cfg_(std::move(cfg)) {}

// ── Load metadata from JSON ────────────────────────────────────────────────

bool EncLoader::load() {
  try {
    std::ifstream file(cfg_.enc_metadata_file);
    if (!file.is_open()) {
      loaded_ = false;
      return false;
    }
    nlohmann::json root;
    file >> root;

    const auto& charts = root["charts"];
    if (!charts.is_array()) {
      loaded_ = false;
      return false;
    }

    charts_.clear();
    for (const auto& chart : charts) {
      EncChart ec;
      ec.name = chart.value("name", std::string{});
      ec.lat_min = chart.value("lat_min", 0.0);
      ec.lat_max = chart.value("lat_max", 0.0);
      ec.lon_min = chart.value("lon_min", 0.0);
      ec.lon_max = chart.value("lon_max", 0.0);
      ec.has_tss = chart.value("has_tss", false);
      ec.has_narrow_channel = chart.value("has_narrow_channel", false);
      ec.min_depth_m = chart.value("min_depth_m", 0.0);

      const auto& tss = chart["tss_lanes"];
      if (tss.is_array()) {
        ec.tss_lanes_json = tss.dump();
      }
      const auto& excl = chart["exclusion_zones"];
      if (excl.is_array()) {
        ec.exclusion_zones_json = excl.dump();
      }

      charts_.push_back(std::move(ec));
    }

    loaded_ = true;
    return true;
  } catch (const std::exception&) {
    loaded_ = false;
    return false;
  }
}

// ── Existing zone query ────────────────────────────────────────────────────

std::optional<ZoneSnapshot>
EncLoader::query_zone(double latitude_deg, double longitude_deg) const {
  if (!loaded_) {
    return std::nullopt;
  }

  for (const auto& chart : charts_) {
    if (latitude_deg >= chart.lat_min && latitude_deg <= chart.lat_max &&
        longitude_deg >= chart.lon_min && longitude_deg <= chart.lon_max) {
      ZoneSnapshot snap;
      if (chart.has_tss) {
        snap.zone_type = "tss";
      } else if (chart.has_narrow_channel) {
        snap.zone_type = "narrow_channel";
      } else {
        snap.zone_type = "open_water";
      }
      snap.in_tss = chart.has_tss;
      snap.in_narrow_channel = chart.has_narrow_channel;
      snap.min_water_depth_m = chart.min_depth_m;
      snap.stamp = std::chrono::steady_clock::now();
      return snap;
    }
  }

  return std::nullopt;
}

// ── Existing point-in-zone helpers ─────────────────────────────────────────

bool EncLoader::in_tss(double latitude_deg, double longitude_deg) const {
  auto zone = query_zone(latitude_deg, longitude_deg);
  return zone.has_value() && zone->in_tss;
}

bool EncLoader::in_narrow_channel(double latitude_deg,
                                   double longitude_deg) const {
  auto zone = query_zone(latitude_deg, longitude_deg);
  return zone.has_value() && zone->in_narrow_channel;
}

// ── D2.2 Track B: polygon query methods ────────────────────────────────────

std::vector<std::string>
EncLoader::get_tss_lanes(double latitude_deg, double longitude_deg) const {
  std::vector<std::string> result;
  if (!loaded_) {
    return result;
  }

  for (const auto& chart : charts_) {
    if (!chart.has_tss || chart.tss_lanes_json.empty()) {
      continue;
    }
    if (latitude_deg >= chart.lat_min && latitude_deg <= chart.lat_max &&
        longitude_deg >= chart.lon_min && longitude_deg <= chart.lon_max) {
      // tss_lanes_json is a JSON array of polygons; parse and yield each one.
      try {
        auto lanes = nlohmann::json::parse(chart.tss_lanes_json);
        if (lanes.is_array()) {
          for (const auto& lane : lanes) {
            result.push_back(lane.dump());
          }
        }
      } catch (const std::exception&) {
      }
    }
  }

  return result;
}

std::vector<std::string>
EncLoader::get_exclusion_zones(double latitude_deg, double longitude_deg,
                                double horizon_m) const {
  std::vector<std::string> result;
  if (!loaded_) {
    return result;
  }

  for (const auto& chart : charts_) {
    if (chart.exclusion_zones_json.empty()) {
      continue;
    }
    // Include chart only if its bbox overlaps the horizon circle
    if (!bbox_overlaps_circle_(latitude_deg, longitude_deg, horizon_m,
                                chart.lat_min, chart.lat_max,
                                chart.lon_min, chart.lon_max)) {
      continue;
    }
    try {
      auto zones = nlohmann::json::parse(chart.exclusion_zones_json);
      if (zones.is_array()) {
        for (const auto& zone : zones) {
          result.push_back(zone.dump());
        }
      }
    } catch (const std::exception&) {
    }
  }

  return result;
}

double EncLoader::get_min_depth(double latitude_deg,
                                 double longitude_deg) const {
  if (!loaded_) {
    return std::numeric_limits<double>::infinity();
  }

  double min_depth = std::numeric_limits<double>::infinity();
  for (const auto& chart : charts_) {
    if (latitude_deg >= chart.lat_min && latitude_deg <= chart.lat_max &&
        longitude_deg >= chart.lon_min && longitude_deg <= chart.lon_max) {
      if (chart.min_depth_m > 0.0 && chart.min_depth_m < min_depth) {
        min_depth = chart.min_depth_m;
      }
    }
  }
  return min_depth;
}

bool EncLoader::refresh_horizon(double, double, double) {
  return load();
}

bool EncLoader::refresh_path_bbox(double, double, double, double) {
  return load();
}

}  // namespace mass_l3::m2
