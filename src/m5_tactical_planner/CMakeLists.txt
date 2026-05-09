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
# Must NOT be STATIC-linked.
find_package(casadi 3.7.2 EXACT REQUIRED)

# PATCH: casadi-config.cmake (pip-installed 3.7.2) incorrectly forces
# _GLIBCXX_USE_CXX11_ABI=0 for GCC, but libcasadi.so.3.7 is compiled with
# the new C++11 ABI (verified via nm). Stripping the wrong INTERFACE define
# ensures uniform new-ABI compilation across our code and all ROS2 Jazzy deps.
get_target_property(_casadi_iface_defs casadi::casadi INTERFACE_COMPILE_DEFINITIONS)
if(_casadi_iface_defs)
  list(REMOVE_ITEM _casadi_iface_defs "_GLIBCXX_USE_CXX11_ABI=0")
  set_target_properties(casadi::casadi PROPERTIES
    INTERFACE_COMPILE_DEFINITIONS "${_casadi_iface_defs}")
endif()

# IPOPT — TODO(Task-2.x): Uncomment when mid_mpc/ sources are added.
# find_package(IPOPT 3.14.19 EXACT REQUIRED)

# GeographicLib — TODO(Task-2.x/3.x): Uncomment when geo conversions needed.
# find_package(GeographicLib 2.7 EXACT REQUIRED)

# Boost headers — used by ConstraintCompiler polygon decomposition.
find_package(Boost REQUIRED COMPONENTS headers)

