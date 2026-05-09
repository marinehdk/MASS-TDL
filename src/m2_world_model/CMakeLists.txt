cmake_minimum_required(VERSION 3.22)
project(m2_world_model VERSION 1.0.0 LANGUAGES CXX)

# ─────────── C++ Standard ───────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ─────────── ROS2 / ament ───────────
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(geographic_msgs REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)

# ─────────── Third-Party ───────────
find_package(Eigen3 3.4 REQUIRED)
list(APPEND CMAKE_MODULE_PATH "/usr/share/cmake/geographiclib")
find_package(GeographicLib REQUIRED)
if(NOT TARGET GeographicLib::GeographicLib)
  add_library(GeographicLib::GeographicLib UNKNOWN IMPORTED)
  set_target_properties(GeographicLib::GeographicLib PROPERTIES
    IMPORTED_LOCATION "${GeographicLib_LIBRARIES}"
    INTERFACE_INCLUDE_DIRECTORIES "${GeographicLib_INCLUDE_DIRS}")
endif()
find_package(Boost REQUIRED COMPONENTS headers)
find_package(spdlog REQUIRED)
find_package(yaml-cpp REQUIRED)

# ─────────── Compile Options Interface (per coding-standards.md §2.2) ───────────
add_library(m2_compile_options INTERFACE)
target_compile_options(m2_compile_options INTERFACE
    -Wall -Wextra -Wpedantic
    -Werror
    -Wshadow -Wconversion -Wsign-conversion
    -Wcast-align -Wcast-qual
    -Wold-style-cast -Wzero-as-null-pointer-constant
    -Wnon-virtual-dtor -Woverloaded-virtual
    -Wnull-dereference -Wdouble-promotion
    -Wfloat-equal
    -Wformat=2 -Wformat-security
    -Wmissing-declarations
    -Wundef -Wunused
    -Wuseless-cast
    -Wlogical-op
    -Wduplicated-cond -Wduplicated-branches
    -fstack-protector-strong
    -D_FORTIFY_SOURCE=2
    "$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined;-fno-omit-frame-pointer>"
    "$<$<CONFIG:Release>:-O2>"
)
target_link_options(m2_compile_options INTERFACE
    "$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined>"
)

# ─────────── Library: m2_world_model_lib ───────────
add_library(${PROJECT_NAME}_lib
  src/error.cpp
  src/coord_transform.cpp
  src/cpa_tcpa_calculator.cpp
  src/encounter_classifier.cpp
  src/track_buffer.cpp
  src/view_health_monitor.cpp
  src/world_state_aggregator.cpp
  src/enc_loader.cpp
  src/world_model_node.cpp
)

target_include_directories(${PROJECT_NAME}_lib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME}_lib
    Eigen3::Eigen
    GeographicLib::GeographicLib
    Boost::headers
    spdlog::spdlog
    yaml-cpp::yaml-cpp
    m2_compile_options
)

ament_target_dependencies(${PROJECT_NAME}_lib
    rclcpp
    builtin_interfaces
    geometry_msgs
    geographic_msgs
    l3_msgs
    l3_external_msgs
)

# ─────────── Executable ───────────
add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} ${PROJECT_NAME}_lib m2_compile_options)
ament_target_dependencies(${PROJECT_NAME} rclcpp)

# ─────────── Install ───────────
install(TARGETS ${PROJECT_NAME} ${PROJECT_NAME}_lib m2_compile_options
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
  INCLUDES DESTINATION include)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})

# ─────────── Tests ───────────
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)
  ament_lint_auto_find_test_dependencies()
  add_subdirectory(test)
endif()

ament_export_include_directories(include)
ament_export_libraries(${PROJECT_NAME}_lib)
ament_export_dependencies(rclcpp l3_msgs l3_external_msgs Eigen3 GeographicLib Boost spdlog)

ament_package()
