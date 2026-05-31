cmake_minimum_required(VERSION 3.22)
project(shell_b_harness VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable keeping link paths in RPATH for install targets
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Enable CMAKE_INSTALL_RPATH_USE_LINK_PATH to preserve dynamic lib paths (like casadi) in install
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(ament_index_cpp REQUIRED)

list(APPEND CMAKE_MODULE_PATH "/usr/share/cmake/geographiclib")
find_package(GeographicLib REQUIRED)
if(NOT TARGET GeographicLib::GeographicLib)
  add_library(GeographicLib::GeographicLib UNKNOWN IMPORTED)
  set_target_properties(GeographicLib::GeographicLib PROPERTIES
    IMPORTED_LOCATION "${GeographicLib_LIBRARIES}"
    INTERFACE_INCLUDE_DIRECTORIES "${GeographicLib_INCLUDE_DIRS}")
endif()

set(casadi_DIR "/usr/local/lib/python3.10/dist-packages/casadi/cmake")
find_package(casadi REQUIRED)
get_target_property(_casadi_iface_defs casadi::casadi INTERFACE_COMPILE_DEFINITIONS)
if(_casadi_iface_defs)
  list(REMOVE_ITEM _casadi_iface_defs "_GLIBCXX_USE_CXX11_ABI=0")
  set_target_properties(casadi::casadi PROPERTIES
    INTERFACE_COMPILE_DEFINITIONS "${_casadi_iface_defs}")
endif()

find_package(rcpputils REQUIRED)
get_target_property(_rcpputils_include_dirs rcpputils::rcpputils INTERFACE_INCLUDE_DIRECTORIES)
if(NOT _rcpputils_include_dirs)
  set(_rcpputils_include_dirs "${rcpputils_INCLUDE_DIRS}")
endif()

find_package(m1_odd_envelope_manager REQUIRED)
find_package(m2_world_model REQUIRED)
find_package(m3_mission_manager REQUIRED)
find_package(m4_behavior_arbiter REQUIRED)
find_package(m5_tactical_planner REQUIRED)
find_package(m6_colregs_reasoner REQUIRED)
find_package(m8_hmi_transparency_bridge REQUIRED)

# Extra libraries needed by M5, M8 or others
find_package(Eigen3 REQUIRED)
find_package(Boost REQUIRED COMPONENTS headers)
find_package(spdlog REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(casadi REQUIRED)

# Define executable
add_executable(doer_composition
  src/composition.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../l3_tdl_kernel/m8_hmi_transparency_bridge/src/hmi_transparency_bridge_node.cpp
)

# Header include paths
target_include_directories(doer_composition PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../l3_tdl_kernel/m1_odd_envelope_manager/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../l3_tdl_kernel/m2_world_model/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../l3_tdl_kernel/m3_mission_manager/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../l3_tdl_kernel/m4_behavior_arbiter/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../l3_tdl_kernel/m5_tactical_planner/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../l3_tdl_kernel/m6_colregs_reasoner/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../l3_tdl_kernel/m8_hmi_transparency_bridge/include
  ${_rcpputils_include_dirs}/rcpputils
)

find_library(M1_LIB m1_odd_envelope_manager_lib REQUIRED)
find_library(M2_LIB m2_world_model_lib REQUIRED)
find_library(M3_LIB m3_mission_manager_lib REQUIRED)
find_library(M4_NODE_LIB m4_node_lib REQUIRED)
find_library(M4_CORE_LIB m4_core REQUIRED)
find_library(M5_LIB m5_shared_lib REQUIRED)
find_library(M6_LIB m6_colregs_reasoner_lib REQUIRED)
find_library(M8_LIB m8_core REQUIRED)

# Direct library links to ensure symbols from all DOER modules are resolved
target_link_libraries(doer_composition
  ${M1_LIB}
  ${M2_LIB}
  ${M3_LIB}
  ${M4_NODE_LIB}
  ${M4_CORE_LIB}
  ${M5_LIB}
  ${M6_LIB}
  ${M8_LIB}
  Eigen3::Eigen
  Boost::headers
  spdlog::spdlog
  yaml-cpp::yaml-cpp
  nlohmann_json::nlohmann_json
  casadi::casadi
)

# Ament dependencies
ament_target_dependencies(doer_composition
  rclcpp
  ament_index_cpp
  rcpputils
  m1_odd_envelope_manager
  m2_world_model
  m3_mission_manager
  m4_behavior_arbiter
  m5_tactical_planner
  m6_colregs_reasoner
  m8_hmi_transparency_bridge
)

# Strip wrong ABI define again at the end, in case ament_target_dependencies / downstream find_package calls re-import it
get_target_property(_casadi_iface_defs_end casadi::casadi INTERFACE_COMPILE_DEFINITIONS)
if(_casadi_iface_defs_end)
  list(REMOVE_ITEM _casadi_iface_defs_end "_GLIBCXX_USE_CXX11_ABI=0")
  set_target_properties(casadi::casadi PROPERTIES
    INTERFACE_COMPILE_DEFINITIONS "${_casadi_iface_defs_end}")
endif()

install(TARGETS doer_composition
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

ament_get_python_install_dir(PYTHON_INSTALL_DIR)
install(DIRECTORY shell_b_harness DESTINATION ${PYTHON_INSTALL_DIR})

ament_environment_hooks(
  "environment/pythonpath.sh"
  "environment/pythonpath.dsv"
)

ament_package()
