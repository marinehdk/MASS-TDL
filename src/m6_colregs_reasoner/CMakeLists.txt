cmake_minimum_required(VERSION 3.22)
project(m6_colregs_reasoner VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion -Wsign-conversion)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(Eigen3 3.4 REQUIRED)
find_package(spdlog REQUIRED)
find_package(yaml-cpp REQUIRED)

add_library(${PROJECT_NAME}_lib
  src/error_codes.cpp
  src/geometry_utils.cpp
  src/target_state_cache.cpp
  src/colregs_phase_classifier.cpp
  src/colregs_constraint_generator.cpp
  src/rule_library_loader.cpp
  src/colregs_reasoner_node.cpp
  src/rules/colregs/rule5_lookout.cpp
  src/rules/colregs/rule6_safe_speed.cpp
  src/rules/colregs/rule7_risk_of_collision.cpp
  src/rules/colregs/rule8_action_to_avoid.cpp
  src/rules/colregs/rule13_overtaking.cpp
  src/rules/colregs/rule14_head_on.cpp
  src/rules/colregs/rule15_crossing.cpp
  src/rules/colregs/rule16_give_way.cpp
  src/rules/colregs/rule17_stand_on.cpp
  src/rules/colregs/rule18_responsibilities.cpp
  src/rules/colregs/rule19_restricted_visibility.cpp
)
target_include_directories(${PROJECT_NAME}_lib
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(${PROJECT_NAME}_lib rclcpp l3_msgs)
target_link_libraries(${PROJECT_NAME}_lib PUBLIC
  Eigen3::Eigen spdlog::spdlog yaml-cpp::yaml-cpp
)

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_lib)

install(TARGETS ${PROJECT_NAME} ${PROJECT_NAME}_lib
  ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)
  ament_lint_auto_find_test_dependencies()
  add_subdirectory(test)
endif()

ament_package()
