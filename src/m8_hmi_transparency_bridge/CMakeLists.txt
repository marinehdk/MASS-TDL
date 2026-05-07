cmake_minimum_required(VERSION 3.22)
project(m8_hmi_transparency_bridge LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()

add_compile_options(
  -Wall -Wextra -Wpedantic -Werror
  -Wshadow -Wconversion -Wsign-conversion
  -Wcast-align -Wcast-qual
  -Wold-style-cast -Wzero-as-null-pointer-constant
  -Wnon-virtual-dtor -Woverloaded-virtual
  -Wnull-dereference -Wdouble-promotion -Wfloat-equal
  -Wformat=2 -Wformat-security
  -Wmissing-declarations -Wundef -Wunused
  -Wuseless-cast -Wlogical-op
  -Wduplicated-cond -Wduplicated-branches
  -fstack-protector-strong
  -D_FORTIFY_SOURCE=2
)
add_compile_options("$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined;-fno-omit-frame-pointer>")
add_link_options("$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined>")

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(nlohmann_json 3.12.0 REQUIRED)
find_package(spdlog 1.17.0 REQUIRED)
find_package(yaml-cpp REQUIRED)

add_library(m8_core
  src/sha256.cpp
  src/sat_aggregator.cpp
  src/adaptive_sat_trigger.cpp
  src/tor_protocol.cpp
  src/ui_state_builder.cpp
  src/tor_request_generator.cpp
  src/asdr_logger.cpp
  src/module_health_monitor.cpp
)
target_include_directories(m8_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(m8_core
  rclcpp l3_msgs l3_external_msgs
)
target_link_libraries(m8_core
  Eigen3::Eigen
  nlohmann_json::nlohmann_json
  spdlog::spdlog
  yaml-cpp
)

add_executable(m8_hmi_transparency_bridge_node
  src/hmi_transparency_bridge_node.cpp
  src/main.cpp
)
target_link_libraries(m8_hmi_transparency_bridge_node m8_core)
ament_target_dependencies(m8_hmi_transparency_bridge_node
  rclcpp l3_msgs l3_external_msgs
)

install(TARGETS m8_core m8_hmi_transparency_bridge_node
  EXPORT export_m8_core
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY launch config DESTINATION share/${PROJECT_NAME})
install(DIRECTORY python/ DESTINATION lib/${PROJECT_NAME}/python
        FILES_MATCHING PATTERN "*.py")

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  ament_add_gtest(test_sat_aggregator test/test_sat_aggregator.cpp)
  target_link_libraries(test_sat_aggregator m8_core)

  ament_add_gtest(test_adaptive_sat_trigger test/test_adaptive_sat_trigger.cpp)
  target_link_libraries(test_adaptive_sat_trigger m8_core)

  ament_add_gtest(test_tor_protocol test/test_tor_protocol.cpp)
  target_link_libraries(test_tor_protocol m8_core)

  ament_add_gtest(test_ui_state_builder test/test_ui_state_builder.cpp)
  target_link_libraries(test_ui_state_builder m8_core)

  ament_add_gtest(test_tor_request_generator test/test_tor_request_generator.cpp)
  target_link_libraries(test_tor_request_generator m8_core)

  ament_add_gtest(test_asdr_logger test/test_asdr_logger.cpp)
  target_link_libraries(test_asdr_logger m8_core)

  ament_add_gtest(test_module_health_monitor
                  test/test_module_health_monitor.cpp)
  target_link_libraries(test_module_health_monitor m8_core)

  find_package(ament_cmake_pytest REQUIRED)
  ament_add_pytest_test(m8_python_tests python/tests/
    PYTHON_EXECUTABLE python3
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/python
  )
endif()

ament_package()
