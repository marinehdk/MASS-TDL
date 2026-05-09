if(BUILD_TESTING)
  ament_add_gtest(test_voyage_task_validator test_voyage_task_validator.cpp)
  target_link_libraries(test_voyage_task_validator ${PROJECT_NAME}_lib)
  target_include_directories(test_voyage_task_validator PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  ament_add_gtest(test_eta_projector test_eta_projector.cpp)
  target_link_libraries(test_eta_projector ${PROJECT_NAME}_lib)
  target_include_directories(test_eta_projector PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  ament_add_gtest(test_replan_request_trigger test_replan_request_trigger.cpp)
  target_link_libraries(test_replan_request_trigger ${PROJECT_NAME}_lib)
  target_include_directories(test_replan_request_trigger PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  ament_add_gtest(test_replan_response_handler test_replan_response_handler.cpp)
  target_link_libraries(test_replan_response_handler ${PROJECT_NAME}_lib)
  target_include_directories(test_replan_response_handler PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  ament_add_gtest(test_mission_state_machine test_mission_state_machine.cpp)
  target_link_libraries(test_mission_state_machine ${PROJECT_NAME}_lib)
  target_include_directories(test_mission_state_machine PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

  ament_add_gtest(test_m3_node_lifecycle integration/test_m3_node_lifecycle.cpp)
  target_link_libraries(test_m3_node_lifecycle ${PROJECT_NAME}_lib)
  target_include_directories(test_m3_node_lifecycle PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
endif()
