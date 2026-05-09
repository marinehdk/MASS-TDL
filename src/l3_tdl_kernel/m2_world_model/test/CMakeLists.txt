if(BUILD_TESTING)
  ament_add_gtest(test_coord_transform test_coord_transform.cpp)
  target_link_libraries(test_coord_transform ${PROJECT_NAME}_lib)
  target_include_directories(test_coord_transform PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>)

  ament_add_gtest(test_cpa_tcpa_calculator test_cpa_tcpa_calculator.cpp)
  target_link_libraries(test_cpa_tcpa_calculator ${PROJECT_NAME}_lib)
  target_include_directories(test_cpa_tcpa_calculator PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>)

  ament_add_gtest(test_encounter_classifier test_encounter_classifier.cpp)
  target_link_libraries(test_encounter_classifier ${PROJECT_NAME}_lib)
  target_include_directories(test_encounter_classifier PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>)

  ament_add_gtest(test_track_buffer test_track_buffer.cpp)
  target_link_libraries(test_track_buffer ${PROJECT_NAME}_lib)
  target_include_directories(test_track_buffer PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>)

  ament_add_gtest(test_view_health_monitor test_view_health_monitor.cpp)
  target_link_libraries(test_view_health_monitor ${PROJECT_NAME}_lib)
  target_include_directories(test_view_health_monitor PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>)

  ament_add_gtest(test_world_state_aggregator test_world_state_aggregator.cpp)
  target_link_libraries(test_world_state_aggregator ${PROJECT_NAME}_lib)
  target_include_directories(test_world_state_aggregator PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>)

  # ── Integration tests ──────────────────────────────────────────────────────
  ament_add_gtest(test_world_model_node_smoke integration/test_world_model_node_smoke.cpp)
  target_link_libraries(test_world_model_node_smoke ${PROJECT_NAME}_lib)
  target_include_directories(test_world_model_node_smoke PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>)

  ament_add_gtest(test_environment_degraded_path
      integration/test_environment_degraded_path.cpp)
  target_link_libraries(test_environment_degraded_path ${PROJECT_NAME}_lib)
  target_include_directories(test_environment_degraded_path PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>)
endif()
