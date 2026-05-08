cmake_minimum_required(VERSION 3.22)
project(fcb_simulator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# PATH-H: tooling/HIL — pragmatic warnings, no MISRA enforcement.
add_compile_options(-Wall -Wextra -Wpedantic)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geographic_msgs REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(pluginlib REQUIRED)
find_package(ship_sim_interfaces REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(ament_index_cpp REQUIRED)

# ---------------------------------------------------------------------------
# Core simulation library (no ROS deps — pure dynamics, easy to unit-test)
# ---------------------------------------------------------------------------
add_library(fcb_simulator_core STATIC
  src/mmg_model.cpp
  src/rk4_integrator.cpp
  src/scenario_runner.cpp
)
target_include_directories(fcb_simulator_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(fcb_simulator_core PUBLIC Eigen3::Eigen)
ament_target_dependencies(fcb_simulator_core PUBLIC ship_sim_interfaces)
set_property(TARGET fcb_simulator_core PROPERTY POSITION_INDEPENDENT_CODE ON)

# Plugin shared library (loaded at runtime by pluginlib ClassLoader)
add_library(fcb_simulator_plugin SHARED
  src/fcb_simulator_plugin.cpp
)
target_include_directories(fcb_simulator_plugin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(fcb_simulator_plugin PUBLIC fcb_simulator_core yaml-cpp)
ament_target_dependencies(fcb_simulator_plugin pluginlib ship_sim_interfaces)
set_property(TARGET fcb_simulator_plugin PROPERTY POSITION_INDEPENDENT_CODE ON)
pluginlib_export_plugin_description_file(ship_sim_interfaces plugins.xml)

# ---------------------------------------------------------------------------
# ROS2 node executable
# ---------------------------------------------------------------------------
add_executable(fcb_simulator_node
  src/fcb_simulator_node.cpp
)
target_include_directories(fcb_simulator_node PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(fcb_simulator_node PRIVATE
  fcb_simulator_core
)
ament_target_dependencies(fcb_simulator_node
  rclcpp
  builtin_interfaces
  std_msgs
  geographic_msgs
  l3_msgs
  l3_external_msgs
  pluginlib
  ship_sim_interfaces
  ament_index_cpp
)

install(TARGETS fcb_simulator_node
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)
install(TARGETS fcb_simulator_core
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
)
install(TARGETS fcb_simulator_plugin
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(FILES plugins.xml DESTINATION share/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config DESTINATION share/${PROJECT_NAME})
install(DIRECTORY launch DESTINATION share/${PROJECT_NAME})

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_mmg_steady_turn
    test/test_mmg_steady_turn.cpp
  )
  target_link_libraries(test_mmg_steady_turn fcb_simulator_core)

  ament_add_gtest(test_rk4_energy
    test/test_rk4_energy.cpp
  )
  target_link_libraries(test_rk4_energy fcb_simulator_core)

  ament_add_gtest(test_stopping_error
    test/test_stopping_error.cpp
  )
  target_link_libraries(test_stopping_error fcb_simulator_core)

  ament_add_gtest(test_stability_1h
    test/test_stability_1h.cpp
    TIMEOUT 70
  )
  target_link_libraries(test_stability_1h fcb_simulator_core)

  ament_add_gtest(test_scenario_runner
    test/test_scenario_runner.cpp
  )
  target_link_libraries(test_scenario_runner fcb_simulator_core)
endif()

ament_package()
