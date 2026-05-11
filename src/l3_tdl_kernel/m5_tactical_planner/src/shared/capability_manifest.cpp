#include "m5_tactical_planner/shared/capability_manifest.hpp"

#include <stdexcept>
#include <string>

#include <yaml-cpp/yaml.h>

#include "m5_tactical_planner/common/units.hpp"

namespace mass_l3::m5::shared {

// ---------------------------------------------------------------------------
// Helper: read a scalar from YAML node with a default fallback.
// Returns the YAML value if present, otherwise the default.
// ---------------------------------------------------------------------------
namespace {

template <typename T>
[[nodiscard]] T yaml_get(const YAML::Node& node, const std::string& key, T default_val) {
  if (node[key] && !node[key].IsNull()) {
    return node[key].as<T>();
  }
  return default_val;
}

// ---------------------------------------------------------------------------
// Section loaders — each handles one YAML sub-node, mutates cfg in place.
// Extracted to satisfy PATH-D ≤60-line / CC≤10 constraints on load_from_yaml().
// ---------------------------------------------------------------------------

static void load_geometry(const YAML::Node& v, CapabilityManifest::Config& cfg) {
  const YAML::Node& geo = v["geometry"];
  if (!geo || geo.IsNull()) { return; }
  cfg.length_m = yaml_get<double>(geo, "length_m", cfg.length_m);
  cfg.beam_m   = yaml_get<double>(geo, "beam_m",   cfg.beam_m);
  cfg.draft_m  = yaml_get<double>(geo, "draft_m",  cfg.draft_m);
  cfg.mass_kg  = yaml_get<double>(geo, "mass_kg",  cfg.mass_kg);
}

static void load_maneuvering(const YAML::Node& v, CapabilityManifest::Config& cfg) {
  const YAML::Node& man = v["maneuvering"];
  if (!man || man.IsNull()) { return; }
  cfg.rot_max_at_18kn_rad_s =
      yaml_get<double>(man, "rot_max_at_18kn_rad_s", cfg.rot_max_at_18kn_rad_s);
  cfg.low_speed_kn          = yaml_get<double>(man, "low_speed_kn",     cfg.low_speed_kn);
  cfg.low_speed_factor      = yaml_get<double>(man, "low_speed_factor", cfg.low_speed_factor);
  cfg.high_speed_kn         = yaml_get<double>(man, "high_speed_kn",    cfg.high_speed_kn);
  cfg.high_speed_factor     = yaml_get<double>(man, "high_speed_factor",cfg.high_speed_factor);
  cfg.rough_sea_factor_per_hs =
      yaml_get<double>(man, "rough_sea_factor_per_hs", cfg.rough_sea_factor_per_hs);
}

static void load_braking(const YAML::Node& v, CapabilityManifest::Config& cfg) {
  const YAML::Node& brk = v["braking"];
  if (!brk || brk.IsNull()) { return; }
  cfg.stopping_distance_at_18kn_m =
      yaml_get<double>(brk, "stopping_distance_at_18kn_m",
                       cfg.stopping_distance_at_18kn_m);
  cfg.crash_stop_max_decel_mps2 =
      yaml_get<double>(brk, "crash_stop_max_decel_mps2",
                       cfg.crash_stop_max_decel_mps2);
}

static void load_speed(const YAML::Node& v, CapabilityManifest::Config& cfg) {
  const YAML::Node& spd = v["speed"];
  if (!spd || spd.IsNull()) { return; }
  cfg.speed_min_kn     = yaml_get<double>(spd, "speed_min_kn",     cfg.speed_min_kn);
  cfg.speed_max_kn     = yaml_get<double>(spd, "speed_max_kn",     cfg.speed_max_kn);
  cfg.service_speed_kn = yaml_get<double>(spd, "service_speed_kn", cfg.service_speed_kn);
}

static void load_mmg(const YAML::Node& v, CapabilityManifest::Config& cfg) {
  const YAML::Node& mmg = v["mmg_coefficients"];
  if (!mmg || mmg.IsNull()) { return; }
  cfg.surge_added_mass_factor =
      yaml_get<double>(mmg, "surge_added_mass_factor", cfg.surge_added_mass_factor);
  cfg.sway_added_mass_factor =
      yaml_get<double>(mmg, "sway_added_mass_factor", cfg.sway_added_mass_factor);
  cfg.yaw_added_inertia_factor =
      yaml_get<double>(mmg, "yaw_added_inertia_factor", cfg.yaw_added_inertia_factor);
}

}  // namespace

// ---------------------------------------------------------------------------
// load_from_yaml() — Backseat Driver pattern factory
// The entire body is wrapped in a single try/catch so that both file-level
// YAML::Exception (from LoadFile) and field-level YAML::Exception (from .as<T>()
// type mismatch) are caught and re-thrown as std::runtime_error.
// Body ≤ 25 lines, CC ≤ 5 (PATH-D compliant after C-1 + I-3 fixes).
// ---------------------------------------------------------------------------
CapabilityManifest CapabilityManifest::load_from_yaml(
    const std::string& yaml_path) {
  try {
    YAML::Node root = YAML::LoadFile(yaml_path);

    const YAML::Node& v = root["vessel"];
    if (!v || v.IsNull()) {
      throw std::runtime_error(
          "CapabilityManifest: missing top-level 'vessel' key in '" +
          yaml_path + "'");
    }

    Config cfg;

    // vessel_id is REQUIRED
    if (!v["vessel_id"] || v["vessel_id"].as<std::string>().empty()) {
      throw std::runtime_error(
          "CapabilityManifest: 'vessel.vessel_id' is required in '" +
          yaml_path + "'");
    }
    cfg.vessel_id    = v["vessel_id"].as<std::string>();
    cfg.vessel_class = yaml_get<std::string>(v, "vessel_class", "unknown");

    load_geometry(v, cfg);
    load_maneuvering(v, cfg);
    load_braking(v, cfg);
    load_speed(v, cfg);
    load_mmg(v, cfg);

    return CapabilityManifest{std::move(cfg)};
  } catch (const YAML::Exception& e) {
    throw std::runtime_error(
        "CapabilityManifest YAML error in '" + yaml_path + "': " + e.what());
  }
}

// ---------------------------------------------------------------------------
// Derived convenience accessors
// ---------------------------------------------------------------------------
double CapabilityManifest::service_speed_mps() const noexcept {
  return units::kn_to_mps(config_.service_speed_kn);
}

double CapabilityManifest::speed_max_mps() const noexcept {
  return units::kn_to_mps(config_.speed_max_kn);
}

}  // namespace mass_l3::m5::shared
