cmake_minimum_required(VERSION 3.8)
project(route_planning_cpp)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Find ROS2 packages
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(ship_interfaces REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(std_msgs REQUIRED)

# Find third-party libraries
find_package(nlohmann_json 3.2.0 REQUIRED)
find_package(websocketpp REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)

# gnc_sim_node
add_executable(gnc_sim_node src/gnc_sim_node.cpp)
ament_target_dependencies(gnc_sim_node rclcpp ship_interfaces std_msgs)
target_link_libraries(gnc_sim_node nlohmann_json::nlohmann_json)

# route_bridge_node
add_executable(route_bridge_node src/route_bridge_node.cpp)
ament_target_dependencies(route_bridge_node rclcpp ship_interfaces nav_msgs std_msgs)
target_link_libraries(route_bridge_node
  nlohmann_json::nlohmann_json
  ${Boost_LIBRARIES}
  OpenSSL::SSL
  OpenSSL::Crypto
)
target_include_directories(route_bridge_node PRIVATE ${websocketpp_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})

# Install targets
install(TARGETS gnc_sim_node route_bridge_node
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()
