cmake_minimum_required(VERSION 3.8)
project(thrust_allocation)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(ship_interfaces REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)

include_directories(include)
include_directories(${EIGEN3_INCLUDE_DIRS})

add_executable(thrust_allocation_node src/thrust_allocation_node.cpp)
ament_target_dependencies(thrust_allocation_node rclcpp geometry_msgs std_msgs ship_interfaces Eigen3 nav_msgs tf2 tf2_geometry_msgs)

install(TARGETS
  thrust_allocation_node
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()