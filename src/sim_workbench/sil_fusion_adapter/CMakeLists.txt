cmake_minimum_required(VERSION 3.8)
project(sil_fusion_adapter)

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
find_package(geographic_msgs REQUIRED)
find_package(sil_msgs REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)

# Translator library — pure field-maps, no ROS node state. Linked by the node
# and the unit tests so the maps are exercised without spinning executors.
add_library(sil_fusion_translators SHARED
  src/translators.cpp
)
target_include_directories(sil_fusion_translators PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(sil_fusion_translators
  builtin_interfaces
  geographic_msgs
  sil_msgs
  l3_msgs
  l3_external_msgs
)

# Relay node executable.
add_executable(sil_fusion_adapter_node src/sil_fusion_adapter_node.cpp)
target_link_libraries(sil_fusion_adapter_node sil_fusion_translators)
ament_target_dependencies(sil_fusion_adapter_node
  rclcpp
  builtin_interfaces
  geographic_msgs
  sil_msgs
  l3_msgs
  l3_external_msgs
)

# Install with RPATH so libsil_fusion_translators.so is found without
# LD_LIBRARY_PATH (exe in lib/<pkg>/, lib in lib/, so $ORIGIN/..).
set_target_properties(sil_fusion_translators PROPERTIES
  INSTALL_RPATH "$ORIGIN")
set_target_properties(sil_fusion_adapter_node PROPERTIES
  INSTALL_RPATH "$ORIGIN/..")
install(TARGETS sil_fusion_adapter_node sil_fusion_translators
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_sil_fusion_translators test/test_translators.cpp)
  target_link_libraries(test_sil_fusion_translators sil_fusion_translators)
  ament_target_dependencies(test_sil_fusion_translators
    builtin_interfaces
    geographic_msgs
    sil_msgs
    l3_msgs
    l3_external_msgs
  )
endif()

ament_package()
