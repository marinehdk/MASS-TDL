#ifndef MASS_L3_M5_SHARED_CAPABILITY_MANIFEST_HPP_
#define MASS_L3_M5_SHARED_CAPABILITY_MANIFEST_HPP_

// M5 Tactical Planner — CapabilityManifest (Backseat Driver pattern)
// PATH-D (MISRA C++:2023): no ship constants in decision-core code.
//
// All FCB hydrodynamic coefficients are loaded from a vessel YAML file at
// runtime. The decision-core (MPC solver, CPA calculator) receives a
// CapabilityManifest reference — never a vessel-type enum or constant.
//
// Backseat Driver pattern reference:
//   Architecture report §4.4 — "多船型 = Capability Manifest + PVA 适配 + 水动力插件"
//
// MMG model reference:
//   Yasukawa, H. & Yoshimura, Y. (2015). Introduction of MMG standard method
//   for ship maneuvering predictions. J. Mar. Sci. Technol. 20, 37–52. [R7]

#include <cstdint>
#include <stdexcept>
#include <string>

namespace mass_l3::m5::shared {

// ---------------------------------------------------------------------------
// CapabilityManifest — vessel hydrodynamic parameters loaded from YAML
// ---------------------------------------------------------------------------
class CapabilityManifest {
 public:
  // -------------------------------------------------------------------------
  // Config — all vessel parameters; zero hardcoded values in decision-core
  // All fields tagged [TBD-HAZID] must be updated from FCB pool-test data
  // and sea trial measurements (HAZID RUN-001, target 2026-08-19).
  // -------------------------------------------------------------------------
  struct Config {
    std::string vessel_id;    // REQUIRED — e.g. "FCB-SANGO-001"
    std::string vessel_class; // e.g. "fast-crew-boat"

    // --- Geometry [m] ---
    double length_m{28.0};   // LOA
    double beam_m{6.5};
    double draft_m{1.4};

    // --- Inertia ---
    // [TBD-HAZID] mass_kg: from inclining experiment / loading computer
    double mass_kg{95000.0};

    // --- Maneuvering (MMG Yasukawa 2015 [R7]) ---
    // [TBD-HAZID] rot_max_at_18kn_rad_s: from IMO turning circle trial
    // 0.20944 rad/s ≈ 12 deg/s (preliminary design estimate)
    double rot_max_at_18kn_rad_s{0.20944};

    // Speed-dependent ROT correction (linear interpolation between two nodes)
    // [TBD-HAZID] low_speed_factor, high_speed_factor: from maneuvering trials
    double low_speed_kn{10.0};
    double low_speed_factor{1.2};   // ROT scales up at low speed
    double high_speed_kn{20.0};
    double high_speed_factor{0.8};  // ROT scales down at high speed

    // [TBD-HAZID] rough_sea_factor_per_hs: 10% ROT reduction per metre Hs.
    // Must be validated in Hs = 1, 2, 3 m sea states (FCB open-sea trials).
    double rough_sea_factor_per_hs{0.10};

    // --- Braking ---
    // [TBD-HAZID] stopping_distance_at_18kn_m: from ahead-to-astern trial
    double stopping_distance_at_18kn_m{250.0};

    // [TBD-HAZID] crash_stop_max_decel_mps2: structural / comfort limit
    double crash_stop_max_decel_mps2{0.8};

    // --- Speed limits [kn] ---
    double speed_min_kn{0.0};

    // [TBD-HAZID] speed_max_kn: from propulsion design, ODD domain cap
    double speed_max_kn{28.0};
    double service_speed_kn{18.0};

    // --- MMG added-mass / inertia coefficients (non-dimensional, [R7]) ---
    // [TBD-HAZID] all three: from CFD or system-ID on sea trials
    double surge_added_mass_factor{0.05};   // m_x / m  (typically 0.03–0.10)
    double sway_added_mass_factor{0.40};    // m_y / m  (typically 0.20–0.60)
    double yaw_added_inertia_factor{0.07};  // J_z / I_zz (typically 0.05–0.15)
  };

  // -------------------------------------------------------------------------
  // Factory: load from YAML file (Backseat Driver pattern entry point)
  // Throws std::runtime_error if vessel_id is missing or file is unreadable.
  // No other constructor is exposed — forces all code paths through YAML.
  // -------------------------------------------------------------------------
  [[nodiscard]] static CapabilityManifest load_from_yaml(
      const std::string& yaml_path);

  // -------------------------------------------------------------------------
  // Accessors
  // -------------------------------------------------------------------------
  [[nodiscard]] const Config& config() const noexcept { return config_; }

  // Convenience: service speed in m/s (derived; not stored separately)
  [[nodiscard]] double service_speed_mps() const noexcept;

  // Convenience: speed_max in m/s
  [[nodiscard]] double speed_max_mps() const noexcept;

 private:
  explicit CapabilityManifest(Config cfg) : config_(std::move(cfg)) {}

  Config config_;
};

}  // namespace mass_l3::m5::shared

#endif  // MASS_L3_M5_SHARED_CAPABILITY_MANIFEST_HPP_
