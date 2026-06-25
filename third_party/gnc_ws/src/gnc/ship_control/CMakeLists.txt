cmake_minimum_required(VERSION 3.8)
project(ship_control)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(Eigen3 REQUIRED)

include_directories(include)

add_executable(ship_control_node src/ship_control_node.cpp)
ament_target_dependencies(ship_control_node 
  rclcpp 
  nav_msgs 
  geometry_msgs 
  tf2 
  tf2_geometry_msgs
  Eigen3
)

install(TARGETS
  ship_control_node
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()