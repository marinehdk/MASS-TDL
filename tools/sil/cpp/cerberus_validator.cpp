/**
 * @file cerberus_validator.cpp
 * @brief Standalone CLI that validates scenario YAML files against a cerberus schema.
 *
 * Exit codes:
 *   0 — document is valid
 *   1 — document failed schema validation
 *   2 — runtime error (file not found, malformed YAML, etc.)
 *
 * Build (standalone, no ROS2):
 *   cd tools/sil/cpp
 *   cmake -B build -DCMAKE_BUILD_TYPE=Release .
 *   cmake --build build
 *
 * Usage:
 *   ./build/validate_scenario <schema.yaml> <scenario.yaml>
 *
 * Dependencies:
 *   - cerberus-cpp (header-only, fetched via CMake FetchContent)
 *   - yaml-cpp     (system package or Homebrew)
 */

#include <cerberus-cpp/validator.hh>
#include <yaml-cpp/yaml.h>
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <schema.yaml> <scenario.yaml>\n";
    return 2;
  }

  try {
    const std::string schema_path(argv[1]);
    const std::string document_path(argv[2]);

    YAML::Node schema   = YAML::LoadFile(schema_path);
    YAML::Node document = YAML::LoadFile(document_path);

    cerberus::Validator validator(schema);

    // Allow extra fields not explicitly declared in the schema.
    // This is needed because the schema only declares the core subset
    // of maritime-schema v3.0 fields; scenario files may contain
    // additional metadata (e.g. odd_cell, encounter, expected_outcome,
    // simulation_settings) that should be tolerated.
    validator.setAllowUnknown(true);

    if (validator.validate(document)) {
      return 0;
    }

    std::cerr << validator;
    return 1;

  } catch (const YAML::BadFile& e) {
    std::cerr << "Error: cannot open file — " << e.what() << "\n";
    return 2;
  } catch (const YAML::ParserException& e) {
    std::cerr << "Error: malformed YAML — " << e.what() << "\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 2;
  }
}
