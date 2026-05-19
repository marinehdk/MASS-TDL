cmake_minimum_required(VERSION 3.22)
project(m4_behavior_arbiter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(geographic_msgs REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)
find_package(Eigen3 5.0.0 EXACT REQUIRED)
find_package(spdlog REQUIRED)
find_package(yaml-cpp REQUIRED)

set(M4_CORE_SOURCES
  src/behavior_dictionary.cpp
  src/behavior_activation.cpp
  src/ivp_function.cpp
  src/ivp_domain.cpp
  src/ivp_combine.cpp
  src/ivp_solver.cpp
  src/behavior_priority.cpp
  src/asdr_logger.cpp
  src/behavior_arbiter.cpp
)

add_library(m4_core ${M4_CORE_SOURCES})
target_include_directories(m4_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(m4_core PUBLIC
  Eigen3::Eigen
  spdlog::spdlog
  yaml-cpp::yaml-cpp
)
ament_target_dependencies(m4_core PUBLIC
  rclcpp
  builtin_interfaces
  geographic_msgs
  l3_msgs
  l3_external_msgs
)
target_compile_options(m4_core PRIVATE
  -Wall -Wextra -Wpedantic -Werror
  -Wshadow -Wconversion -Wsign-conversion
  -Wnon-virtual-dtor -Woverloaded-virtual
  -Wnull-dereference -Wdouble-promotion -Wfloat-equal
  -Wformat=2 -Wformat-security -Wmissing-declarations -Wundef -Wunused
  -fstack-protector-strong
  "$<$<CONFIG:Release>:-O2>"
  "$<$<CONFIG:Debug>:-O0;-g;-fsanitize=address;-fsanitize=undefined;-fno-omit-frame-pointer>"
)

add_executable(m4_behavior_arbiter_node
  src/behavior_arbiter_node.cpp
  src/main.cpp
)
target_link_libraries(m4_behavior_arbiter_node PRIVATE m4_core)
target_include_directories(m4_behavior_arbiter_node PRIVATE include)
ament_target_dependencies(m4_behavior_arbiter_node PRIVATE
  rclcpp
  l3_msgs
)
target_compile_options(m4_behavior_arbiter_node PRIVATE
  -Wall -Wextra -Wpedantic -Werror
  -Wshadow -Wconversion -Wsign-conversion
  -Wnon-virtual-dtor -Woverloaded-virtual
  -Wnull-dereference -Wdouble-promotion -Wfloat-equal
  -Wformat=2 -Wformat-security -Wmissing-declarations -Wundef -Wunused
  -fstack-protector-strong
  "$<$<CONFIG:Release>:-O2>"
  "$<$<CONFIG:Debug>:-O0;-g;-fsanitize=address;-fsanitize=undefined;-fno-omit-frame-pointer>"
)

install(TARGETS m4_core
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)
install(TARGETS m4_behavior_arbiter_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config/ DESTINATION share/${PROJECT_NAME}/config)
install(DIRECTORY test/fixtures/ DESTINATION share/${PROJECT_NAME}/test/fixtures)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)
  ament_lint_auto_find_test_dependencies()

  ament_add_gtest(test_behavior_dictionary test/unit/test_behavior_dictionary.cpp)
  target_link_libraries(test_behavior_dictionary m4_core)
  target_include_directories(test_behavior_dictionary PRIVATE include)

  ament_add_gtest(test_behavior_activation test/unit/test_behavior_activation.cpp)
  target_link_libraries(test_behavior_activation m4_core)
  target_include_directories(test_behavior_activation PRIVATE include)

  ament_add_gtest(test_ivp_function test/unit/test_ivp_function.cpp)
  target_link_libraries(test_ivp_function m4_core)
  target_include_directories(test_ivp_function PRIVATE include)

  ament_add_gtest(test_ivp_solver test/unit/test_ivp_solver.cpp)
  target_link_libraries(test_ivp_solver m4_core)
  target_include_directories(test_ivp_solver PRIVATE include)

  ament_add_gtest(test_behavior_priority test/unit/test_behavior_priority.cpp)
  target_link_libraries(test_behavior_priority m4_core)
  target_include_directories(test_behavior_priority PRIVATE include)

  ament_add_gtest(test_behavior_arbiter test/unit/test_behavior_arbiter.cpp)
  target_link_libraries(test_behavior_arbiter m4_core)
  target_include_directories(test_behavior_arbiter PRIVATE include)

  ament_add_gtest(test_asdr_logger test/unit/test_asdr_logger.cpp)
  target_link_libraries(test_asdr_logger m4_core)
  target_include_directories(test_asdr_logger PRIVATE include)
endif()

ament_package()
