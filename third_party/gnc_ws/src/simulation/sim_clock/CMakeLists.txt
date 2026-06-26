cmake_minimum_required(VERSION 3.8)
project(sim_clock)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rosgraph_msgs REQUIRED)

add_executable(sim_clock src/sim_clock_node.cpp)
target_include_directories(sim_clock PRIVATE include)
ament_target_dependencies(sim_clock rclcpp rosgraph_msgs)

install(TARGETS sim_clock
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()
