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
find_package(m1_odd_envelope_manager REQUIRED)
find_package(m2_world_model REQUIRED)
find_package(m3_mission_manager REQUIRED)
find_package(m4_behavior_arbiter REQUIRED)
find_package(m5_tactical_planner REQUIRED)
find_package(m6_colregs_reasoner REQUIRED)
find_package(m7_safety_supervisor REQUIRED)
find_package(m8_hmi_transparency_bridge REQUIRED)

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

# INT-005+006: Hardware override + dual interface
ament_add_gtest(test_int_005_006 test_int_005_006_override_dual_interface.cpp)
ament_target_dependencies(test_int_005_006 rclcpp l3_msgs l3_external_msgs)

# INT-007: ASDR full
ament_add_gtest(test_int_007 test_int_007_asdr_full.cpp)
ament_target_dependencies(test_int_007 rclcpp l3_msgs l3_external_msgs)

# INT-008: M8 SAT/ToR
ament_add_gtest(test_int_008 test_int_008_m8_sat_tor.cpp)
ament_target_dependencies(test_int_008 rclcpp l3_msgs l3_external_msgs)

ament_package()
