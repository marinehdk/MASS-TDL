cmake_minimum_required(VERSION 3.22)
project(m4_behavior_arbiter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-fno-fast-math -ffp-contract=off)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)
find_package(spdlog REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(builtin_interfaces REQUIRED)

# Core library — pure logic, no ROS dependency
add_library(m4_core STATIC
  src/ivp_domain.cpp
  src/ivp_function.cpp
  src/ivp_combine.cpp
  src/ivp_solver.cpp
  src/behavior_dictionary.cpp
  src/behavior_activation.cpp
  src/behavior_priority.cpp
  src/colregs_directive.cpp
)
target_include_directories(m4_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(m4_core PUBLIC
  yaml-cpp::yaml-cpp
  spdlog::spdlog
  ${l3_msgs_TARGETS}
)
target_compile_options(m4_core PRIVATE -Wall -Wextra -Wpedantic)

# Node library — ROS2 integration
add_library(m4_node_lib STATIC
  src/behavior_arbiter_node.cpp
)
target_include_directories(m4_node_lib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(m4_node_lib PUBLIC
  m4_core
  rclcpp::rclcpp
  ${l3_msgs_TARGETS}
  ${l3_external_msgs_TARGETS}
  ${builtin_interfaces_TARGETS}
)
target_compile_options(m4_node_lib PRIVATE -Wall -Wextra -Wpedantic)

# Executable
add_executable(m4_behavior_arbiter src/main.cpp)
target_link_libraries(m4_behavior_arbiter m4_node_lib)

install(TARGETS m4_core m4_node_lib m4_behavior_arbiter
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config/ DESTINATION share/${PROJECT_NAME}/config)
install(DIRECTORY launch/ DESTINATION share/${PROJECT_NAME}/launch)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_ivp_domain test/unit/test_ivp_domain.cpp)
  target_link_libraries(test_ivp_domain m4_core)
  target_include_directories(test_ivp_domain PRIVATE include)

  ament_add_gtest(test_ivp_function test/unit/test_ivp_function.cpp)
  target_link_libraries(test_ivp_function m4_core)
  target_include_directories(test_ivp_function PRIVATE include)

  ament_add_gtest(test_ivp_combine test/unit/test_ivp_combine.cpp)
  target_link_libraries(test_ivp_combine m4_core)
  target_include_directories(test_ivp_combine PRIVATE include)

  ament_add_gtest(test_ivp_solver test/unit/test_ivp_solver.cpp)
  target_link_libraries(test_ivp_solver m4_core)
  target_include_directories(test_ivp_solver PRIVATE include)

  ament_add_gtest(test_behavior_dictionary test/unit/test_behavior_dictionary.cpp)
  target_link_libraries(test_behavior_dictionary m4_core)
  target_include_directories(test_behavior_dictionary PRIVATE include)
  target_compile_definitions(test_behavior_dictionary PRIVATE
    TEST_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config"
  )

  ament_add_gtest(test_behavior_activation test/unit/test_behavior_activation.cpp)
  target_link_libraries(test_behavior_activation m4_core)
  target_include_directories(test_behavior_activation PRIVATE include)

  ament_add_gtest(test_behavior_priority test/unit/test_behavior_priority.cpp)
  target_link_libraries(test_behavior_priority m4_core)
  target_include_directories(test_behavior_priority PRIVATE include)

  ament_add_gtest(test_colregs_directive test/unit/test_colregs_directive.cpp)
  target_link_libraries(test_colregs_directive m4_core)
  target_include_directories(test_colregs_directive PRIVATE include)

  ament_add_gtest(test_m4_node_lifecycle test/unit/test_m4_node_lifecycle.cpp)
  target_link_libraries(test_m4_node_lifecycle m4_node_lib)
  target_include_directories(test_m4_node_lifecycle PRIVATE include)
endif()

ament_package()
