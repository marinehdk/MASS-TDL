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
    double dynamic_horizon_nm{5.0};
    double cpa_safe_multiplier{3.0};
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

  // ── D2.2 Track B: polygon query methods ──

  /// Return TSS lane polygon JSON strings for charts whose bbox contains (lat,lon).
  [[nodiscard]] std::vector<std::string>
  get_tss_lanes(double latitude_deg, double longitude_deg) const;

  /// Return exclusion-zone polygon JSON strings for charts whose bbox
  /// overlaps the horizon circle centred at (lat,lon) with radius horizon_m.
  [[nodiscard]] std::vector<std::string>
  get_exclusion_zones(double latitude_deg, double longitude_deg,
                      double horizon_m) const;

  /// Return minimum water depth (m) across all charts covering (lat,lon).
  /// Returns +infinity if no chart covers the position.
  [[nodiscard]] double get_min_depth(double latitude_deg, double longitude_deg) const;

  /// Re-load metadata and update internal horizon. Returns true on success.
  bool refresh_horizon(double latitude_deg, double longitude_deg,
                       double horizon_m);

  /// Re-load metadata for a given bounding-box region. Returns true on success.
  bool refresh_path_bbox(double lat_min, double lat_max,
                         double lon_min, double lon_max);

  /// Update dynamic horizon value at run time.
  void set_dynamic_horizon_nm(double nm) { cfg_.dynamic_horizon_nm = nm; }

  bool loaded() const { return loaded_; }

 private:
  struct EncChart {
    std::string name;
    double lat_min, lat_max, lon_min, lon_max;
    bool has_tss;
    bool has_narrow_channel;
    double min_depth_m;
    std::string tss_lanes_json;
    std::string exclusion_zones_json;
  };

  Config cfg_;
  std::vector<EncChart> charts_;
  bool loaded_ = false;
};

}  // namespace mass_l3::m2
