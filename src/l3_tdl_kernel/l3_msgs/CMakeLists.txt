cmake_minimum_required(VERSION 3.22)
project(l3_msgs)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(geographic_msgs REQUIRED)

set(MSG_FILES
  msg/ODDState.msg
  msg/ModeCmd.msg
  msg/WorldState.msg
  msg/MissionGoal.msg
  msg/RouteReplanRequest.msg
  msg/BehaviorPlan.msg
  msg/COLREGsConstraint.msg
  msg/AvoidancePlan.msg
  msg/ReactiveOverrideCmd.msg
  msg/SafetyAlert.msg
  msg/ASDRRecord.msg
  msg/ToRRequest.msg
  msg/UIState.msg
  msg/SATData.msg
  msg/TrackedTarget.msg
  msg/OwnShipState.msg
  msg/EncounterClassification.msg
  msg/ZoneConstraint.msg
  msg/AvoidanceWaypoint.msg
  msg/SpeedSegment.msg
  msg/Constraint.msg
  msg/RuleActive.msg
  msg/SAT1Data.msg
  msg/SAT2Data.msg
  msg/SAT3Data.msg
)

rosidl_generate_interfaces(${PROJECT_NAME}
  ${MSG_FILES}
  DEPENDENCIES builtin_interfaces geometry_msgs geographic_msgs
  ADD_LINTER_TESTS
)

ament_export_dependencies(rosidl_default_runtime)
ament_package()
