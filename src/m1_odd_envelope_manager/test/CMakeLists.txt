if(BUILD_TESTING)
  # OddStateMachine unit tests
  ament_add_gtest(test_odd_state_machine test_odd_state_machine.cpp)
  target_link_libraries(test_odd_state_machine PRIVATE ${PROJECT_NAME}_lib)
endif()
