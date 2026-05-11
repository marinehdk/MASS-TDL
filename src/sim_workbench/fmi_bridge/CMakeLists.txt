cmake_minimum_required(VERSION 3.16)
project(fmi_bridge)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)
find_package(pybind11 REQUIRED)
find_package(yaml-cpp REQUIRED)

# libcosim: system package or FetchContent fallback
find_package(cosim QUIET)
if(NOT cosim_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    libcosim
    GIT_REPOSITORY https://github.com/open-simulation-platform/libcosim.git
    GIT_TAG v0.9.0
  )
  FetchContent_MakeAvailable(libcosim)
endif()

# Core library
add_library(fmi_bridge_core SHARED
  src/libcosim_wrapper.cpp
  src/dds_fmu_bridge.cpp
  src/fmu_utils.cpp
)
target_include_directories(fmi_bridge_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(fmi_bridge_core PUBLIC rclcpp::rclcpp yaml-cpp)
if(cosim_FOUND)
  target_link_libraries(fmi_bridge_core PUBLIC cosim::cosim)
endif()
ament_target_dependencies(fmi_bridge_core l3_msgs l3_external_msgs)

# ROS2 node executable
add_executable(dds_fmu_node src/dds_fmu_node.cpp)
target_link_libraries(dds_fmu_node fmi_bridge_core)

# pybind11 module: FCBSimulator bindings
pybind11_add_module(fcb_mmg_py python/fcb_mmg_bindings.cpp)
target_link_libraries(fcb_mmg_py PRIVATE fcb_simulator::fcb_simulator_core)
target_include_directories(fcb_mmg_py PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../fcb_simulator/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../ship_sim_interfaces/include
)

install(TARGETS fmi_bridge_core dds_fmu_node fcb_mmg_py
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY config launch python
  DESTINATION share/${PROJECT_NAME}
)

ament_package()
