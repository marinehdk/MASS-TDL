cmake_minimum_required(VERSION 3.22)
project(l3_external_msgs)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(geographic_msgs REQUIRED)
# Wave 0 fix F-IMP-B-003: l3_external_msgs now depends on l3_msgs
# (TrackedTargetArray uses l3_msgs/TrackedTarget[]; AoS rewrite per ros2-idl-implementation-guide.md §3.3)
find_package(l3_msgs REQUIRED)

set(MSG_FILES
  msg/VoyageTask.msg
  msg/PlannedRoute.msg
  msg/SpeedProfile.msg
  msg/ReplanResponse.msg
  msg/TrackedTargetArray.msg
  msg/FilteredOwnShipState.msg
  msg/EnvironmentState.msg
  msg/CheckerVetoNotification.msg
  msg/EmergencyCommand.msg
  msg/ReflexActivationNotification.msg
  msg/OverrideActiveSignal.msg
  # Wave 0 fix F-CRIT-B-003: TimeWindow moved here from l3_msgs (only consumer is VoyageTask)
  msg/TimeWindow.msg
)

rosidl_generate_interfaces(${PROJECT_NAME}
  ${MSG_FILES}
  DEPENDENCIES builtin_interfaces geometry_msgs geographic_msgs l3_msgs
  ADD_LINTER_TESTS
)

ament_export_dependencies(rosidl_default_runtime l3_msgs)
ament_package()
