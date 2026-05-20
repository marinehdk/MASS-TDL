cmake_minimum_required(VERSION 3.11)
project(fcb_cerberus_validator VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------------------------------------------------
# cerberus-cpp — FetchContent from GitHub
# -------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
  cerberus_cpp
  GIT_REPOSITORY https://github.com/dokempf/cerberus-cpp.git
  GIT_TAG        main
)
# Suppress FetchContent download status noise
set(FETCHCONTENT_QUIET ON)
FetchContent_MakeAvailable(cerberus_cpp)

# -------------------------------------------------------------------
# yaml-cpp — system / Homebrew
# -------------------------------------------------------------------
# macOS Homebrew: /opt/homebrew
if(APPLE)
  list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
endif()
find_package(yaml-cpp REQUIRED)

# -------------------------------------------------------------------
# Executable
# -------------------------------------------------------------------
add_executable(validate_scenario cerberus_validator.cpp)

# cerberus-cpp header-only; after FetchContent_MakeAvailable
# the include directory is available via cerberus_cpp_SOURCE_DIR
target_include_directories(validate_scenario PRIVATE ${cerberus_cpp_SOURCE_DIR}/include)
target_link_libraries(validate_scenario PRIVATE yaml-cpp::yaml-cpp)

# -------------------------------------------------------------------
# Install
# -------------------------------------------------------------------
install(TARGETS validate_scenario DESTINATION bin)
