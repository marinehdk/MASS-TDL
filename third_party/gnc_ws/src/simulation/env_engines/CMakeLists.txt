cmake_minimum_required(VERSION 3.8)
project(env_engines)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(ship_interfaces REQUIRED)

include_directories(include)

add_executable(wind_engine_node src/nodes/wind_engine_node.cpp src/nodes/env_engine_base.cpp)
target_include_directories(wind_engine_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
ament_target_dependencies(wind_engine_node rclcpp geometry_msgs nav_msgs ship_interfaces)

add_executable(wave_engine_node src/nodes/wave_engine_node.cpp src/nodes/env_engine_base.cpp)
target_include_directories(wave_engine_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
ament_target_dependencies(wave_engine_node rclcpp geometry_msgs nav_msgs ship_interfaces)

add_executable(current_engine_node src/nodes/current_engine_node.cpp src/nodes/env_engine_base.cpp)
target_include_directories(current_engine_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
ament_target_dependencies(current_engine_node rclcpp ship_interfaces geometry_msgs nav_msgs)

add_executable(force_aggregator_node src/nodes/force_aggregator_node.cpp)
target_include_directories(force_aggregator_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
ament_target_dependencies(force_aggregator_node rclcpp geometry_msgs ship_interfaces)

install(TARGETS wind_engine_node wave_engine_node current_engine_node force_aggregator_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY data/
  DESTINATION share/${PROJECT_NAME}/data)

ament_package()
