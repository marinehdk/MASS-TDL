#ifndef SIL_FUSION_ADAPTER_TRANSLATORS_HPP_
#define SIL_FUSION_ADAPTER_TRANSLATORS_HPP_
// sil_fusion_adapter translators — pure field-maps from SIL sensor-side types
// to the L3 fusion-side external types. No ROS node state, so the maps are
// unit-testable without spinning an executor.
//
// Mapping (mirrors the prior sil_topic_bridge.py fusion callbacks):
//   sil_msgs/TargetVesselState      -> l3_external_msgs/TrackedTargetArray
//     (single target -> 1-element array; heading is in RADIANS on the SIL msg
//      and DEGREES on the L3 msg; sog m/s -> kn; 3x3 identity covariance;
//      classification "vessel" @ 0.85)
//   sil_msgs/EnvironmentState       -> l3_external_msgs/EnvironmentState
//     (wind/current m/s -> kn; directions deg->deg passthrough; visibility nm->nm;
//      wave fields zeroed — SIL does not produce wave data)
//   sil_msgs/OwnShipState           -> l3_external_msgs/FilteredOwnShipState
//     (position passthrough; SOG m/s -> kn; heading/cog/ROT already deg/deg_s;
//      through-water velocity remains m/s)
//
// Constants: schema_version 112 = architecture v1.1.2.
#include "l3_external_msgs/msg/environment_state.hpp"
#include "l3_external_msgs/msg/filtered_own_ship_state.hpp"
#include "l3_external_msgs/msg/tracked_target_array.hpp"
#include "l3_msgs/msg/tracked_target.hpp"
#include "sil_msgs/msg/environment_state.hpp"
#include "sil_msgs/msg/own_ship_state.hpp"
#include "sil_msgs/msg/target_vessel_state.hpp"

namespace sil_fusion_adapter {

constexpr uint16_t kSchemaV112 = 112;
constexpr double kMpsPerKn = 0.51444;          // 1 kn in m/s
constexpr double kRadPerDeg = 0.017453292519943295;
constexpr double kDegPerRad = 57.295779513082323;

// TargetVesselState (single) -> TrackedTargetArray (1-element). Matches the
// bridge's _on_target_vessel_state: heading rad->deg, sog m/s->kn, identity
// covariance, source_sensor "fused", confidence 0.85.
l3_external_msgs::msg::TrackedTargetArray target_vessel_to_tracked_array(
    const sil_msgs::msg::TargetVesselState& sil);

// EnvironmentState (SIL m/s) -> EnvironmentState (L3 kn). Matches the bridge's
// _on_environment_state: wind/current m/s->kn, directions deg->deg, visibility
// nm->nm, wave fields zeroed, weather_source "sensor".
l3_external_msgs::msg::EnvironmentState environment_sil_to_l3(
    const sil_msgs::msg::EnvironmentState& sil);

// OwnShipState (SIL m/s + deg) -> FilteredOwnShipState (L3 kn + deg).
l3_external_msgs::msg::FilteredOwnShipState own_ship_sil_to_l3(
    const sil_msgs::msg::OwnShipState& sil);

}  // namespace sil_fusion_adapter

#endif  // SIL_FUSION_ADAPTER_TRANSLATORS_HPP_
