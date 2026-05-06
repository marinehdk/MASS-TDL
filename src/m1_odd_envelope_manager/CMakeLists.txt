cmake_minimum_required(VERSION 3.22)
project(m1_odd_envelope_manager VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(
  -Wall -Wextra -Wpedantic -Werror
  -Wshadow -Wconversion -Wsign-conversion
  -Wcast-align -Wcast-qual -Wold-style-cast -Wzero-as-null-pointer-constant
  -Wnon-virtual-dtor -Woverloaded-virtual
  -Wnull-dereference -Wdouble-promotion -Wfloat-equal
  -Wformat=2 -Wformat-security -Wmissing-declarations -Wundef -Wunused
  -fstack-protector-strong -D_FORTIFY_SOURCE=2
  -fno-exceptions -fno-rtti
)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)
find_package(spdlog REQUIRED)
find_package(yaml-cpp REQUIRED)

# tl::expected - header only
include_directories(${CMAKE_SOURCE_DIR}/../../tools/third-party/tl-expected-1.1.0/include)

add_library(${PROJECT_NAME}_lib
  src/odd_state_machine.cpp
  src/conformance_score_calculator.cpp
  src/tmr_tdl_estimator.cpp
  src/mrc_trigger_logic.cpp
  src/parameter_loader.cpp
  src/odd_envelope_manager_node.cpp
)
target_include_directories(${PROJECT_NAME}_lib
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(${PROJECT_NAME}_lib
  rclcpp l3_msgs l3_external_msgs
)
target_link_libraries(${PROJECT_NAME}_lib PUBLIC
  spdlog::spdlog yaml-cpp::yaml-cpp
)

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_lib)

install(TARGETS ${PROJECT_NAME} ${PROJECT_NAME}_lib
  ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)
  ament_lint_auto_find_test_dependencies()
  add_subdirectory(test)
endif()

ament_export_include_directories(include)
ament_export_libraries(${PROJECT_NAME}_lib)
ament_export_dependencies(rclcpp l3_msgs l3_external_msgs)
ament_package()
