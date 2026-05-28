if(BUILD_TESTING)
  add_compile_options(-Wno-error -Wno-undef -Wno-shadow -Wno-conversion -Wno-sign-conversion)
  ament_add_gtest(test_voyage_task_validator test_voyage_task_validator.cpp)
  target_link_libraries(test_voyage_task_validator ${PROJECT_NAME}_lib)

  ament_add_gtest(test_eta_projector test_eta_projector.cpp)
  target_link_libraries(test_eta_projector ${PROJECT_NAME}_lib)

  ament_add_gtest(test_replan_request_trigger test_replan_request_trigger.cpp)
  target_link_libraries(test_replan_request_trigger ${PROJECT_NAME}_lib)

  ament_add_gtest(test_replan_response_handler test_replan_response_handler.cpp)
  target_link_libraries(test_replan_response_handler ${PROJECT_NAME}_lib)

  ament_add_gtest(test_mission_state_machine test_mission_state_machine.cpp)
  target_link_libraries(test_mission_state_machine ${PROJECT_NAME}_lib)

  ament_add_gtest(test_route_received test_route_received.cpp)
  target_link_libraries(test_route_received ${PROJECT_NAME}_lib)

  ament_add_gtest(test_m3_node_lifecycle integration/test_m3_node_lifecycle.cpp)
  target_link_libraries(test_m3_node_lifecycle ${PROJECT_NAME}_lib)

  ament_add_gtest(test_current_error_monitor test_current_error_monitor.cpp)
  target_link_libraries(test_current_error_monitor ${PROJECT_NAME}_lib)

  ament_add_gtest(test_l1_watchdog_monitor test_l1_watchdog_monitor.cpp)
  target_link_libraries(test_l1_watchdog_monitor ${PROJECT_NAME}_lib)

  ament_add_gtest(test_idl_schema_version test_idl_schema_version.cpp)
  target_link_libraries(test_idl_schema_version ${PROJECT_NAME}_lib)

  ament_add_gtest(test_m3_dual_subscription integration/test_m3_dual_subscription.cpp)
  target_link_libraries(test_m3_dual_subscription ${PROJECT_NAME}_lib)
endif()
