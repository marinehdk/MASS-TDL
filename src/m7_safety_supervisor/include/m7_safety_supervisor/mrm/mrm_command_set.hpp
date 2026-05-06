#ifndef M7_SAFETY_SUPERVISOR_MRM_MRM_COMMAND_SET_HPP_
#define M7_SAFETY_SUPERVISOR_MRM_MRM_COMMAND_SET_HPP_

#include <cstdint>
#include <string_view>

namespace mass_l3::m7::mrm {

// 4 pre-defined MRM IDs (ADR-001: M7 never generates trajectories)
enum class MrmId : std::uint8_t {
  kNone = 0,
  kMrm01_Drift = 1,      // Decelerate to safe speed (COLREG Rule 8 + Rule 6)
  kMrm02_Anchor = 2,     // Anchor (water_depth <= 30m + sog <= 4 kn)
  kMrm03_HeaveTo = 3,    // Emergency turn +/-60 degrees (multiple targets close)
  kMrm04_Mooring = 4     // Harbor mooring (ODD-C zone + sog <= 2 kn)
};

[[nodiscard]] constexpr std::string_view to_string(MrmId id) noexcept {
  switch (id) {
    case MrmId::kNone:          return "NONE";
    case MrmId::kMrm01_Drift:   return "MRM-01-DRIFT";
    case MrmId::kMrm02_Anchor:  return "MRM-02-ANCHOR";
    case MrmId::kMrm03_HeaveTo: return "MRM-03-HEAVE-TO";
    case MrmId::kMrm04_Mooring: return "MRM-04-MOORING";
    default:                    return "UNKNOWN";
  }
}

struct Mrm01Params {
  double target_speed_kn{4.0};     // [TBD-HAZID §11.6]
  double max_decel_time_s{30.0};   // [TBD-HAZID §11.6]
};
struct Mrm02Params {
  double max_water_depth_m{30.0};  // [TBD-HAZID §11.6]
  double max_speed_kn{4.0};        // [TBD-HAZID]
};
struct Mrm03Params {
  double turn_angle_deg{60.0};     // [TBD-HAZID §11.6 +/-60 degrees]
  double rot_factor{0.8};          // [TBD-HAZID 0.8 x ROT_max]
};
struct Mrm04Params {
  bool requires_harbor_zone{true};
  double max_speed_kn{2.0};
};

struct MrmCommandSet {
  Mrm01Params mrm01;
  Mrm02Params mrm02;
  Mrm03Params mrm03;
  Mrm04Params mrm04;
};

// Boot-time loader only; not on the decision path
class MrmCommandSetLoader {
public:
  [[nodiscard]] static MrmCommandSet load_from_yaml(std::string_view yaml_path) noexcept;
  [[nodiscard]] static MrmCommandSet safe_default() noexcept;
};

}  // namespace mass_l3::m7::mrm

#endif  // M7_SAFETY_SUPERVISOR_MRM_MRM_COMMAND_SET_HPP_
