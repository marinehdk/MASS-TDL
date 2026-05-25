#include <gtest/gtest.h>
#include "m7_safety_supervisor/core/hard_constraint_dc.hpp"

namespace mass_l3::m7::core {
namespace {

TEST(HardConstraintDc, AllSelfChecksPassMeetsDcTarget)
{
  DcSelfCheckState state{};
  state.ram_integrity_ok    = true;
  state.alu_test_passed     = true;
  state.control_flow_ok     = true;
  state.input_integrity_ok  = true;
  state.output_integrity_ok = true;
  state.watchdog_ok         = true;
  auto const result = evaluate_dc_constraint(state);
  EXPECT_GE(result.effective_dc_pct, 90.0F);
  EXPECT_TRUE(result.dc_met);
  EXPECT_FALSE(result.violation);
}

TEST(HardConstraintDc, RamIntegrityFailureDegradesDc)
{
  DcSelfCheckState state{};
  state.ram_integrity_ok    = false;
  state.alu_test_passed     = true;
  state.control_flow_ok     = true;
  state.input_integrity_ok  = true;
  state.output_integrity_ok = true;
  state.watchdog_ok         = true;
  auto const result = evaluate_dc_constraint(state);
  EXPECT_LT(result.effective_dc_pct, 90.0F);
  EXPECT_TRUE(result.violation);
}

TEST(HardConstraintDc, AllSelfChecksFailMinimumDc)
{
  DcSelfCheckState state{};
  auto const result = evaluate_dc_constraint(state);
  EXPECT_LT(result.effective_dc_pct, 60.0F);
  EXPECT_TRUE(result.violation);
}

}  // namespace
}  // namespace mass_l3::m7::core
