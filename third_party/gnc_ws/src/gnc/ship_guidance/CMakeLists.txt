cmake_minimum_required(VERSION 3.8)
project(ship_guidance)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(ship_interfaces REQUIRED)

include_directories(
  include
  ${EIGEN3_INCLUDE_DIRS}
)

add_executable(ship_guidance_node src/ship_guidance_node.cpp)
target_include_directories(ship_guidance_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(ship_guidance_node PUBLIC cxx_std_17)
ament_target_dependencies(ship_guidance_node rclcpp nav_msgs geometry_msgs std_msgs tf2 tf2_geometry_msgs ship_interfaces)
target_link_libraries(ship_guidance_node Eigen3::Eigen)

add_executable(coordinate_transform_node src/coordinate_transform_node.cpp)
target_include_directories(coordinate_transform_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(coordinate_transform_node PUBLIC cxx_std_17)
ament_target_dependencies(coordinate_transform_node rclcpp nav_msgs geometry_msgs std_msgs ship_interfaces)

add_executable(active_route_manager_node src/active_route_manager_node.cpp)
target_include_directories(active_route_manager_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(active_route_manager_node PUBLIC cxx_std_17)
ament_target_dependencies(active_route_manager_node rclcpp std_msgs ship_interfaces)

install(TARGETS ship_guidance_node coordinate_transform_node active_route_manager_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY include/
  DESTINATION include)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_route_update_guard_policy
    test/test_route_update_guard_policy.cpp)
  target_include_directories(test_route_update_guard_policy PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_route_update_guard_policy PUBLIC cxx_std_17)

  ament_add_gtest(test_navigation_mode_policy
    test/test_navigation_mode_policy.cpp)
  target_include_directories(test_navigation_mode_policy PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_navigation_mode_policy PUBLIC cxx_std_17)
endif()

ament_package()
