cmake_minimum_required(VERSION 3.22)
project(m4_behavior_arbiter)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(spdlog REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(builtin_interfaces REQUIRED)

# ── m4_core static library ──────────────────────────────────────────
set(M4_CORE_SOURCES
  src/ivp_solver.cpp
  src/ivp_combine.cpp
  src/ivp_function.cpp
  src/ivp_domain.cpp
  src/behavior_dictionary.cpp
  src/behavior_activation.cpp
  src/behavior_priority.cpp
)

add_library(m4_core STATIC ${M4_CORE_SOURCES})
target_include_directories(m4_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
)
ament_target_dependencies(m4_core
  rclcpp
  l3_msgs
  l3_external_msgs
  builtin_interfaces
)
target_link_libraries(m4_core
  Eigen3::Eigen
  spdlog::spdlog
  yaml-cpp
)
target_compile_features(m4_core PUBLIC cxx_std_17)

# ── m4_node_lib static library ──────────────────────────────────────
add_library(m4_node_lib STATIC src/behavior_arbiter_node.cpp)
target_include_directories(m4_node_lib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
)
ament_target_dependencies(m4_node_lib
  rclcpp
)
target_link_libraries(m4_node_lib
  m4_core
)
target_compile_features(m4_node_lib PUBLIC cxx_std_17)

# ── Executable ───────────────────────────────────────────────────────
add_executable(m4_behavior_arbiter src/main.cpp)
target_link_libraries(m4_behavior_arbiter PRIVATE m4_node_lib)

# ── Install targets ─────────────────────────────────────────────────
install(TARGETS
  m4_core
  m4_node_lib
  m4_behavior_arbiter
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY include/
  DESTINATION include/${PROJECT_NAME}
)

install(TARGETS m4_behavior_arbiter
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY launch/
  DESTINATION share/${PROJECT_NAME}/launch
)

install(DIRECTORY config/
  DESTINATION share/${PROJECT_NAME}/config
)

# ── Testing ─────────────────────────────────────────────────────────
if(BUILD_TESTING)
  add_subdirectory(test)
endif()

ament_package()
