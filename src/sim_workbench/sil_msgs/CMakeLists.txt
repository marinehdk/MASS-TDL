cmake_minimum_required(VERSION 3.8)
project(sil_msgs)

# Default to C++17
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)

# Define the 10 message files for code generation
set(msg_files
  "msg/OwnShipState.msg"
  "msg/TargetVesselState.msg"
  "msg/RadarMeasurement.msg"
  "msg/AISMessage.msg"
  "msg/EnvironmentState.msg"
  "msg/FaultEvent.msg"
  "msg/ModulePulse.msg"
  "msg/ScoringRow.msg"
  "msg/ASDREvent.msg"
  "msg/LifecycleStatus.msg"
)

rosidl_generate_interfaces(${PROJECT_NAME}
  ${msg_files}
  DEPENDENCIES builtin_interfaces std_msgs geometry_msgs
)

ament_export_dependencies(rosidl_default_runtime)

ament_package()
