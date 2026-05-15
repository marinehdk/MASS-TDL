if(BUILD_TESTING)
  # IvP solver unit tests
  ament_add_gtest(test_ivp_solver unit/test_ivp_solver.cpp)
  target_link_libraries(test_ivp_solver m4_core)
  target_include_directories(test_ivp_solver PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
  )

  # IvP function unit tests
  ament_add_gtest(test_ivp_function unit/test_ivp_function.cpp)
  target_link_libraries(test_ivp_function m4_core)
  target_include_directories(test_ivp_function PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
  )

  # IvP domain unit tests
  ament_add_gtest(test_ivp_domain unit/test_ivp_domain.cpp)
  target_link_libraries(test_ivp_domain m4_core)
  target_include_directories(test_ivp_domain PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
  )

  # Behavior dictionary unit tests
  ament_add_gtest(test_behavior_dictionary unit/test_behavior_dictionary.cpp)
  target_link_libraries(test_behavior_dictionary m4_core)
  target_include_directories(test_behavior_dictionary PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
  )
  target_compile_definitions(test_behavior_dictionary PRIVATE
    TEST_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../config"
  )

  # Behavior activation unit tests
  ament_add_gtest(test_behavior_activation unit/test_behavior_activation.cpp)
  target_link_libraries(test_behavior_activation m4_core)
  target_include_directories(test_behavior_activation PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
  )

  # M4 node lifecycle integration test
  ament_add_gtest(test_m4_node_lifecycle unit/test_m4_node_lifecycle.cpp)
  target_link_libraries(test_m4_node_lifecycle m4_node_lib)
  target_include_directories(test_m4_node_lifecycle PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
  )
endif()
