if(BUILD_TESTING)
  ament_add_gtest(test_geometry_utils test_geometry_utils.cpp)
  target_link_libraries(test_geometry_utils ${PROJECT_NAME}_lib)

  # Rule tests
  ament_add_gtest(test_rule5_lookout test_rule5_lookout.cpp)
  target_link_libraries(test_rule5_lookout ${PROJECT_NAME}_lib)

  ament_add_gtest(test_rule6_safe_speed test_rule6_safe_speed.cpp)
  target_link_libraries(test_rule6_safe_speed ${PROJECT_NAME}_lib)

  ament_add_gtest(test_rule7_risk_of_collision test_rule7_risk_of_collision.cpp)
  target_link_libraries(test_rule7_risk_of_collision ${PROJECT_NAME}_lib)

  ament_add_gtest(test_rule8_action_to_avoid test_rule8_action_to_avoid.cpp)
  target_link_libraries(test_rule8_action_to_avoid ${PROJECT_NAME}_lib)

  ament_add_gtest(test_rule13_overtaking test_rule13_overtaking.cpp)
  target_link_libraries(test_rule13_overtaking ${PROJECT_NAME}_lib)

  ament_add_gtest(test_rule14_head_on test_rule14_head_on.cpp)
  target_link_libraries(test_rule14_head_on ${PROJECT_NAME}_lib)

  ament_add_gtest(test_rule15_crossing test_rule15_crossing.cpp)
  target_link_libraries(test_rule15_crossing ${PROJECT_NAME}_lib)

  ament_add_gtest(test_rule16_give_way test_rule16_give_way.cpp)
  target_link_libraries(test_rule16_give_way ${PROJECT_NAME}_lib)

  ament_add_gtest(test_rule17_stand_on test_rule17_stand_on.cpp)
  target_link_libraries(test_rule17_stand_on ${PROJECT_NAME}_lib)

  ament_add_gtest(test_rule18_responsibilities test_rule18_responsibilities.cpp)
  target_link_libraries(test_rule18_responsibilities ${PROJECT_NAME}_lib)

  ament_add_gtest(test_rule19_restricted_visibility test_rule19_restricted_visibility.cpp)
  target_link_libraries(test_rule19_restricted_visibility ${PROJECT_NAME}_lib)

  # Support class tests
  ament_add_gtest(test_phase_classifier test_phase_classifier.cpp)
  target_link_libraries(test_phase_classifier ${PROJECT_NAME}_lib)

  ament_add_gtest(test_constraint_generator test_constraint_generator.cpp)
  target_link_libraries(test_constraint_generator ${PROJECT_NAME}_lib)

  ament_add_gtest(test_rule_library_loader test_rule_library_loader.cpp)
  target_link_libraries(test_rule_library_loader ${PROJECT_NAME}_lib)

  ament_add_gtest(test_rule_latch test_rule_latch.cpp)
  target_link_libraries(test_rule_latch ${PROJECT_NAME}_lib)

  ament_add_gtest(test_colregs_release_policy test_colregs_release_policy.cpp)
  target_link_libraries(test_colregs_release_policy ${PROJECT_NAME}_lib)

  # Integration tests
  ament_add_gtest(test_node_lifecycle integration/test_node_lifecycle.cpp)
  target_link_libraries(test_node_lifecycle ${PROJECT_NAME}_lib)
  # The node looks for config/ relative to CWD.  Point it at the package source root.
  set_tests_properties(test_node_lifecycle PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
  )

  ament_add_gtest(test_colregs_chain test_colregs_chain.cpp)
  target_link_libraries(test_colregs_chain ${PROJECT_NAME}_lib)
endif()
