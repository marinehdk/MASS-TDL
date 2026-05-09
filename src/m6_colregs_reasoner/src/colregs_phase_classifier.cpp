#include "m6_colregs_reasoner/colregs_phase_classifier.hpp"

#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs {

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
TimingPhase PhaseClassifier::classify(double tcpa_s, const RuleParameters& params) const {
  if (tcpa_s > params.t_standOn_s) {
    return TimingPhase::PRESERVE_COURSE;
  }
  if (tcpa_s > params.t_act_s) {
    return TimingPhase::SOUND_WARNING;
  }
  if (tcpa_s > params.t_emergency_s) {
    return TimingPhase::INDEPENDENT_ACTION;
  }
  return TimingPhase::CRITICAL_ACTION;
}

}  // namespace mass_l3::m6_colregs
