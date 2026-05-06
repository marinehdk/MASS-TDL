if(BUILD_TESTING)
  ament_add_gtest(test_coord_transform test_coord_transform.cpp)
  target_link_libraries(test_coord_transform PRIVATE ${PROJECT_NAME}_lib)
  target_include_directories(test_coord_transform PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>)
endif()
