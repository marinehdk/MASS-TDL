#pragma once

namespace mass_l3::m6_colregs {

// Onset-latched COLREG hysteresis (Rule 13(d): classification fixed at onset;
// later bearing changes do not reclassify). Release follows Rule 16 "finally past
// and clear": the target has drawn abaft the beam and the range is opening. A
// conservative CPA fallback (predicted CPA above 1.5×cpa_safe while opening) also
// releases, in case the abaft-beam geometry is unavailable. Both release paths
// require the range to be opening, which prevents chattering while still closing.
class RuleLatch {
 public:
  RuleLatch(double cpa_safe_m, double release_factor)
      : cpa_safe_m_(cpa_safe_m), release_cpa_m_(cpa_safe_m * release_factor) {}

  // Returns whether the rule should be treated as ACTIVE this cycle.
  // past_and_clear: target is abaft the beam (Rule 16 finally-past-and-clear test).
  bool update(bool rule_active, double cpa_m, bool range_closing, bool past_and_clear) {
    if (!latched_) {
      // Latch only on a genuine onset: rule fired AND threat is real.
      if (rule_active && cpa_m < cpa_safe_m_ && range_closing) latched_ = true;
      return latched_;
    }
    // Latched: release only once the encounter is finally past and clear.
    // Never release while the range is still closing.
    const bool opening = !range_closing;
    if (opening && (past_and_clear || cpa_m > release_cpa_m_)) latched_ = false;
    return latched_;
  }

  bool latched() const { return latched_; }

 private:
  double cpa_safe_m_;
  double release_cpa_m_;
  bool latched_{false};
};

}  // namespace mass_l3::m6_colregs
