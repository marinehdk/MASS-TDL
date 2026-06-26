cmake_minimum_required(VERSION 3.8)
project(ship_dynamics)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(Eigen3 REQUIRED NO_MODULE)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(ship_interfaces REQUIRED)

include_directories(include)

add_executable(ship_dynamics_node 
  src/ship_dynamics_node.cpp
  src/main.cpp
)

target_include_directories(ship_dynamics_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

target_link_libraries(ship_dynamics_node Eigen3::Eigen)

ament_target_dependencies(ship_dynamics_node
  rclcpp
  rclcpp_lifecycle
  geometry_msgs
  nav_msgs
  tf2_ros
  ship_interfaces
)

install(TARGETS ship_dynamics_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY include/
  DESTINATION include)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config")
  install(DIRECTORY config/
    DESTINATION share/${PROJECT_NAME}/config)
endif()

ament_package()
