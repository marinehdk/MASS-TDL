#pragma once

namespace mass_l3::m6_colregs {

// Onset-latched COLREG hysteresis (Rule 13(d): classification fixed at onset;
// later bearing changes do not reclassify). Dual-threshold CPA release prevents
// chattering when own-ship's own maneuver moves the target out of the trigger sector.
class RuleLatch {
 public:
  RuleLatch(double cpa_safe_m, double release_factor)
      : cpa_safe_m_(cpa_safe_m), release_cpa_m_(cpa_safe_m * release_factor) {}

  // Returns whether the rule should be treated as ACTIVE this cycle.
  bool update(bool rule_active, double cpa_m, bool range_closing) {
    if (!latched_) {
      // Latch only on a genuine onset: rule fired AND threat is real.
      if (rule_active && cpa_m < cpa_safe_m_ && range_closing) latched_ = true;
      return latched_;
    }
    // Latched: release only when the encounter is demonstrably resolved —
    // CPA above the (larger) release threshold AND no longer closing.
    if (cpa_m > release_cpa_m_ && !range_closing) latched_ = false;
    return latched_;
  }

  bool latched() const { return latched_; }

 private:
  double cpa_safe_m_;
  double release_cpa_m_;
  bool latched_{false};
};

}  // namespace mass_l3::m6_colregs
