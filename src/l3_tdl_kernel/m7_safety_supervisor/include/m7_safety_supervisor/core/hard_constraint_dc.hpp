#ifndef M7_SAFETY_SUPERVISOR_CORE_HARD_CONSTRAINT_DC_HPP_
#define M7_SAFETY_SUPERVISOR_CORE_HARD_CONSTRAINT_DC_HPP_

namespace mass_l3::m7::core {

struct DcSelfCheckState {
  bool ram_integrity_ok{false};
  bool alu_test_passed{false};
  bool control_flow_ok{false};
  bool input_integrity_ok{false};
  bool output_integrity_ok{false};
  bool watchdog_ok{false};
};

struct DcConstraintResult {
  bool dc_met{false};
  bool violation{false};
  float effective_dc_pct{0.0F};
};

DcConstraintResult evaluate_dc_constraint(DcSelfCheckState const& state) noexcept;

}  // namespace mass_l3::m7::core

#endif
