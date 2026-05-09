// SPDX-License-Identifier: Proprietary
// Unit test for FCBSimulator::export_fmu_interface() (spec §v3.1.4 + §v3.1.12 DoD).
#include <gtest/gtest.h>
#include "fcb_simulator/fcb_simulator_plugin.hpp"
#include "ship_sim_interfaces/ship_motion_simulator.hpp"

using ship_sim::FmuInterfaceSpec;
using ship_sim::FmuVariableSpec;

class FcbExportFmuInterfaceTest : public ::testing::Test {
 protected:
  fcb_sim::FCBSimulator sut_;
};

TEST_F(FcbExportFmuInterfaceTest, ReturnsCorrectFmiVersion) {
  FmuInterfaceSpec spec = sut_.export_fmu_interface();
  EXPECT_EQ(spec.fmi_version, "2.0");
}

TEST_F(FcbExportFmuInterfaceTest, ReturnsCorrectModelName) {
  FmuInterfaceSpec spec = sut_.export_fmu_interface();
  EXPECT_EQ(spec.model_name, "FCBShipDynamics");
  EXPECT_EQ(spec.model_identifier, "FCBShipDynamics");
}

TEST_F(FcbExportFmuInterfaceTest, ReturnsCorrectStepSize) {
  FmuInterfaceSpec spec = sut_.export_fmu_interface();
  EXPECT_DOUBLE_EQ(spec.default_step_size, 0.02);
}

TEST_F(FcbExportFmuInterfaceTest, HasExactVariableCount) {
  FmuInterfaceSpec spec = sut_.export_fmu_interface();
  EXPECT_EQ(spec.variables.size(), 22u);
}

TEST_F(FcbExportFmuInterfaceTest, AllCausalityValuesLegal) {
  FmuInterfaceSpec spec = sut_.export_fmu_interface();
  for (const auto& v : spec.variables) {
    EXPECT_TRUE(v.causality == "input"     ||
                v.causality == "output"    ||
                v.causality == "parameter" ||
                v.causality == "local")
      << "Variable " << v.name << " has invalid causality: " << v.causality;
  }
}

TEST_F(FcbExportFmuInterfaceTest, AllVariabilityValuesLegal) {
  FmuInterfaceSpec spec = sut_.export_fmu_interface();
  for (const auto& v : spec.variables) {
    EXPECT_TRUE(v.variability == "continuous" ||
                v.variability == "discrete"   ||
                v.variability == "fixed"      ||
                v.variability == "tunable")
      << "Variable " << v.name << " has invalid variability: " << v.variability;
  }
}

TEST_F(FcbExportFmuInterfaceTest, AllTypeValuesLegal) {
  FmuInterfaceSpec spec = sut_.export_fmu_interface();
  for (const auto& v : spec.variables) {
    EXPECT_TRUE(v.type == "Real"    ||
                v.type == "Integer" ||
                v.type == "Boolean" ||
                v.type == "String")
      << "Variable " << v.name << " has invalid type: " << v.type;
  }
}

TEST_F(FcbExportFmuInterfaceTest, ContainsRequiredOutputs) {
  FmuInterfaceSpec spec = sut_.export_fmu_interface();
  std::vector<std::string> required_outputs =
      {"u","v","r","x","y","psi","phi","p","delta","n","sog"};
  for (const auto& name : required_outputs) {
    bool found = false;
    for (const auto& v : spec.variables) {
      if (v.name == name && v.causality == "output") { found = true; break; }
    }
    EXPECT_TRUE(found) << "Required output variable not found: " << name;
  }
}

TEST_F(FcbExportFmuInterfaceTest, ContainsControlInputs) {
  FmuInterfaceSpec spec = sut_.export_fmu_interface();
  std::vector<std::string> ctrl_inputs = {"delta_cmd", "n_rps_cmd"};
  for (const auto& name : ctrl_inputs) {
    bool found = false;
    for (const auto& v : spec.variables) {
      if (v.name == name && v.causality == "input") { found = true; break; }
    }
    EXPECT_TRUE(found) << "Control input not found: " << name;
  }
}

TEST_F(FcbExportFmuInterfaceTest, ContainsDisturbanceInputs) {
  FmuInterfaceSpec spec = sut_.export_fmu_interface();
  std::vector<std::string> dist_inputs =
      {"wind_dir_deg","wind_speed_mps","current_dir_deg","current_speed_mps"};
  for (const auto& name : dist_inputs) {
    bool found = false;
    for (const auto& v : spec.variables) {
      if (v.name == name && v.causality == "input") { found = true; break; }
    }
    EXPECT_TRUE(found) << "Disturbance input not found: " << name;
  }
}

TEST_F(FcbExportFmuInterfaceTest, ContainsParameters) {
  FmuInterfaceSpec spec = sut_.export_fmu_interface();
  std::vector<std::string> params = {"L","B","d","m","Izz"};
  for (const auto& name : params) {
    bool found = false;
    for (const auto& v : spec.variables) {
      if (v.name == name && v.causality == "parameter") { found = true; break; }
    }
    EXPECT_TRUE(found) << "Parameter not found: " << name;
  }
}
