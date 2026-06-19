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

  ament_add_gtest(test_woerner_compliance_scorer test_woerner_compliance_scorer.cpp)
  target_link_libraries(test_woerner_compliance_scorer ${PROJECT_NAME}_lib)
  target_include_directories(test_woerner_compliance_scorer PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>)

  # ── D2.2 Track D: MUST-1 OVERTAKING sector boundary tests ─────────────────
  ament_add_gtest(test_must1_overtaking_sector test_must1_overtaking_sector.cpp)
  target_link_libraries(test_must1_overtaking_sector ${PROJECT_NAME}_lib)
  target_include_directories(test_must1_overtaking_sector PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>)

  # ── D2.2 Track D: MUST-6 SOG validation ────────────────────────────────────
  ament_add_gtest(test_must6_sog_validation test_must6_sog_validation.cpp)

  # ── D2.2 Track D: Intent confidence ────────────────────────────────────────
  ament_add_gtest(test_intent_confidence test_intent_confidence.cpp)

  # ── D2.2 Track D: Environment sanity checker ───────────────────────────────
  ament_add_gtest(test_env_sanity_checker test_env_sanity_checker.cpp)
  target_link_libraries(test_env_sanity_checker ${PROJECT_NAME}_lib)
  target_include_directories(test_env_sanity_checker PRIVATE
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

  # ── D2.2 NEW: End-to-end integration test scaffolds ──────────────────────────
  ament_add_gtest(test_world_state_end_to_end
      integration/test_world_state_end_to_end.cpp)
  target_link_libraries(test_world_state_end_to_end ${PROJECT_NAME}_lib)
  target_include_directories(test_world_state_end_to_end PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>)
endif()
