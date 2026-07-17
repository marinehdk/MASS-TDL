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
  -fno-fast-math -ffp-contract=off
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
find_package(std_msgs REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)
find_package(ship_interfaces REQUIRED)
find_package(l3_risk_model REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(spdlog REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(Eigen3 REQUIRED NO_MODULE)
find_package(ament_index_cpp REQUIRED)

# CasADi — optional, required for Mid-MPC (Task 2.x+).
# When unavailable, Mid-MPC components and constraint_compiler are excluded.
# Set -DM5_USE_CASADI=OFF to skip CasADi-dependent targets.
option(M5_USE_CASADI "Build Mid-MPC and constraint_compiler (requires CasADi/IPOPT)" ON)
if(M5_USE_CASADI)
  find_package(casadi 3.7.2 EXACT QUIET)
  if(NOT casadi_FOUND)
    message(STATUS "M5: CasADi 3.7.2 not found — Mid-MPC and constraint_compiler will be skipped")
    set(M5_HAS_CASADI OFF)
  else()
    set(M5_HAS_CASADI ON)
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
  endif()
else()
  set(M5_HAS_CASADI OFF)
  message(STATUS "M5: CasADi disabled by option — Mid-MPC and constraint_compiler will be skipped")
endif()

# Empty default for M5_ACADOS_CONSUMER_LIBS: stays empty under M5_USE_ACADOS=OFF
# so the consumer link lists below (node + m5_add_gtest macro) are no-ops.
# Populated under M5_HAS_ACADOS in the option block below with the acatos solver
# .so + the imported acados target. MUST be defined BEFORE the option block so
# the list(APPEND ...) inside it is not clobbered; consumers (node, tests) read
# it AFTER the option block has run.
set(M5_ACADOS_CONSUMER_LIBS)

# acados — optional, P1b-1b PRODUCTION backend for MidMpc (parallel to IPOPT).
# Default OFF: IPOPT (M5_USE_CASADI) is the production solve path. acados is a
# parallel backend exercised only inside the sil_nodes container
# (docker/sil_nodes.Dockerfile:95-149 installs acados v0.4.4 + t_renderer +
# acados_template). ON requires both the acados C lib AND the Python codegen
# toolchain (gen_mid_mpc_acados.py runs at build time). No ABI patch here — the
# Dockerfile forces -D_GLIBCXX_USE_CXX11_ABI=1 at the acados build, so it is
# already aligned with casadi (line 78-79) and the workspace.
#
# What ON enables (see codegen block after add_library(m5_shared_lib)):
#   1. Runs gen_mid_mpc_acados.py (CasADi SX codegen -> C sources + Makefile).
#   2. Builds the generated solver shared lib (libacados_ocp_solver_m5_mid_mpc_acados.so)
#      via its Makefile `ocp_shared_lib` target.
#   3. Links that .so into m5_shared_lib + exposes its include dir.
#   4. Defines M5_USE_ACADOS=1 so Task 17's solver wrapper (#ifdef) compiles in.
# The formulation source (mid_mpc_acados_formulation.cpp) is CasADi-only and is
# already built under M5_HAS_CASADI (line 160); it is NOT re-added here.
option(M5_USE_ACADOS "Build production acados M5 MidMpc backend (P1b-1b)" OFF)
set(M5_HAS_ACADOS OFF)
if(M5_USE_ACADOS)
  find_package(acados QUIET REQUIRED)
  # Python3 Interpreter is needed to run gen_mid_mpc_acados.py at build time.
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(M5_HAS_ACADOS ON)
  message(STATUS "M5: acados production backend enabled (P1b-1b) — codegen + "
                 "solver lib will be built and linked into m5_shared_lib")

  # Absolute paths to the codegen outputs (built by the custom_command block
  # after add_library(m5_shared_lib)). Defined here so they can be appended to
  # M5_LINK_LIBS and flow through the EXISTING PLAIN target_link_libraries call
  # at line ~190 (the plain signature is already in use via ament_target_deps +
  # that call, so we cannot introduce a keyword-signature target_link_libraries
  # on the same target — CMake forbids mixing the two). `acados` is the IMPORTED
  # target exported by acadosConfig.cmake (transitively links hpipm + blasfeo).
  set(ACADOS_BACKEND_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test/external/acados_backend)
  set(ACADOS_GEN_DIR     ${ACADOS_BACKEND_DIR}/c_generated_code)
  set(M5_ACADOS_SOLVER_HEADER ${ACADOS_GEN_DIR}/acados_solver_m5_mid_mpc_acados.h)
  set(M5_ACADOS_SOLVER_LIB
      ${ACADOS_GEN_DIR}/libacados_ocp_solver_m5_mid_mpc_acados.so)
  list(APPEND M5_LINK_LIBS ${M5_ACADOS_SOLVER_LIB} acados)
  # m5_shared_lib is STATIC: its link deps do NOT auto-propagate to consumers
  # (node + test executables) under the plain-signature target_link_libraries
  # used throughout this file. Task 17's wrapper references acatos symbols that
  # must resolve at the FINAL executable link, so every consumer of
  # m5_shared_lib must also link the acatos solver .so + the imported acados
  # target. Collected here as M5_ACADOS_CONSUMER_LIBS and appended to each
  # consumer's target_link_libraries call (node + m5_add_gtest macro).
  list(APPEND M5_ACADOS_CONSUMER_LIBS ${M5_ACADOS_SOLVER_LIB} acados)
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
set(M5_SHARED_SOURCES
  src/avoidance_route_hash.cpp
  src/committed_route/committed_route.cpp
  src/gnc_preflight.cpp
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
  src/shared/relative_track.cpp
  # --- bc_mpc/ ---
  src/bc_mpc/bc_mpc_branch_formulation.cpp
  src/bc_mpc/bc_mpc_collision_detector.cpp
  src/bc_mpc/bc_mpc_solver.cpp
  src/bc_mpc/bc_mpc_override_generator.cpp
  # --- mid_mpc/ (CasADi-free) ---
  src/mid_mpc/nomoto_fallback.cpp
  src/mid_mpc/degraded_candidate_adapter.cpp
  # P2 T4 (VR-07b): per-stage t_b closest-point (project_to_segment on the F1
  # seed). CasADi-free free function; the acatos solver .cpp calls it, and the
  # unit test links it directly (no acatos .so dependency — testable in
  # isolation from the stale codegen until T5 regenerates NP=143).
  src/mid_mpc/mid_mpc_per_stage_tb.cpp
  # --- tail_builder/ ---
  src/tail_builder/tail_builder.cpp
)

set(M5_LINK_LIBS
  spdlog::spdlog
  yaml-cpp::yaml-cpp
  Eigen3::Eigen
  Boost::headers
  l3_risk_model::l3_risk_model
)

if(M5_HAS_CASADI)
  list(APPEND M5_SHARED_SOURCES
    # --- shared/ (CasADi-dependent) ---
    src/shared/constraint_compiler.cpp
    # --- mid_mpc/ ---
    src/mid_mpc/mid_mpc_nlp_formulation.cpp
    src/mid_mpc/mid_mpc_solver.cpp
    src/mid_mpc/mid_mpc_waypoint_generator.cpp
    # P1b-1b Task 15: production acados OCP symbol graph (CasADi MX). Built
    # unconditionally under M5_HAS_CASADI; the acados codegen script consumes
    # it but the .cpp itself only links CasADi (no acados link dep here).
    src/mid_mpc/mid_mpc_acados_formulation.cpp
  )
  list(APPEND M5_LINK_LIBS casadi::casadi)
endif()

add_library(m5_shared_lib STATIC ${M5_SHARED_SOURCES})

target_include_directories(m5_shared_lib
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

ament_target_dependencies(m5_shared_lib
  rclcpp
  l3_msgs
  l3_external_msgs
  ship_interfaces
  builtin_interfaces
)

target_link_libraries(m5_shared_lib ${M5_LINK_LIBS})

# ---------------------------------------------------------------------------
# P1b-1b Task 16: acados codegen + generated solver lib (M5_HAS_ACADOS only).
# ---------------------------------------------------------------------------
# When the production acados backend is enabled (M5_USE_ACADOS=ON), run the
# CasADi-SX codegen (gen_mid_mpc_acados.py) at BUILD time to emit the C solver
# sources + Makefile into c_generated_code/, then build the generated solver
# shared lib via its Makefile `ocp_shared_lib` target, and link it into
# m5_shared_lib. The generated header dir is exposed so Task 17's solver
# wrapper (mid_mpc_acados_solver.cpp, #ifdef M5_USE_ACADOS) can include
# acados_solver_m5_mid_mpc_acados.h. M5_USE_ACADOS=1 is defined PUBLIC so the
# Task 17 wrapper compiles in wherever m5_shared_lib is consumed.
#
# LINK wiring: the generated .so + the imported `acados` target are appended to
# M5_LINK_LIBS in the option block above, so they flow through the EXISTING
# plain-signature target_link_libraries at line ~204. We do NOT add a separate
# keyword-signature target_link_libraries here: m5_shared_lib already uses the
# plain signature (ament_target_dependencies + line 204), and CMake forbids
# mixing plain and keyword signatures on the same target.
#
# Why two add_custom_command + one add_custom_target:
#   - custom_command #1 (OUTPUT: generated .h): runs the Python codegen.
#   - custom_command #2 (OUTPUT: generated .so): runs `make ocp_shared_lib`.
#   - custom_target (ALL): ties the .so into the build graph; add_dependencies
#     forces m5_shared_lib to wait for the .so before linking.
# The Makefile (acados codegen output) hardcodes INCLUDE_PATH=/usr/local/include
# and LIB_PATH=/usr/local/lib and links -lacados -lhpipm -lblasfeo (verified in
# c_generated_code/Makefile:75-99); the Dockerfile installs all three to those
# exact paths (sil_nodes.Dockerfile:95-149). ACADOS_SOURCE_DIR=/usr/local is set
# in the image so the codegen include path resolves the installed headers.
# ACADOS_BACKEND_DIR / ACADOS_GEN_DIR / M5_ACADOS_SOLVER_HEADER / LIB are set in
# the option block above (they are also needed to populate M5_LINK_LIBS there).
if(M5_HAS_ACADOS)
  # codegen: gen_mid_mpc_acados.py -> C sources + Makefile + json (the header is
  # the canonical "codegen complete" sentinel; it is emitted alongside the .c).
  add_custom_command(
    OUTPUT  ${M5_ACADOS_SOLVER_HEADER}
    COMMAND ${Python3_EXECUTABLE} gen_mid_mpc_acados.py
    WORKING_DIRECTORY ${ACADOS_BACKEND_DIR}
    DEPENDS ${ACADOS_BACKEND_DIR}/gen_mid_mpc_acados.py
    COMMENT "[acados] codegen m5_mid_mpc_acados solver (CasADi SX -> C)"
    VERBATIM)

  # build generated solver shared lib via its Makefile (Makefile:119-121
  # `ocp_shared_lib` -> libacados_ocp_solver_m5_mid_mpc_acados.so).
  add_custom_command(
    OUTPUT  ${M5_ACADOS_SOLVER_LIB}
    COMMAND ${CMAKE_MAKE_PROGRAM} -C ${ACADOS_GEN_DIR} ocp_shared_lib
    DEPENDS ${M5_ACADOS_SOLVER_HEADER}
    COMMENT "[acados] build generated solver shared lib (ocp_shared_lib)")

  add_custom_target(m5_acados_solver_lib ALL
    DEPENDS ${M5_ACADOS_SOLVER_LIB})

  # Force m5_shared_lib to wait for the generated .so before linking (the .so
  # path is on M5_LINK_LIBS, but add_dependencies makes the build-order
  # dependency explicit so the linker never sees a stale/absent file).
  add_dependencies(m5_shared_lib m5_acados_solver_lib)

  # Generated solver header (Task 17 wrapper includes it) + the acados/blasfeo/
  # hpipm include dirs the generated header chain transitively #includes
  # (matches the Makefile CPPFLAGS, Makefile:79-82). Keyword signature here is
  # fine: target_include_directories has no plain variant, so no conflict with
  # the plain target_link_libraries on the same target.
  target_include_directories(m5_shared_lib PUBLIC
    ${ACADOS_GEN_DIR}
    /usr/local/include/acados
    /usr/local/include/blasfeo/include
    /usr/local/include/hpipm/include)

  # Public so Task 17's #ifdef M5_USE_ACADOS wrapper compiles in for every
  # consumer of m5_shared_lib.
  target_compile_definitions(m5_shared_lib PUBLIC M5_USE_ACADOS=1)

  # P1b-1b Task 17: the acados solver wrapper. Only added under M5_HAS_ACADOS —
  # it includes acados_solver_m5_mid_mpc_acados.h (T16 codegen output), which is
  # absent when codegen did not run. The include dirs + link deps for it are
  # wired by the T16 block above (target_include_directories PUBLIC +
  # M5_LINK_LIBS). IPOPT-only sources (mid_mpc_solver.cpp, the NLP formulation)
  # stay in the M5_HAS_CASADI block; this wrapper is additive. Uses
  # target_sources (NOT list APPEND on M5_SHARED_SOURCES) because add_library
  # above already snapshotted that list — only target_sources retroactively adds
  # to an existing target.
  target_sources(m5_shared_lib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mid_mpc/mid_mpc_acados_solver.cpp)
endif()

# ---------------------------------------------------------------------------
# m5_mid_mpc_node executable (CasADi-dependent)
# ---------------------------------------------------------------------------
if(M5_HAS_CASADI)
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
    std_msgs
    l3_msgs
    l3_external_msgs
    ship_interfaces
    builtin_interfaces
    ament_index_cpp
  )
  target_link_libraries(m5_mid_mpc_node
    m5_shared_lib
    spdlog::spdlog
    ${M5_ACADOS_CONSUMER_LIBS}
  )
endif()

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
  std_msgs
  l3_msgs
  l3_external_msgs
  builtin_interfaces
)
  target_link_libraries(m5_bc_mpc_node
    m5_shared_lib
    spdlog::spdlog
    Eigen3::Eigen
    ${M5_ACADOS_CONSUMER_LIBS}
  )

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS m5_bc_mpc_node
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)
if(M5_HAS_CASADI)
  install(TARGETS m5_mid_mpc_node
    RUNTIME DESTINATION lib/${PROJECT_NAME}
  )
