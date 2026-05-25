#include "m7_safety_supervisor/core/hard_constraint_dc.hpp"

namespace mass_l3::m7::core {
namespace {
inline constexpr float kDcRamIntegrity   = 0.99F;
inline constexpr float kDcAluTest        = 0.90F;
inline constexpr float kDcControlFlow    = 0.90F;
inline constexpr float kDcInputIntegrity = 0.99F;
inline constexpr float kDcOutputIntegrity= 0.99F;
inline constexpr float kDcWatchdog       = 0.95F;
inline constexpr float kDcWeight         = 1.0F / 6.0F;
inline constexpr float kDcTarget         = 0.90F;
}  // namespace

DcConstraintResult evaluate_dc_constraint(DcSelfCheckState const& state) noexcept
{
  DcConstraintResult result{};
  float product = 1.0F;
  auto const apply = [&product](bool ok, float dc) {
    float const contrib = ok ? dc * kDcWeight : 0.0F;
    product *= (1.0F - contrib);
  };
  apply(state.ram_integrity_ok,    kDcRamIntegrity);
  apply(state.alu_test_passed,     kDcAluTest);
  apply(state.control_flow_ok,     kDcControlFlow);
  apply(state.input_integrity_ok,  kDcInputIntegrity);
  apply(state.output_integrity_ok, kDcOutputIntegrity);
  apply(state.watchdog_ok,         kDcWatchdog);
  result.effective_dc_pct = (1.0F - product) * 100.0F;
  result.dc_met   = (result.effective_dc_pct >= kDcTarget * 100.0F);
  result.violation = !result.dc_met;
  return result;
}

}  // namespace mass_l3::m7::core
