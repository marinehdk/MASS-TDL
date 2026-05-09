#include "m7_safety_supervisor/mrm/mrm_command_set.hpp"

#include <string_view>

namespace mass_l3::m7::mrm {

// ---------------------------------------------------------------------------
// safe_default()
// Returns default-initialized MrmCommandSet (all struct defaults apply).
// ---------------------------------------------------------------------------

MrmCommandSet MrmCommandSetLoader::safe_default() noexcept {
  return MrmCommandSet{};
}

// ---------------------------------------------------------------------------
// load_from_yaml()
// [TBD-T6]: Full YAML parsing in SafetySupervisorNode boot sequence.
// yaml-cpp throws exceptions; since M7 compiles with -fno-exceptions,
// YAML parsing is deferred to the ROS2 node boot sequence (Task 6).
// For now, always return safe_default().
// ---------------------------------------------------------------------------

MrmCommandSet MrmCommandSetLoader::load_from_yaml(std::string_view /*yaml_path*/) noexcept {
  // [TBD-T6]: Full YAML parsing in SafetySupervisorNode boot sequence.
  // yaml-cpp is a throwing API; full integration deferred to Task 6 node.
  return safe_default();
}

}  // namespace mass_l3::m7::mrm