endif()
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
  # Auto-detect by ROS_DISTRO env so this works under both Jazzy and Humble.
  if(DEFINED ENV{ROS_DISTRO})
    set(_M5_GTEST_SRC_DIR "/opt/ros/$ENV{ROS_DISTRO}/src/gtest_vendor")
  else()
    set(_M5_GTEST_SRC_DIR "/opt/ros/jazzy/src/gtest_vendor")
  endif()
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
      ${M5_ACADOS_CONSUMER_LIBS}
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

  # P2 T1: relative_track project_to_segment pure function (Eriksen t_b, VR-07b).
  # No CasADi/acados dependency — pure point-to-segment projection.
  m5_add_gtest(test_relative_track
    test/unit/test_relative_track.cpp
    spdlog::spdlog Eigen3::Eigen yaml-cpp::yaml-cpp
  )

  # P2 T2: huber_cost pure function (VR-07b). Header-only inline Huber loss:
  # numeric oracle for T3's acados MX Huber helper + T6's parity/benchmark.
  # Header-only — NO .cpp added to M5_SHARED_SOURCES.
  m5_add_gtest(test_huber_cost
    test/unit/test_huber_cost.cpp
    spdlog::spdlog Eigen3::Eigen yaml-cpp::yaml-cpp
  )

  # P2 T4: per-stage t_b closest-point computation (VR-07b). CasADi-free free
  # function (compute_per_stage_tb) the acatos solver .cpp calls to fill the
  # per-stage tb_x/tb_y slots via project_to_segment on the F1 seed. The test
  # cross-checks the packed tb against the T1 project_to_segment ORACLE directly
  # (no acatos .so dependency — the .so is stale NP=141 until T5 regenerates
  # NP=143; the full solver test PASS is deferred to T5/T6, but this test proves
  # the tb computation itself is correct in isolation).
  m5_add_gtest(test_mid_mpc_per_stage_tb
    test/unit/test_mid_mpc_per_stage_tb.cpp
    spdlog::spdlog Eigen3::Eigen yaml-cpp::yaml-cpp
  )

  if(M5_HAS_CASADI)
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

    # ── P1b-1b Task 15: production acados OCP symbol-graph (CasADi MX) ────────
    # Dimension/parameter-partition contract test for the acados formulation.
    # Depends on CasADi (MX graph) but NOT on acados being installed; the symbol
    # graph is buildable from CasADi alone (acados codegen is verified separately
    # in the container via test/external/acados_backend/gen_mid_mpc_acados.py).
    # Gated on M5_HAS_CASADI (not M5_HAS_ACADOS) so the contract test runs in
    # both acados-OFF and acados-ON builds.
    m5_add_gtest(test_mid_mpc_acados_formulation
      test/unit/test_mid_mpc_acados_formulation.cpp
      spdlog::spdlog Eigen3::Eigen yaml-cpp::yaml-cpp casadi::casadi
    )

    # ── Task 2.2: Mid-MPC solver integration tests (real IPOPT solves) ─────────
    m5_add_gtest(test_mid_mpc_solver
      test/unit/test_mid_mpc_solver.cpp
      spdlog::spdlog Eigen3::Eigen yaml-cpp::yaml-cpp casadi::casadi
    )

    # Spec v2.2 §4.7 (D2): MidMpcInput.speed_gap_infeasible flag + compute fn.
    m5_add_gtest(test_mid_mpc_types
      test/unit/test_mid_mpc_types.cpp
    )

    # ── Slice R1: route-frame J_route dimensionless + COLREG dominance ─────────
    m5_add_gtest(test_mid_mpc_route_cost
      test/unit/test_mid_mpc_route_cost.cpp
      spdlog::spdlog Eigen3::Eigen yaml-cpp::yaml-cpp casadi::casadi
    )

    # ── Slice N1: NLP row registry per-class lbg/ubg (spec §3.8) ───────────────
    m5_add_gtest(test_row_registry
      test/unit/test_row_registry.cpp
      casadi::casadi
    )

    # ── Slice T1: terminal constraints + smooth J_terminal (spec §5.4/§5.5) ────
    m5_add_gtest(test_mid_mpc_terminal
      test/unit/test_mid_mpc_terminal.cpp
      spdlog::spdlog Eigen3::Eigen yaml-cpp::yaml-cpp casadi::casadi
    )

    # ── Slice C1: continuity H_commit prefix equality + reprojection (spec §6) ─
    m5_add_gtest(test_mid_mpc_continuity
      test/unit/test_mid_mpc_continuity.cpp
      spdlog::spdlog Eigen3::Eigen yaml-cpp::yaml-cpp casadi::casadi
    )

    # ── Slice D1: COLREG direction + min_alt internalization (spec §7.1) ─────────
    m5_add_gtest(test_mid_mpc_direction
      test/unit/test_mid_mpc_direction.cpp
      spdlog::spdlog Eigen3::Eigen yaml-cpp::yaml-cpp casadi::casadi
    )

    # ── Slice R1 review round-2: route-frame active-leg geometry (Critical 3) ──
    m5_add_gtest(test_mid_mpc_route_frame
      test/unit/test_mid_mpc_route_frame.cpp
      spdlog::spdlog
    )

    # ── Task 2.3: Mid-MPC waypoint generator unit tests ────────────────────────
    m5_add_gtest(test_mid_mpc_waypoint_generator
      test/unit/test_mid_mpc_waypoint_generator.cpp
      spdlog::spdlog yaml-cpp::yaml-cpp
    )

    # ── Slice B: Mid-MPC tail-gate and stand-on acceptance tests ───────────────
    m5_add_gtest(test_midmpc_tail_gate
      test/unit/test_midmpc_tail_gate.cpp
      spdlog::spdlog yaml-cpp::yaml-cpp
    )
    m5_add_gtest(test_heading_bounds
      test/unit/test_heading_bounds.cpp
      spdlog::spdlog yaml-cpp::yaml-cpp
    )
    m5_add_gtest(test_stand_on_reject
      test/unit/test_stand_on_reject.cpp
      spdlog::spdlog yaml-cpp::yaml-cpp
    )
  endif()

  # ── Slice C: Terminal TailBuilder library ─────────────────────────────────
  m5_add_gtest(test_tail_builder
    test/unit/test_tail_builder.cpp
  )

  # ── Slice D: CommittedAvoidanceRoute manager ──────────────────────────────
  m5_add_gtest(test_committed_route
    test/unit/test_committed_route.cpp
  )

  # ── M1 review High-4/High-5: along-track frozen_prefix_count pure function ──
  m5_add_gtest(test_committed_candidate_geometry
    test/unit/test_committed_candidate_geometry.cpp
  )

  # ── Slice E: degraded fallback candidates ─────────────────────────────────
  m5_add_gtest(test_degraded_candidate_adapter
    test/unit/test_degraded_candidate_adapter.cpp
  )

  # ── Slice F: GNC feasibility preflight ──────────────────────────────────────
  m5_add_gtest(test_gnc_preflight
    test/unit/test_gnc_preflight.cpp
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

  # ── DEMO-2 P0: Nomoto 1st-order fallback solver (CasADi-free) ────────────────
  m5_add_gtest(test_nomoto_fallback
    test/unit/test_nomoto_fallback.cpp
    spdlog::spdlog Eigen3::Eigen yaml-cpp::yaml-cpp
  )
  target_compile_definitions(test_nomoto_fallback
    PRIVATE
      "M5_TEST_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures\""
  )

  # ── DEMO-2 P2: Geometric fallback turn magnitude from M6 min-alteration ────
  m5_add_gtest(test_geometric_fallback
    test/unit/test_geometric_fallback.cpp
  )

  # ── Track A A3: M5 avoidance waypoint generation (GNC feasibility) ─────────
  # Pure C++ geometry (header-only), no CasADi/rclpy deps. Verifies the
  # generated waypoint string satisfies the GNC active_route_manager gate.
  m5_add_gtest(test_avoidance_waypoint_gen
    test/unit/test_avoidance_waypoint_gen.cpp
  )

  # Slice A: M5 AvoidancePlan committed-route message contract.
  m5_add_gtest(test_avoidance_plan_contract
    test/unit/test_avoidance_plan_contract.cpp
  )

  # W4-A: target-track vs corridor clearance gate (pure geometry, no ROS).
  m5_add_gtest(test_target_corridor_clearance
    test/unit/test_target_corridor_clearance.cpp
  )

  # CasADi loads its IPOPT/MA57/HSL backends via dlopen(RTLD_DEEPBIND).
  # RTLD_DEEPBIND is incompatible with AddressSanitizer: the loaded DSO sees
  # its own (un-instrumented) libc symbols, causing ASAN to crash on
  # deallocation of memory it did not tag.
  # Strip ASAN from the targets that link against casadi in Debug builds.
  if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND M5_HAS_CASADI)
    foreach(_no_asan_tgt
        test_constraint_compiler
        test_mid_mpc_nlp_formulation
        test_mid_mpc_solver
        test_mid_mpc_continuity)
      target_compile_options(${_no_asan_tgt} PRIVATE
        -fno-sanitize=address -fno-sanitize=undefined)
      target_link_options(${_no_asan_tgt} PRIVATE
        -fno-sanitize=address -fno-sanitize=undefined)
    endforeach()
  endif()

  # ── P1b-1b Task 17: production acados MidMpc solver wrapper tests. ──────────
  # End-to-end: build the formulation + the acados wrapper (links the generated
  # solver .so via m5_shared_lib), run the standard straight-line scenario,
  # assert the SAME MidMpcSolution output contract as IPOPT (psi/u/x/y finite,
  # cost/duration/slack populated, status Converged or status-4+solver-moved).
  # Gated on M5_HAS_ACADOS: the wrapper's #include of the generated solver
  # header only resolves when T16 codegen ran. OFF builds skip this test (the
  # dispatch branch compiles to nothing under OFF, so the IPOPT test suite
  # already covers the OFF path).
  if(M5_HAS_ACADOS)
    m5_add_gtest(test_mid_mpc_acados_solver
      test/unit/test_mid_mpc_acados_solver.cpp
      spdlog::spdlog Eigen3::Eigen yaml-cpp::yaml-cpp casadi::casadi
    )

    # ── P1b-1b Task 18: IPOPT ↔ acados output-contract parity test. ──────────
    # Backend-switch gate. Same MidMpcInput fed to BOTH backends; the test
    # proves the produced MidMpcSolution shapes are CONTRACT-compatible
    # (downstream M4/L4/tail_gate is agnostic to which backend produced the
    # trajectory). NOT bit-close (two physics differ per spec gate 3).
    # Gated on BOTH M5_HAS_CASADI AND M5_HAS_ACADOS (this block already sits
    # inside the M5_HAS_CASADI block, so the test compiles out under either
    # flag OFF). Under OFF the IPOPT-only test suite already covers the OFF
    # path; the parity test is meaningless without both backends present.
    m5_add_gtest(test_mid_mpc_acados_parity
      test/unit/test_mid_mpc_acados_parity.cpp
      spdlog::spdlog Eigen3::Eigen yaml-cpp::yaml-cpp casadi::casadi
    )
    # IPOPT loads its linear solver via dlopen (RTLD_DEEPBIND) — strip ASAN
    # in Debug builds (mirrors the test_mid_mpc_solver exception above).
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
      target_compile_options(test_mid_mpc_acados_parity PRIVATE
        -fno-sanitize=address -fno-sanitize=undefined)
      target_link_options(test_mid_mpc_acados_parity PRIVATE
        -fno-sanitize=address -fno-sanitize=undefined)
    endif()
  endif()

  # ── P1b-1c Task 19: Rule14 head-on benchmark runner (IPOPT vs acatos). ──────
  # Standalone executable (NOT a gtest). Same source compiled under BOTH
  # M5_USE_ACADOS=OFF (IPOPT kinematics) and =ON (acatos Path B double-
  # integrator); the #ifdef M5_USE_ACADOS branch in the runner picks the
  # backend. run_benchmark.sh orchestrates the two builds + runs + compare.
  # This target sits under M5_HAS_CASADI (NOT M5_HAS_ACADOS) so it builds in
  # BOTH the OFF (IPOPT) and ON (acatos) builds — m5_shared_lib propagates
  # M5_USE_ACADOS=1 PUBLIC under ON (T16), so the runner's #ifdef branch
  # compiles to the acatos wrapper + links the generated solver .so via
  # M5_ACADOS_CONSUMER_LIBS (empty under OFF); under OFF it compiles to the
  # IPOPT wrapper. NOT registered as a ctest — the benchmark gate is the
  # cross-build compare.py, not a single-build pass/fail.
  add_executable(rule14_bench_runner
    test/external/rule14_ho_benchmark/runner_rule14.cpp
  )
  target_link_libraries(rule14_bench_runner
    m5_shared_lib
    spdlog::spdlog Eigen3::Eigen yaml-cpp::yaml-cpp casadi::casadi
    ${M5_ACADOS_CONSUMER_LIBS}
  )
  # Scenario target distance — compile-time macro. Default 2000.0 (primary,
  # production-realistic mid-MPC early-detection envelope). run_benchmark.sh
  # rebuilds under -DSCENARIO_TARGET_DISTANCE_M=500 for the documented-limitation
  # datapoint. The CMake var RULE14_SCENARIO_TARGET_DISTANCE_M (if set) overrides
  # the default; this indirection keeps the macro out of the public compile
  # interface of m5_shared_lib (benchmark-local).
  if(DEFINED RULE14_SCENARIO_TARGET_DISTANCE_M)
    target_compile_definitions(rule14_bench_runner PRIVATE
      SCENARIO_TARGET_DISTANCE_M=${RULE14_SCENARIO_TARGET_DISTANCE_M})
  endif()
  # CasADi dlopen(RTLD_DEEPBIND) + ASAN incompatibility (mirrors the existing
  # CasADi test exceptions). Strip ASAN in Debug builds.
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(rule14_bench_runner PRIVATE
      -fno-sanitize=address -fno-sanitize=undefined)
    target_link_options(rule14_bench_runner PRIVATE
      -fno-sanitize=address -fno-sanitize=undefined)
  endif()
  install(TARGETS rule14_bench_runner
    DESTINATION lib/m5_tactical_planner)
endif()

ament_export_include_directories(include)
ament_export_libraries(m5_shared_lib)
if(M5_HAS_CASADI)
  ament_export_dependencies(rclcpp l3_msgs l3_external_msgs builtin_interfaces casadi Boost)
else()
  ament_export_dependencies(rclcpp l3_msgs l3_external_msgs builtin_interfaces Boost)
endif()
ament_package()