# ---------------------------------------------------------------------------
# m5_shared_lib — shared library (Tasks 1.x + 2.x + 3.x)
# ---------------------------------------------------------------------------
# IPOPT note: CasADi loads its IPOPT backend dynamically at runtime via the
# libcasadi_nlpsol_ipopt plugin. Linking against `casadi` (already configured
# below) is sufficient; no separate find_package(IPOPT) is required for this
# Function-based API. If a future task uses IPOPT's C++ API directly, the
# commented find_package(IPOPT) block above must be re-enabled.
# ---------------------------------------------------------------------------
add_library(m5_shared_lib STATIC
  # --- common/ ---
  src/common/error.cpp
  src/common/types.cpp
  src/common/units.cpp
  src/common/time_alignment.cpp
  src/common/sha256.cpp
  # --- shared/ ---
  src/shared/capability_manifest.cpp
  src/shared/vessel_dynamics_model.cpp
  src/shared/cpa_calculator.cpp
  src/shared/trajectory_propagator.cpp
  src/shared/constraint_compiler.cpp
  # --- mid_mpc/ ---
  src/mid_mpc/mid_mpc_nlp_formulation.cpp
  src/mid_mpc/mid_mpc_solver.cpp
  src/mid_mpc/mid_mpc_waypoint_generator.cpp
  # --- bc_mpc/ ---
  src/bc_mpc/bc_mpc_branch_formulation.cpp
  src/bc_mpc/bc_mpc_collision_detector.cpp
  src/bc_mpc/bc_mpc_solver.cpp
  src/bc_mpc/bc_mpc_override_generator.cpp
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

target_link_libraries(m5_shared_lib
  spdlog::spdlog
  yaml-cpp::yaml-cpp
  Eigen3::Eigen
  casadi::casadi          # dynamic — LGPL-3.0 requirement (no STATIC linking)
  Boost::headers
)

# ---------------------------------------------------------------------------
# m5_mid_mpc_node executable
# ---------------------------------------------------------------------------
add_executable(m5_mid_mpc_node
  src/mid_mpc/mid_mpc_node.cpp
  src/mid_mpc/main_mid_mpc.cpp
)
target_include_directories(m5_mid_mpc_node
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
ament_target_dependencies(m5_mid_mpc_node
  rclcpp
  l3_msgs
  l3_external_msgs
  builtin_interfaces
)
target_link_libraries(m5_mid_mpc_node
  m5_shared_lib
  spdlog::spdlog
)

# ---------------------------------------------------------------------------
# m5_bc_mpc_node executable
# ---------------------------------------------------------------------------
add_executable(m5_bc_mpc_node
  src/bc_mpc/bc_mpc_node.cpp
  src/bc_mpc/main_bc_mpc.cpp
)
target_include_directories(m5_bc_mpc_node
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
ament_target_dependencies(m5_bc_mpc_node
  rclcpp
  l3_msgs
  l3_external_msgs
  builtin_interfaces
)
target_link_libraries(m5_bc_mpc_node
  m5_shared_lib
  spdlog::spdlog
  Eigen3::Eigen
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS m5_bc_mpc_node
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)
install(TARGETS m5_mid_mpc_node
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)
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
  ament_lint_auto_find_test_dependencies()

  # Recompile gtest FROM VENDOR SOURCE with new ABI explicitly forced.
  # Root cause: gtest_vendor was precompiled with _GLIBCXX_USE_CXX11_ABI=0
  # (old ABI). System libgtest.a uses new ABI but ament prefix-path ordering
  # causes the linker to find the OLD archive first. Recompiling from source
  # with the same flags as the rest of the workspace guarantees ABI parity.
  set(_M5_GTEST_SRC_DIR "/opt/ros/jazzy/src/gtest_vendor")
  find_package(Threads REQUIRED)

  add_library(m5_gtest STATIC
    ${_M5_GTEST_SRC_DIR}/src/gtest-all.cc
  )
  target_include_directories(m5_gtest SYSTEM PUBLIC
    ${_M5_GTEST_SRC_DIR}/include
    ${_M5_GTEST_SRC_DIR}
  )
  target_compile_definitions(m5_gtest PUBLIC GTEST_HAS_PTHREAD=1)
  target_link_libraries(m5_gtest PUBLIC Threads::Threads)
  # Suppress warnings from third-party gtest source (not our code)
  target_compile_options(m5_gtest PRIVATE
    -Wno-error -w
  )

  add_library(m5_gtest_main STATIC
    ${_M5_GTEST_SRC_DIR}/src/gtest_main.cc
  )
  target_include_directories(m5_gtest_main SYSTEM PUBLIC
    ${_M5_GTEST_SRC_DIR}/include
  )
  target_link_libraries(m5_gtest_main PUBLIC m5_gtest)
  target_compile_options(m5_gtest_main PRIVATE
    -Wno-error -w
  )

  # Helper: plain add_executable + locally-compiled gtest (new ABI).
  # ARGN = extra link libs (spdlog, Eigen3, yaml-cpp, casadi, …)
  macro(m5_add_gtest target source)
    add_executable(${target} ${source})
    ament_target_dependencies(${target}
      rclcpp l3_msgs l3_external_msgs builtin_interfaces
    )
    target_include_directories(${target}
      PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test
    )
    target_link_libraries(${target}
      m5_shared_lib
      m5_gtest_main
      ${ARGN}
    )
    # Pass --gtest_output so colcon test-result can parse the XML.
    set(_m5_result_file
      "${CMAKE_BINARY_DIR}/test_results/${PROJECT_NAME}/${target}.xml")
    ament_add_test(${target}
      COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${target}
        --gtest_output=xml:${_m5_result_file}
      TIMEOUT 120
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      RESULT_FILE ${_m5_result_file}
    )
  endmacro()

  m5_add_gtest(test_vessel_dynamics_model
    test/unit/test_vessel_dynamics_model.cpp
    spdlog::spdlog Eigen3::Eigen yaml-cpp::yaml-cpp
  )
  target_compile_definitions(test_vessel_dynamics_model
    PRIVATE
      "M5_TEST_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures\""
  )

  m5_add_gtest(test_constraint_compiler
    test/unit/test_constraint_compiler.cpp
    spdlog::spdlog Eigen3::Eigen yaml-cpp::yaml-cpp casadi::casadi Boost::headers
  )
  target_compile_definitions(test_constraint_compiler
    PRIVATE
      "M5_TEST_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures\""
  )

  # ── Task 2.1: Mid-MPC NLP formulation stub tests ────────────────────────
  m5_add_gtest(test_mid_mpc_nlp_formulation
    test/unit/test_mid_mpc_nlp_formulation.cpp
    spdlog::spdlog Eigen3::Eigen yaml-cpp::yaml-cpp casadi::casadi
  )

  # ── Task 2.2: Mid-MPC solver integration tests (real IPOPT solves) ─────────
  m5_add_gtest(test_mid_mpc_solver
    test/unit/test_mid_mpc_solver.cpp
    spdlog::spdlog Eigen3::Eigen yaml-cpp::yaml-cpp casadi::casadi
  )

  # ── Task 2.3: Mid-MPC waypoint generator unit tests ────────────────────────
  m5_add_gtest(test_mid_mpc_waypoint_generator
    test/unit/test_mid_mpc_waypoint_generator.cpp
    spdlog::spdlog yaml-cpp::yaml-cpp
  )

  # ── Task 3.1: BC-MPC branch formulation + collision detector unit tests ──────
  m5_add_gtest(test_bc_mpc_collision_detector
    test/unit/test_bc_mpc_collision_detector.cpp
    spdlog::spdlog Eigen3::Eigen yaml-cpp::yaml-cpp
  )

  # ── Task 3.2: BC-MPC solver + override generator unit tests ──────────────────
  m5_add_gtest(test_bc_mpc_solver
    test/unit/test_bc_mpc_solver.cpp
    spdlog::spdlog Eigen3::Eigen yaml-cpp::yaml-cpp
  )

  # ── Task 4.1: BC-MPC handover state machine + ASDR signature tests ──────────
  m5_add_gtest(test_bc_mpc_node_handover
    test/unit/test_bc_mpc_node_handover.cpp
    spdlog::spdlog Eigen3::Eigen yaml-cpp::yaml-cpp
  )

  # CasADi loads its IPOPT/MA57/HSL backends via dlopen(RTLD_DEEPBIND).
  # RTLD_DEEPBIND is incompatible with AddressSanitizer: the loaded DSO sees
  # its own (un-instrumented) libc symbols, causing ASAN to crash on
  # deallocation of memory it did not tag.
  # Strip ASAN from the three targets that link against casadi in Debug builds.
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    foreach(_no_asan_tgt
        test_constraint_compiler
        test_mid_mpc_nlp_formulation
        test_mid_mpc_solver)
      target_compile_options(${_no_asan_tgt} PRIVATE
        -fno-sanitize=address -fno-sanitize=undefined)
      target_link_options(${_no_asan_tgt} PRIVATE
        -fno-sanitize=address -fno-sanitize=undefined)
    endforeach()
  endif()
endif()

ament_export_include_directories(include)
ament_export_libraries(m5_shared_lib)
ament_export_dependencies(rclcpp l3_msgs l3_external_msgs builtin_interfaces casadi Boost)
ament_package()
