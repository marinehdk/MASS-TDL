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
)

rosidl_generate_interfaces(${PROJECT_NAME}
  ${MSG_FILES}
  DEPENDENCIES builtin_interfaces geometry_msgs geographic_msgs
  ADD_LINTER_TESTS
)

ament_export_dependencies(rosidl_default_runtime)
ament_package()
