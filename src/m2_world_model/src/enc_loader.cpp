#include "m2_world_model/enc_loader.hpp"

#include <string>
#include <utility>

#include <yaml-cpp/yaml.h>

namespace mass_l3::m2 {

EncLoader::EncLoader(Config cfg) : cfg_(std::move(cfg)) {}

bool EncLoader::load() {
  try {
    YAML::Node root = YAML::LoadFile(cfg_.enc_metadata_file);
    const auto& charts = root["charts"];
    if (!charts || !charts.IsSequence()) {
      return false;
    }

    charts_.clear();
    for (const auto& chart : charts) {
      EncChart ec;
      ec.name = chart["name"].as<std::string>();
      ec.lat_min = chart["lat_min"].as<double>();
      ec.lat_max = chart["lat_max"].as<double>();
      ec.lon_min = chart["lon_min"].as<double>();
      ec.lon_max = chart["lon_max"].as<double>();
      ec.has_tss = chart["has_tss"].as<bool>(false);
      ec.has_narrow_channel = chart["has_narrow_channel"].as<bool>(false);
      ec.min_depth_m = chart["min_depth_m"].as<double>(0.0);
      charts_.push_back(std::move(ec));
    }

    loaded_ = true;
    return true;
  } catch (const std::exception&) {
    // File not found or parse error
    loaded_ = false;
    return false;
  }
}

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

  // Position not found in any chart → no zone constraints
  return std::nullopt;
}

bool EncLoader::in_tss(double latitude_deg, double longitude_deg) const {
  auto zone = query_zone(latitude_deg, longitude_deg);
  return zone.has_value() && zone->in_tss;
}

bool EncLoader::in_narrow_channel(double latitude_deg, double longitude_deg) const {
  auto zone = query_zone(latitude_deg, longitude_deg);
  return zone.has_value() && zone->in_narrow_channel;
}

}  // namespace mass_l3::m2
