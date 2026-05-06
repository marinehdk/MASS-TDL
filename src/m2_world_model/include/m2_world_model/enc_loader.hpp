#pragma once

#include <optional>
#include <string>
#include <vector>

#include "m2_world_model/types.hpp"

namespace mass_l3::m2 {

/// Loads ENC static constraint data (from JSON metadata produced by Python tool).
class EncLoader final {
 public:
  struct Config {
    std::string enc_data_root;
    std::string enc_metadata_file;
  };

  explicit EncLoader(Config cfg);

  /// Load metadata from JSON file.
  bool load();

  /// Get zone constraints at given lat/lon (simplified bounding-box lookup).
  [[nodiscard]] std::optional<ZoneSnapshot>
  query_zone(double latitude_deg, double longitude_deg) const;

  /// Is this position inside a TSS (Traffic Separation Scheme)?
  [[nodiscard]] bool in_tss(double latitude_deg, double longitude_deg) const;

  /// Is this position in a narrow channel?
  [[nodiscard]] bool in_narrow_channel(double latitude_deg, double longitude_deg) const;

  bool loaded() const { return loaded_; }

 private:
  struct EncChart {
    std::string name;
    double lat_min, lat_max, lon_min, lon_max;
    bool has_tss;
    bool has_narrow_channel;
    double min_depth_m;
  };

  Config cfg_;
  std::vector<EncChart> charts_;
  bool loaded_ = false;
};

}  // namespace mass_l3::m2
