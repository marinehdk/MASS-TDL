if(BUILD_TESTING)
  # OddStateMachine unit tests
  ament_add_gtest(test_odd_state_machine test_odd_state_machine.cpp)
  target_link_libraries(test_odd_state_machine ${PROJECT_NAME}_lib)

  # ConformanceScoreCalculator unit tests (E/T/H scoring)
  ament_add_gtest(test_conformance_score test_conformance_score.cpp)
  target_link_libraries(test_conformance_score ${PROJECT_NAME}_lib)
  target_include_directories(test_conformance_score PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  # TmrTdlEstimator unit tests (TMR/TDL estimation)
  ament_add_gtest(test_tmr_tdl_estimator test_tmr_tdl_estimator.cpp)
  target_link_libraries(test_tmr_tdl_estimator ${PROJECT_NAME}_lib)
  target_include_directories(test_tmr_tdl_estimator PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  # MrcTriggerLogic unit tests (MRC selection)
  ament_add_gtest(test_mrc_trigger_logic test_mrc_trigger_logic.cpp)
  target_link_libraries(test_mrc_trigger_logic ${PROJECT_NAME}_lib)
  target_include_directories(test_mrc_trigger_logic PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  # Integration test (node construction, pub/sub topology)
  ament_add_gtest(test_node_integration test_node_integration.cpp)
  target_link_libraries(test_node_integration ${PROJECT_NAME}_lib)
  target_include_directories(test_node_integration PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
endif()
