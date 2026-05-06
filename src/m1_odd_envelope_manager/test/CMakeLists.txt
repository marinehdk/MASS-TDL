if(BUILD_TESTING)
  # OddStateMachine unit tests
  ament_add_gtest(test_odd_state_machine test_odd_state_machine.cpp)
  target_link_libraries(test_odd_state_machine PRIVATE ${PROJECT_NAME}_lib)

  # ConformanceScoreCalculator unit tests (E/T/H scoring)
  ament_add_gtest(test_conformance_score test_conformance_score.cpp)
  target_link_libraries(test_conformance_score PRIVATE ${PROJECT_NAME}_lib)
  target_include_directories(test_conformance_score PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
endif()
