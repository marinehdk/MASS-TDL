cmake_minimum_required(VERSION 3.8)
project(ship_interfaces)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(std_msgs REQUIRED)

set(msg_files
  "msg/ShipState.msg"
  "msg/EnvironmentalData.msg"
  "msg/ThrustCommand.msg"
  "msg/ControlCommand.msg"
  "msg/OceanCurrents.msg"
  "msg/ThrusterCmd.msg"
  "msg/ThrusterState.msg"
  "msg/VesselParams.msg"
  "msg/RoutePlan.msg"
  "msg/RoutePlanStatus.msg"
  "msg/AvoidancePlan.msg"
  "msg/RouteExecutionStatus.msg"
  msg/GeoPosition.msg
  "msg/ShipReset.msg"
)

set(srv_files
  "srv/GetShipState.srv"
  "srv/SetControlParams.srv"
)

rosidl_generate_interfaces(${PROJECT_NAME}
  ${msg_files}
  ${srv_files}
  DEPENDENCIES std_msgs builtin_interfaces
)

ament_export_dependencies(rosidl_default_runtime)

ament_package()
