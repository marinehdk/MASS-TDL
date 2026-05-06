cmake_minimum_required(VERSION 3.22)
project(m7_safety_supervisor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# PATH-S compile options (IEC 61508 SIL 2 — Checker side)
# -fno-exceptions -fno-rtti are mandatory for all M7 translation units
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

# ---------------------------------------------------------------------------
# Dependencies — whitelist only (ADR-001 independence matrix §3.1)
# Forbidden: casadi, Ipopt, GeographicLib, nlohmann_json, boost_geometry,
#            boost_property_tree
# ---------------------------------------------------------------------------
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(spdlog REQUIRED)
find_package(yaml-cpp REQUIRED)

# ---------------------------------------------------------------------------
# Static library — all M7 source files
# Subsequent tasks (T2–T8) will add more source files here
# ---------------------------------------------------------------------------
add_library(m7_lib STATIC
  src/common/error.cpp
  src/common/time_utils.cpp
  src/common/safety_constants.cpp
  src/sotif/assumption_monitor.cpp
)

target_include_directories(m7_lib
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

ament_target_dependencies(m7_lib
  rclcpp
  l3_msgs
  l3_external_msgs
  builtin_interfaces
)

target_link_libraries(m7_lib PUBLIC
  spdlog::spdlog
  yaml-cpp::yaml-cpp
)

# ---------------------------------------------------------------------------
# Doer-Checker independence verification (ADR-001)
# Runs as part of ALL target build to catch violations early
# ---------------------------------------------------------------------------
add_custom_target(m7_doer_checker_independence_check ALL
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/ci/check-doer-checker-independence.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..
  COMMENT "Verifying M7 Doer-Checker independence (ADR-001)"
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS m7_lib
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})

# ---------------------------------------------------------------------------
# Tests (PATH-S ≥95% branch coverage target)
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)
  ament_lint_auto_find_test_dependencies()

  ament_add_gtest(test_assumption_monitor
    test/unit/test_assumption_monitor.cpp
  )
  target_link_libraries(test_assumption_monitor
    m7_lib
    spdlog::spdlog
  )
  ament_target_dependencies(test_assumption_monitor
    rclcpp
    l3_msgs
    l3_external_msgs
    builtin_interfaces
  )
endif()

ament_export_include_directories(include)
ament_export_libraries(m7_lib)
ament_export_dependencies(rclcpp l3_msgs l3_external_msgs builtin_interfaces)
ament_package()
