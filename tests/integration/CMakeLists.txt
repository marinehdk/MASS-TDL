cmake_minimum_required(VERSION 3.22)
project(l3_integration_tests)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_gtest REQUIRED)
find_package(rclcpp REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)
# GeographicLib ships only a Find module (not a config package).
# Add the system module path so ament's transitive dep resolution from
# m2_world_model can locate FindGeographicLib.cmake even with
# CMAKE_FIND_PACKAGE_PREFER_CONFIG=ON (ROS2/ament default).
list(APPEND CMAKE_MODULE_PATH "/usr/share/cmake/geographiclib")
find_package(GeographicLib QUIET)
# TDD RED: module packages found but not yet linked — promoted to REQUIRED in TDD GREEN
# as each INT stub is implemented and headers are included
find_package(m1_odd_envelope_manager QUIET)
find_package(m2_world_model QUIET)
find_package(m3_mission_manager QUIET)
find_package(m5_tactical_planner QUIET)
find_package(m6_colregs_reasoner QUIET)
find_package(m4_behavior_arbiter QUIET)
find_package(m7_safety_supervisor QUIET)
find_package(m8_hmi_transparency_bridge QUIET)

# INT-001: M2→M6→M5 chain
ament_add_gtest(test_int_001 test_int_001_m2_m6_m5_chain.cpp)
ament_target_dependencies(test_int_001 rclcpp l3_msgs l3_external_msgs)

# INT-002: M3→L2 replan
ament_add_gtest(test_int_002 test_int_002_m3_l2_replan.cpp)
ament_target_dependencies(test_int_002 rclcpp l3_msgs l3_external_msgs)

# INT-003: Checker veto
ament_add_gtest(test_int_003 test_int_003_checker_veto.cpp)
ament_target_dependencies(test_int_003 rclcpp l3_msgs l3_external_msgs)

# INT-004: Reflex Arc override
ament_add_gtest(test_int_004 test_int_004_reflex_override.cpp)
ament_target_dependencies(test_int_004 rclcpp l3_msgs l3_external_msgs)

# INT-005+006 share the same hardware override / dual-interface fixture setup
ament_add_gtest(test_int_005_006 test_int_005_006_override_dual_interface.cpp)
ament_target_dependencies(test_int_005_006 rclcpp l3_msgs l3_external_msgs)

# INT-007: ASDR full
ament_add_gtest(test_int_007 test_int_007_asdr_full.cpp)
ament_target_dependencies(test_int_007 rclcpp l3_msgs l3_external_msgs)

# INT-008: M8 SAT/ToR
ament_add_gtest(test_int_008 test_int_008_m8_sat_tor.cpp)
ament_target_dependencies(test_int_008 rclcpp l3_msgs l3_external_msgs)

# M4 Node Lifecycle — node construction and interface advertisement
if(m4_behavior_arbiter_FOUND)
  ament_add_gtest(test_m4_node_lifecycle test_m4_node_lifecycle.cpp)
  ament_target_dependencies(test_m4_node_lifecycle rclcpp l3_msgs m4_behavior_arbiter)
else()
  message(WARNING "m4_behavior_arbiter not found — skipping test_m4_node_lifecycle")
endif()

# INT-009: M1+M6→M4 arbitration chain
ament_add_gtest(test_int_009 test_int_009_m4_arbitration_chain.cpp)
ament_target_dependencies(test_int_009 rclcpp l3_msgs)

ament_package()
