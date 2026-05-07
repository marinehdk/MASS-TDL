cmake_minimum_required(VERSION 3.22)
project(m5_tactical_planner VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# PATH-D compile options (MISRA C++:2023 — Doer side)
# Note: -fno-exceptions / -fno-rtti are NOT applied here; M5 is Doer (not Checker).
# M5 uses exceptions for YAML parsing boundary; yaml-cpp is a throwing API.
add_compile_options(
  -Wall -Wextra -Wpedantic -Werror
  -Wshadow -Wconversion -Wsign-conversion
  -Wcast-align -Wcast-qual -Wold-style-cast -Wzero-as-null-pointer-constant
  -Wnon-virtual-dtor -Woverloaded-virtual
  -Wnull-dereference -Wdouble-promotion
  -Wformat=2 -Wformat-security -Wmissing-declarations -Wundef -Wunused
  -fstack-protector-strong -D_FORTIFY_SOURCE=2
)

# Release: -O2 (NOT -O3 per PATH-D §2.2)
# Debug: ASan + UBSan
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(-O2)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-O0 -g -fsanitize=address -fsanitize=undefined)
  add_link_options(-fsanitize=address -fsanitize=undefined)
endif()

# ---------------------------------------------------------------------------
# Dependencies
# Note on EXACT versions: These pins ensure reproducible builds for certification.
# CasADi and IPOPT are included now (even unused in T1.1) so CMake validates
# the toolchain before Task 2.x (Mid-MPC solver) begins.
# ---------------------------------------------------------------------------
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(spdlog REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(Eigen3 REQUIRED NO_MODULE)

# CasADi — dynamic linking required (LGPL-3.0 compliance: §6 distribution clause).
# Must NOT be STATIC-linked. Task 2.x (Mid-MPC) will add casadi usages.
# TODO(Task-2.x): Uncomment when mid_mpc/ sources are added.
# find_package(casadi 3.7.2 EXACT REQUIRED)

# IPOPT — TODO(Task-2.x): Uncomment when mid_mpc/ sources are added.
# find_package(IPOPT 3.14.19 EXACT REQUIRED)

# GeographicLib — TODO(Task-2.x/3.x): Uncomment when geo conversions needed.
# find_package(GeographicLib 2.7 EXACT REQUIRED)

# Boost headers — TODO(Task-2.x): Uncomment when geometry structures needed.
# find_package(Boost 1.91 EXACT REQUIRED COMPONENTS headers)

# ---------------------------------------------------------------------------
# m5_shared_lib — Task 1.1 source files
# TODO(Task-2.x): Add mid_mpc/ source files here when implementing Mid-MPC
# TODO(Task-3.x): Add bc_mpc/ source files here when implementing BC-MPC
# ---------------------------------------------------------------------------
add_library(m5_shared_lib STATIC
  # --- common/ ---
  src/common/error.cpp
  src/common/types.cpp
  src/common/units.cpp
  src/common/time_alignment.cpp
  # --- shared/ ---
  src/shared/capability_manifest.cpp
  src/shared/vessel_dynamics_model.cpp
  src/shared/cpa_calculator.cpp
  src/shared/trajectory_propagator.cpp
  # TODO(Task-2.x): src/mid_mpc/constraint_compiler.cpp
  # TODO(Task-2.x): src/mid_mpc/mid_mpc_solver.cpp
  # TODO(Task-2.x): src/mid_mpc/mid_mpc_node.cpp
  # TODO(Task-3.x): src/bc_mpc/bc_mpc_evaluator.cpp
  # TODO(Task-3.x): src/bc_mpc/bc_mpc_node.cpp
)

target_include_directories(m5_shared_lib
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

ament_target_dependencies(m5_shared_lib
  rclcpp
  l3_msgs
  l3_external_msgs
  builtin_interfaces
)

target_link_libraries(m5_shared_lib PUBLIC
  spdlog::spdlog
  yaml-cpp::yaml-cpp
  Eigen3::Eigen
  # TODO(Task-2.x): casadi  (dynamic — LGPL-3.0 requirement)
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS m5_shared_lib
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})

# ---------------------------------------------------------------------------
# Tests (PATH-D ≥95% branch coverage target)
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)
  ament_lint_auto_find_test_dependencies()

  ament_add_gtest(test_vessel_dynamics_model
    test/unit/test_vessel_dynamics_model.cpp
  )
  target_link_libraries(test_vessel_dynamics_model
    m5_shared_lib
    spdlog::spdlog
    Eigen3::Eigen
  )
  ament_target_dependencies(test_vessel_dynamics_model
    rclcpp
    l3_msgs
    l3_external_msgs
    builtin_interfaces
  )
  target_include_directories(test_vessel_dynamics_model
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/test
  )
  # Inject fixture directory path so tests can locate YAML without install step
  target_compile_definitions(test_vessel_dynamics_model
    PRIVATE
      "M5_TEST_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures\""
  )

  # TODO(Task-2.x): ament_add_gtest(test_mid_mpc_solver ...)
  # TODO(Task-3.x): ament_add_gtest(test_bc_mpc_evaluator ...)
endif()

ament_export_include_directories(include)
ament_export_libraries(m5_shared_lib)
ament_export_dependencies(rclcpp l3_msgs l3_external_msgs builtin_interfaces)
ament_package()
