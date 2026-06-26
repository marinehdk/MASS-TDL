cmake_minimum_required(VERSION 3.8)
project(sil_trace_adapter)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(sil_msgs REQUIRED)
find_package(l3_msgs REQUIRED)

add_library(sil_trace_translators SHARED
  src/translators.cpp
)
target_include_directories(sil_trace_translators PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(sil_trace_translators
  builtin_interfaces
  sil_msgs
  l3_msgs
)

add_executable(sil_trace_adapter_node src/sil_trace_adapter_node.cpp)
target_link_libraries(sil_trace_adapter_node sil_trace_translators)
ament_target_dependencies(sil_trace_adapter_node
  rclcpp
  builtin_interfaces
  sil_msgs
  l3_msgs
)

set_target_properties(sil_trace_translators PROPERTIES
  INSTALL_RPATH "$ORIGIN")
set_target_properties(sil_trace_adapter_node PROPERTIES
  INSTALL_RPATH "$ORIGIN/..")
install(TARGETS sil_trace_adapter_node sil_trace_translators
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_sil_trace_translators test/test_translators.cpp)
  target_link_libraries(test_sil_trace_translators sil_trace_translators)
  ament_target_dependencies(test_sil_trace_translators
    builtin_interfaces
    sil_msgs
    l3_msgs
  )
endif()

ament_package()
