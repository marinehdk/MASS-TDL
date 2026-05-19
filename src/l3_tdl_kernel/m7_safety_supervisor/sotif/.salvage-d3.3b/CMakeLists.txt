cmake_minimum_required(VERSION 3.22)
project(m7_safety_supervisor VERSION 1.0.0 LANGUAGES CXX)

# ─────────── C++ Standard ───────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ─────────── ROS2 / ament (optional for standalone build) ───────────
find_package(ament_cmake QUIET)
set(BUILD_ROS2 ${ament_cmake_FOUND})

if(BUILD_ROS2)
  find_package(rclcpp REQUIRED)
  find_package(builtin_interfaces REQUIRED)
  find_package(l3_msgs REQUIRED)
endif()

# ─────────── Testing ───────────
enable_testing()
if(BUILD_TESTING)
  find_package(GTest REQUIRED)
endif()

# ─────────── Compile Options Interface (per coding-standards.md §2.2) ───────────
add_library(m7_compile_options INTERFACE)
target_compile_options(m7_compile_options INTERFACE
    -Wall -Wextra -Wpedantic
    -Werror
    -Wshadow -Wconversion -Wsign-conversion
    -Wcast-align -Wcast-qual
    -Wold-style-cast -Wzero-as-null-pointer-constant
    -Wnon-virtual-dtor -Woverloaded-virtual
    -Wnull-dereference -Wdouble-promotion
    -Wfloat-equal
    -Wformat=2 -Wformat-security
    -Wmissing-declarations
    -Wundef -Wunused
    -fstack-protector-strong
    "$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined;-fno-omit-frame-pointer>"
    "$<$<CONFIG:Release>:-O2>"
)
target_link_options(m7_compile_options INTERFACE
    "$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined>"
)

# ─────────── Library: m7_safety_supervisor_lib ───────────
add_library(${PROJECT_NAME}_lib
  src/sotif_assumption_monitor.cpp
)

target_include_directories(${PROJECT_NAME}_lib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME}_lib
    PUBLIC
        m7_compile_options
)

if(BUILD_ROS2)
  ament_target_dependencies(${PROJECT_NAME}_lib
      PUBLIC
          rclcpp
          builtin_interfaces
          l3_msgs
  )
endif()

# ─────────── Unit Tests ───────────
if(BUILD_TESTING)
  add_executable(test_sotif_assumption_monitor
    test/test_sotif_assumption_monitor.cpp
  )

  target_link_libraries(test_sotif_assumption_monitor
    PRIVATE
      ${PROJECT_NAME}_lib
      GTest::gtest
      GTest::gtest_main
      m7_compile_options
  )

  if(BUILD_ROS2)
    ament_target_dependencies(test_sotif_assumption_monitor
      PRIVATE
          rclcpp
          builtin_interfaces
          l3_msgs
    )
  endif()

  add_test(
    NAME test_sotif_assumption_monitor
    COMMAND test_sotif_assumption_monitor
  )
endif()

# ─────────── Install ───────────
if(BUILD_ROS2)
  install(TARGETS ${PROJECT_NAME}_lib m7_compile_options
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
  )

  install(DIRECTORY include/
    DESTINATION include
  )

  ament_package()
endif()
