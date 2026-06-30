cmake_minimum_required(VERSION 3.8)
project(gnc_bridge)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)
find_package(sil_msgs REQUIRED)
find_package(ship_interfaces REQUIRED)

# Translator library — pure field maps, no ROS node state. Linked by the node
# and the unit tests so the maps are exercised without spinning executors.
add_library(gnc_bridge_translators SHARED
  src/translators.cpp
)
target_include_directories(gnc_bridge_translators PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(gnc_bridge_translators
  rclcpp
  builtin_interfaces
  l3_msgs
  l3_external_msgs
  sil_msgs
  ship_interfaces
)

# The bridge node (two-context cross-domain). Built as an executable.
add_executable(gnc_bridge_node
  src/gnc_bridge_node.cpp
  src/main.cpp
)
target_link_libraries(gnc_bridge_node gnc_bridge_translators)
ament_target_dependencies(gnc_bridge_node
  rclcpp
  builtin_interfaces
  l3_msgs
  l3_external_msgs
  sil_msgs
  ship_interfaces
)

# Install the translator library alongside the executable and set RPATH so the
# runtime linker finds libgnc_bridge_translators.so without LD_LIBRARY_PATH.
# Executable installs to lib/<pkg>/, library installs to lib/, so the exe's
# RPATH must point one level up ($ORIGIN/..).
set_target_properties(gnc_bridge_translators PROPERTIES
  INSTALL_RPATH "$ORIGIN")
set_target_properties(gnc_bridge_node PROPERTIES
  INSTALL_RPATH "$ORIGIN/..")
install(TARGETS gnc_bridge_node gnc_bridge_translators
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_translators test/test_translators.cpp)
  target_link_libraries(test_translators gnc_bridge_translators)
  ament_target_dependencies(test_translators
    rclcpp
    builtin_interfaces
    l3_msgs
    l3_external_msgs
    sil_msgs
    ship_interfaces
  )
endif()

ament_package()
