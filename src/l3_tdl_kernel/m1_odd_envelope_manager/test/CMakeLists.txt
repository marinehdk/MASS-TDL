if(BUILD_TESTING)
  # OddStateMachine unit tests
  ament_add_gtest(test_odd_state_machine test_odd_state_machine.cpp)
  target_link_libraries(test_odd_state_machine ${PROJECT_NAME}_lib)

  # ConformanceScoreCalculator unit tests (E/T/H scoring)
  ament_add_gtest(test_conformance_score test_conformance_score.cpp)
  target_link_libraries(test_conformance_score ${PROJECT_NAME}_lib)

  # TmrTdlEstimator unit tests (TMR/TDL estimation)
  ament_add_gtest(test_tmr_tdl_estimator test_tmr_tdl_estimator.cpp)
  target_link_libraries(test_tmr_tdl_estimator ${PROJECT_NAME}_lib)

  # MrcTriggerLogic unit tests (MRC selection)
  ament_add_gtest(test_mrc_trigger_logic test_mrc_trigger_logic.cpp)
  target_link_libraries(test_mrc_trigger_logic ${PROJECT_NAME}_lib)

  # Integration test (node construction, pub/sub topology)
  ament_add_gtest(test_node_integration test_node_integration.cpp)
  target_link_libraries(test_node_integration ${PROJECT_NAME}_lib)

  # ScoringInputs degraded defaults test (DEMO-1 Task 3)
  ament_add_gtest(test_scoring_inputs_degraded test_scoring_inputs_degraded.cpp)
  target_link_libraries(test_scoring_inputs_degraded ${PROJECT_NAME}_lib)
endif()
