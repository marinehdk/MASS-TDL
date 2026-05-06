if(BUILD_TESTING)
  ament_add_gtest(test_geometry_utils test_geometry_utils.cpp)
  target_link_libraries(test_geometry_utils PRIVATE ${PROJECT_NAME}_lib)
  target_include_directories(test_geometry_utils PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  # Rule tests
  ament_add_gtest(test_rule5_lookout test_rule5_lookout.cpp)
  target_link_libraries(test_rule5_lookout PRIVATE ${PROJECT_NAME}_lib)
  target_include_directories(test_rule5_lookout PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  ament_add_gtest(test_rule6_safe_speed test_rule6_safe_speed.cpp)
  target_link_libraries(test_rule6_safe_speed PRIVATE ${PROJECT_NAME}_lib)
  target_include_directories(test_rule6_safe_speed PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  ament_add_gtest(test_rule7_risk_of_collision test_rule7_risk_of_collision.cpp)
  target_link_libraries(test_rule7_risk_of_collision PRIVATE ${PROJECT_NAME}_lib)
  target_include_directories(test_rule7_risk_of_collision PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  ament_add_gtest(test_rule8_action_to_avoid test_rule8_action_to_avoid.cpp)
  target_link_libraries(test_rule8_action_to_avoid PRIVATE ${PROJECT_NAME}_lib)
  target_include_directories(test_rule8_action_to_avoid PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  ament_add_gtest(test_rule13_overtaking test_rule13_overtaking.cpp)
  target_link_libraries(test_rule13_overtaking PRIVATE ${PROJECT_NAME}_lib)
  target_include_directories(test_rule13_overtaking PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  ament_add_gtest(test_rule14_head_on test_rule14_head_on.cpp)
  target_link_libraries(test_rule14_head_on PRIVATE ${PROJECT_NAME}_lib)
  target_include_directories(test_rule14_head_on PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  ament_add_gtest(test_rule15_crossing test_rule15_crossing.cpp)
  target_link_libraries(test_rule15_crossing PRIVATE ${PROJECT_NAME}_lib)
  target_include_directories(test_rule15_crossing PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  ament_add_gtest(test_rule16_give_way test_rule16_give_way.cpp)
  target_link_libraries(test_rule16_give_way PRIVATE ${PROJECT_NAME}_lib)
  target_include_directories(test_rule16_give_way PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  ament_add_gtest(test_rule17_stand_on test_rule17_stand_on.cpp)
  target_link_libraries(test_rule17_stand_on PRIVATE ${PROJECT_NAME}_lib)
  target_include_directories(test_rule17_stand_on PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  ament_add_gtest(test_rule18_responsibilities test_rule18_responsibilities.cpp)
  target_link_libraries(test_rule18_responsibilities PRIVATE ${PROJECT_NAME}_lib)
  target_include_directories(test_rule18_responsibilities PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  ament_add_gtest(test_rule19_restricted_visibility test_rule19_restricted_visibility.cpp)
  target_link_libraries(test_rule19_restricted_visibility PRIVATE ${PROJECT_NAME}_lib)
  target_include_directories(test_rule19_restricted_visibility PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  # Support class tests
  ament_add_gtest(test_phase_classifier test_phase_classifier.cpp)
  target_link_libraries(test_phase_classifier PRIVATE ${PROJECT_NAME}_lib)
  target_include_directories(test_phase_classifier PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  ament_add_gtest(test_constraint_generator test_constraint_generator.cpp)
  target_link_libraries(test_constraint_generator PRIVATE ${PROJECT_NAME}_lib)
  target_include_directories(test_constraint_generator PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  ament_add_gtest(test_rule_library_loader test_rule_library_loader.cpp)
  target_link_libraries(test_rule_library_loader PRIVATE ${PROJECT_NAME}_lib)
  target_include_directories(test_rule_library_loader PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  # Integration tests
  ament_add_gtest(test_node_lifecycle integration/test_node_lifecycle.cpp)
  target_link_libraries(test_node_lifecycle PRIVATE ${PROJECT_NAME}_lib)
  target_include_directories(test_node_lifecycle PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
endif()
