// sil_fusion_adapter translators — implementation of the pure field-maps
// declared in translators.hpp. Mirrors the SIL→L3 fusion callbacks previously
// in docker/sil_topic_bridge.py (_on_target_vessel_state, _on_environment_state).
#include "sil_fusion_adapter/translators.hpp"

#include <cstddef>
#include <cmath>

namespace sil_fusion_adapter {
namespace {
double normalize_deg(double deg) {
  const double wrapped = std::fmod(deg, 360.0);
  return wrapped < 0.0 ? wrapped + 360.0 : wrapped;
}
}  // namespace

l3_external_msgs::msg::TrackedTargetArray target_vessel_to_tracked_array(
    const sil_msgs::msg::TargetVesselState& sil) {
  l3_msgs::msg::TrackedTarget tgt;
  tgt.schema_version = kSchemaV112;
  tgt.stamp = sil.stamp;
  tgt.target_id = sil.mmsi;
  tgt.position.latitude = sil.lat;
  tgt.position.longitude = sil.lon;
  tgt.position.altitude = 0.0;
  // SIL msg heading/cog are in RADIANS; L3 TrackedTarget expects DEGREES.
  tgt.heading_deg = sil.heading * kDegPerRad;
  tgt.cog_deg = sil.cog * kDegPerRad;
  tgt.sog_kn = sil.sog / kMpsPerKn;
  // 3x3 identity covariance (lat/lon/heading), row-major.
  tgt.covariance = {1.0, 0.0, 0.0,
                    0.0, 1.0, 0.0,
                    0.0, 0.0, 1.0};
  tgt.classification = "vessel";
  tgt.classification_confidence = 0.85f;
  // M2 owns CPA/TCPA downstream; relay carries zeros.
  tgt.cpa_m = 0.0;
  tgt.tcpa_s = 0.0;
  tgt.confidence = 0.85f;
  tgt.rationale = "SIL bridge";
  tgt.source_sensor = "fused";

  l3_external_msgs::msg::TrackedTargetArray out;
  out.schema_version = kSchemaV112;
  out.stamp = sil.stamp;
  out.targets = {std::move(tgt)};
  out.confidence = 0.85f;
  out.rationale = "SIL bridge";
  return out;
}

l3_external_msgs::msg::EnvironmentState environment_sil_to_l3(
    const sil_msgs::msg::EnvironmentState& sil) {
  l3_external_msgs::msg::EnvironmentState out;
  out.schema_version = kSchemaV112;
  out.stamp = sil.stamp;
  out.wind_speed_kn = sil.wind_speed_mps / kMpsPerKn;
  out.wind_direction_deg = sil.wind_direction;
  // SIL does not produce wave data; L3 schema requires the fields.
  out.wave_height_m = 0.0;
  out.wave_direction_deg = 0.0;
  out.current_speed_kn = sil.current_speed_mps / kMpsPerKn;
  out.current_direction_deg = sil.current_direction;
  out.visibility_range_nm = sil.visibility_nm;
  out.weather_source = "sensor";
  out.confidence = 0.9f;
  out.rationale = "SIL bridge";
  return out;
}

l3_external_msgs::msg::FilteredOwnShipState own_ship_sil_to_l3(
    const sil_msgs::msg::OwnShipState& sil) {
  l3_external_msgs::msg::FilteredOwnShipState out;
  out.schema_version = kSchemaV112;
  out.stamp = sil.stamp;
  out.position.latitude = sil.lat;
  out.position.longitude = sil.lon;
  out.position.altitude = 0.0;
  out.sog_kn = sil.sog / kMpsPerKn;
  out.cog_deg = normalize_deg(sil.cog * kDegPerRad);
  out.heading_deg = normalize_deg(sil.heading * kDegPerRad);
  out.u_water = sil.u;
  out.v_water = sil.v;
  out.r_dot_deg_s = sil.rot * kDegPerRad;
  out.current_speed_kn = 0.0;
  out.current_direction_deg = 0.0;
  out.roll_deg = 0.0;
  out.pitch_deg = 0.0;
  out.covariance.fill(0.0);
  for (std::size_t i = 0; i < 6; ++i) {
    out.covariance[i * 6 + i] = 1.0;
  }
  out.nav_mode = "OPTIMAL";
  out.confidence = 0.95f;
  out.rationale = "SIL bridge";
  return out;
}

}  // namespace sil_fusion_adapter
