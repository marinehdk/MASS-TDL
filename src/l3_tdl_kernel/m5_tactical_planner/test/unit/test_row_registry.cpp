// test/unit/test_row_registry.cpp
// Slice N1: NLP row registry per-class lbg/ubg (spec §3.8 + §10.1 row registry).
//
// Verifies:
//   - prefix equality rows: k<K → equality [0,0]; k>=K → double-disabled [-inf,+inf]
//   - COLREG (CPA/direction/min_alt) prefix-softened: k<K → [-inf,+inf]; k>=K → [0,+inf]
//   - direction_disabled: all direction + min_alt rows [-inf,+inf]
//   - row class ranges contiguous, non-overlapping, non-gappy
//   - default RowBoundConfig (K=0, no soften, no disable) reproduces legacy [0,+inf]
//
// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md §10.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>

#include "m5_tactical_planner/mid_mpc/row_registry.hpp"

using mass_l3::m5::mid_mpc::BoundArray;
using mass_l3::m5::mid_mpc::RowBoundConfig;
using mass_l3::m5::mid_mpc::RowRegistry;
using mass_l3::m5::mid_mpc::kTerminalRowCount;

namespace {
constexpr double kInf = std::numeric_limits<double>::infinity();

// Expect a bound equals +inf (avoids floating ambiguity).
void ExpectInf(double v) { EXPECT_EQ(v, kInf); }
void ExpectNegInf(double v) { EXPECT_EQ(v, -kInf); }
}  // namespace

// ---------------------------------------------------------------------------
// total_rows: fixed-class spans must be contiguous (no gaps, no overlap).
// Layout: [ROT 2N (Fix E: own_psi→psi[0] + N-1 inter-step)][speed_rate N]
//         [prefix_psi_eq N][prefix_u_eq N][CPA n_targets*N]
//         [direction N][min_alt N][terminal 3]  (rule/zone appended after)
// ---------------------------------------------------------------------------
TEST(RowRegistry, totalRowsIsSumOfClassSpans) {
  const RowRegistry reg(/*N=*/18, /*n_targets=*/2, /*n_rule_rows=*/4,
                        /*n_zone_rows=*/8);
  // ROT 2*18=36 (Fix E), speed_rate 18 (Fix D-2), prefix_psi 18, prefix_u 18,
  // CPA 2*18=36, dir 18, minalt 18, term 3, rule 4, zone 8
  EXPECT_EQ(reg.total_rows(), 36 + 18 + 18 + 18 + 36 + 18 + 18 + 3 + 4 + 8);
}

TEST(RowRegistry, classRangesAreContiguousAndOrdered) {
  const RowRegistry reg(/*N=*/6, /*n_targets=*/1, /*n_rule_rows=*/0,
                        /*n_zone_rows=*/0);
  // ROT 2*6=12 → [0,12) (Fix E: rows 0-1 own_psi→psi[0], 2-11 inter-step),
  // speed_rate [12,18), prefix_psi_eq [18,24), prefix_u_eq [24,30),
  // CPA 6 → [30,36), direction [36,42), min_alt [42,48),
  // terminal [48,51), rule/zone [51,51).
  EXPECT_EQ(reg.rot_row_start(), 0);
  EXPECT_EQ(reg.rot_row_end(), 12);          // 2*6 (Fix E)
  EXPECT_EQ(reg.speed_rate_row(0), 12);      // Fix D-2
  EXPECT_EQ(reg.speed_rate_row(5), 17);
  EXPECT_EQ(reg.speed_rate_end(), 18);
  EXPECT_EQ(reg.prefix_psi_eq_row(0), 18);
  EXPECT_EQ(reg.prefix_psi_eq_row(5), 23);
  EXPECT_EQ(reg.prefix_u_eq_row(0), 24);
  EXPECT_EQ(reg.prefix_u_eq_row(5), 29);
  EXPECT_EQ(reg.cpa_row(/*t=*/0, /*k=*/0), 30);
  EXPECT_EQ(reg.cpa_row(/*t=*/0, /*k=*/5), 35);
  EXPECT_EQ(reg.direction_row(0), 36);
  EXPECT_EQ(reg.direction_row(5), 41);
  EXPECT_EQ(reg.min_alt_row(0), 42);
  EXPECT_EQ(reg.min_alt_row(5), 47);
  EXPECT_EQ(reg.terminal_row(0), 48);
  EXPECT_EQ(reg.terminal_row(2), 50);
  EXPECT_EQ(reg.rule_row_start(), 51);
  EXPECT_EQ(reg.zone_row_start(), 51);  // 0 rule rows
}

// ---------------------------------------------------------------------------
// Fix D-2: speed_rate rows always hard [0,+inf] (physical decel limit, no
// per-cycle activation).
// ---------------------------------------------------------------------------
TEST(RowRegistry, speedRateRowsAlwaysHardZeroInf) {
  RowRegistry reg(/*N=*/8, /*n_targets=*/1, /*n_rule_rows=*/0,
                  /*n_zone_rows=*/0);
  RowBoundConfig cfg;  // default
  const BoundArray b = reg.build_bounds(cfg);
  for (int k = 0; k < 8; ++k) {
    const int r = reg.speed_rate_row(k);
    EXPECT_DOUBLE_EQ(b.lbg[static_cast<std::size_t>(r)], 0.0)
        << "speed_rate row " << k << " lbg";
    ExpectInf(b.ubg[static_cast<std::size_t>(r)]);
  }
}

// ---------------------------------------------------------------------------
// prefix equality: k<K → equality [0,0]; k>=K → double-disabled [-inf,+inf]
// (spec §3.8: inactive equality must be double-sided, not single-sided)
// ---------------------------------------------------------------------------
TEST(RowRegistry, prefixPsiEqualityActiveRowsAreEqualityBounds) {
  RowRegistry reg(/*N=*/18, /*n_targets=*/2, /*n_rule_rows=*/0,
                  /*n_zone_rows=*/0);
  RowBoundConfig cfg;
  cfg.K = 4;
  const BoundArray b = reg.build_bounds(cfg);
  // active prefix psi equality: k<4 → [0,0]
  for (int k = 0; k < 4; ++k) {
    const int r = reg.prefix_psi_eq_row(k);
    EXPECT_DOUBLE_EQ(b.lbg[static_cast<std::size_t>(r)], 0.0);
    EXPECT_DOUBLE_EQ(b.ubg[static_cast<std::size_t>(r)], 0.0);
  }
  // inactive: k>=4 → [-inf,+inf] (double-sided disable)
  for (int k = 4; k < 18; ++k) {
    const int r = reg.prefix_psi_eq_row(k);
    ExpectNegInf(b.lbg[static_cast<std::size_t>(r)]);
    ExpectInf(b.ubg[static_cast<std::size_t>(r)]);
  }
}

TEST(RowRegistry, prefixUEqualityActiveRowsAreEqualityBounds) {
  RowRegistry reg(/*N=*/18, /*n_targets=*/2, /*n_rule_rows=*/0,
                  /*n_zone_rows=*/0);
  RowBoundConfig cfg;
  cfg.K = 4;
  const BoundArray b = reg.build_bounds(cfg);
  for (int k = 0; k < 4; ++k) {
    const int r = reg.prefix_u_eq_row(k);
    EXPECT_DOUBLE_EQ(b.lbg[static_cast<std::size_t>(r)], 0.0);
    EXPECT_DOUBLE_EQ(b.ubg[static_cast<std::size_t>(r)], 0.0);
  }
  for (int k = 4; k < 18; ++k) {
    const int r = reg.prefix_u_eq_row(k);
    ExpectNegInf(b.lbg[static_cast<std::size_t>(r)]);
    ExpectInf(b.ubg[static_cast<std::size_t>(r)]);
  }
}

// K=0 (no prefix): ALL prefix equality rows double-disabled [-inf,+inf].
TEST(RowRegistry, prefixEqualityAllInactiveWhenKZero) {
  RowRegistry reg(/*N=*/8, /*n_targets=*/1, /*n_rule_rows=*/0,
                  /*n_zone_rows=*/0);
  RowBoundConfig cfg;  // K=0 default
  const BoundArray b = reg.build_bounds(cfg);
  for (int k = 0; k < 8; ++k) {
    ExpectNegInf(b.lbg[static_cast<std::size_t>(reg.prefix_psi_eq_row(k))]);
    ExpectInf(b.ubg[static_cast<std::size_t>(reg.prefix_psi_eq_row(k))]);
    ExpectNegInf(b.lbg[static_cast<std::size_t>(reg.prefix_u_eq_row(k))]);
    ExpectInf(b.ubg[static_cast<std::size_t>(reg.prefix_u_eq_row(k))]);
  }
}

// ---------------------------------------------------------------------------
// COLREG prefix softening (CPA rows): k<K → [-inf,+inf] softened;
// k>=K → [0,+inf] hard floor (spec §3.3, §6.4).
// ---------------------------------------------------------------------------
TEST(RowRegistry, cpaRowsSoftenedInPrefixAndHardInSuffix) {
  RowRegistry reg(/*N=*/18, /*n_targets=*/2, /*n_rule_rows=*/0,
                  /*n_zone_rows=*/0);
  RowBoundConfig cfg;
  cfg.K = 4;
  cfg.colreg_prefix_softened = true;
  const BoundArray b = reg.build_bounds(cfg);
  for (int t = 0; t < 2; ++t) {
    for (int k = 0; k < 4; ++k) {  // prefix softened
      const int r = reg.cpa_row(t, k);
      ExpectNegInf(b.lbg[static_cast<std::size_t>(r)]);
      ExpectInf(b.ubg[static_cast<std::size_t>(r)]);
    }
    for (int k = 4; k < 18; ++k) {  // suffix hard floor [0,+inf]
      const int r = reg.cpa_row(t, k);
      EXPECT_DOUBLE_EQ(b.lbg[static_cast<std::size_t>(r)], 0.0);
      ExpectInf(b.ubg[static_cast<std::size_t>(r)]);
    }
  }
}

// direction/min_alt prefix soften: k<K → [-inf,+inf]; k>=K → [0,+inf].
TEST(RowRegistry, directionMinAltSoftenedInPrefixWhenColregSoftened) {
  RowRegistry reg(/*N=*/10, /*n_targets=*/1, /*n_rule_rows=*/0,
                  /*n_zone_rows=*/0);
  RowBoundConfig cfg;
  cfg.K = 3;
  cfg.colreg_prefix_softened = true;
  const BoundArray b = reg.build_bounds(cfg);
  for (int k = 0; k < 3; ++k) {
    // prefix softened: lbg=-inf AND ubg=+inf (double-sided, not a half-constraint).
    ExpectNegInf(b.lbg[static_cast<std::size_t>(reg.direction_row(k))]);
    ExpectInf(b.ubg[static_cast<std::size_t>(reg.direction_row(k))]);
    ExpectNegInf(b.lbg[static_cast<std::size_t>(reg.min_alt_row(k))]);
    ExpectInf(b.ubg[static_cast<std::size_t>(reg.min_alt_row(k))]);
  }
  for (int k = 3; k < 10; ++k) {
    // suffix hard floor [0,+inf]: lbg=0 AND ubg=+inf (upper bound must stay open).
    EXPECT_DOUBLE_EQ(b.lbg[static_cast<std::size_t>(reg.direction_row(k))], 0.0);
    ExpectInf(b.ubg[static_cast<std::size_t>(reg.direction_row(k))]);
    EXPECT_DOUBLE_EQ(b.lbg[static_cast<std::size_t>(reg.min_alt_row(k))], 0.0);
    ExpectInf(b.ubg[static_cast<std::size_t>(reg.min_alt_row(k))]);
  }
}

// ---------------------------------------------------------------------------
// K clamp (spec §6.3): an upstream K>N must NOT index out of bounds.
// build_bounds clamps K to [0,N]. With K=25 (>N=18) + softening, K_eff=18 → the
// ENTIRE horizon is the prefix (all CPA/direction/min_alt softened, no suffix),
// and BoundArray stays exactly total_rows() with no OOB write. This only guards
// OOB; spec K_max=N-K_suffix_min validation is C1's responsibility.
// ---------------------------------------------------------------------------
TEST(RowRegistry, kLargerThanHorizonIsClampedNoOutOfBoundsWrite) {
  const int32_t N = 18;
  RowRegistry reg(N, /*n_targets=*/2, /*n_rule_rows=*/0, /*n_zone_rows=*/0);
  RowBoundConfig cfg;
  cfg.K = 25;  // > N (would OOB-iterate without clamp)
  cfg.colreg_prefix_softened = true;
  const BoundArray b = reg.build_bounds(cfg);

  // Size contract: no spurious growth / OOB beyond total_rows().
  EXPECT_EQ(static_cast<int>(b.lbg.size()), reg.total_rows());
  EXPECT_EQ(static_cast<int>(b.ubg.size()), reg.total_rows());

  // K clamped to N → every step is prefix (softened [-inf,+inf]), none is the
  // hard-floor suffix. Verify across all CPA/direction/min_alt rows.
  for (int t = 0; t < 2; ++t) {
    for (int k = 0; k < N; ++k) {
      const int r = reg.cpa_row(t, k);
      ExpectNegInf(b.lbg[static_cast<std::size_t>(r)]);
      ExpectInf(b.ubg[static_cast<std::size_t>(r)]);
    }
  }
  for (int k = 0; k < N; ++k) {
    ExpectNegInf(b.lbg[static_cast<std::size_t>(reg.direction_row(k))]);
    ExpectInf(b.ubg[static_cast<std::size_t>(reg.direction_row(k))]);
    ExpectNegInf(b.lbg[static_cast<std::size_t>(reg.min_alt_row(k))]);
    ExpectInf(b.ubg[static_cast<std::size_t>(reg.min_alt_row(k))]);
  }
}

// ---------------------------------------------------------------------------
// direction_disabled: ALL direction + min_alt rows double-disabled
// (preferred_direction==0 / STAND_ON / HOLD — spec §3.3).
// ---------------------------------------------------------------------------
TEST(RowRegistry, directionDisabledNullifiesAllDirectionAndMinAltRows) {
  RowRegistry reg(/*N=*/10, /*n_targets=*/1, /*n_rule_rows=*/0,
                  /*n_zone_rows=*/0);
  RowBoundConfig cfg;  // K=0, no soften
  cfg.direction_disabled = true;
  const BoundArray b = reg.build_bounds(cfg);
  for (int k = 0; k < 10; ++k) {
    const int dr = reg.direction_row(k);
    const int mr = reg.min_alt_row(k);
    ExpectNegInf(b.lbg[static_cast<std::size_t>(dr)]);
    ExpectInf(b.ubg[static_cast<std::size_t>(dr)]);
    ExpectNegInf(b.lbg[static_cast<std::size_t>(mr)]);
    ExpectInf(b.ubg[static_cast<std::size_t>(mr)]);
  }
}

// ---------------------------------------------------------------------------
// ROT rows: always [0,+inf] (smooth linear, unchanged).
// terminal rows: always [0,+inf] (T1 will gate by role; N1 leaves default).
// rule/zone rows: always [0,+inf] (compiler rows, unchanged).
// ---------------------------------------------------------------------------
TEST(RowRegistry, rotTerminalRuleZoneRowsAreLegacyZeroInfBounds) {
  RowRegistry reg(/*N=*/6, /*n_targets=*/1, /*n_rule_rows=*/2,
                  /*n_zone_rows=*/3);
  RowBoundConfig cfg;  // defaults, except terminal_nlp_soft=false (v2.1 §4.5
                       // default flipped to true in Task 7; this legacy-shape
                       // test pins the hard-terminal semantics explicitly).
  cfg.terminal_nlp_soft = false;
  const BoundArray b = reg.build_bounds(cfg);
  for (int r = reg.rot_row_start(); r < reg.rot_row_end(); ++r) {
    EXPECT_DOUBLE_EQ(b.lbg[static_cast<std::size_t>(r)], 0.0);
    ExpectInf(b.ubg[static_cast<std::size_t>(r)]);
  }
  for (int i = 0; i < 3; ++i) {
    const int r = reg.terminal_row(i);
    EXPECT_DOUBLE_EQ(b.lbg[static_cast<std::size_t>(r)], 0.0);
    ExpectInf(b.ubg[static_cast<std::size_t>(r)]);
  }
  for (int r = reg.rule_row_start(); r < reg.zone_row_start(); ++r) {
    EXPECT_DOUBLE_EQ(b.lbg[static_cast<std::size_t>(r)], 0.0);
    ExpectInf(b.ubg[static_cast<std::size_t>(r)]);
  }
  for (int r = reg.zone_row_start(); r < reg.total_rows(); ++r) {
    EXPECT_DOUBLE_EQ(b.lbg[static_cast<std::size_t>(r)], 0.0);
    ExpectInf(b.ubg[static_cast<std::size_t>(r)]);
  }
}

// ---------------------------------------------------------------------------
// Default RowBoundConfig (K=0, no soften, no disable) reproduces legacy
// zeros/inf for ALL fixed-class rows EXCEPT prefix equality (which is
// double-disabled [-inf,+inf] when K=0, matching the "no prefix" state).
// This is the N1 first-version no-op behavior guarantee.
// ---------------------------------------------------------------------------
TEST(RowRegistry, defaultConfigReproducesLegacyBoundsForNonPrefixClasses) {
  RowRegistry reg(/*N=*/8, /*n_targets=*/2, /*n_rule_rows=*/0,
                  /*n_zone_rows=*/0);
  RowBoundConfig cfg;  // K=0, soften=false, direction_disabled=false
  const BoundArray b = reg.build_bounds(cfg);
  // CPA: all [0,+inf] (no soften → hard everywhere)
  for (int t = 0; t < 2; ++t) {
    for (int k = 0; k < 8; ++k) {
      EXPECT_DOUBLE_EQ(
          b.lbg[static_cast<std::size_t>(reg.cpa_row(t, k))], 0.0);
      ExpectInf(b.ubg[static_cast<std::size_t>(reg.cpa_row(t, k))]);
    }
  }
  // direction/min_alt: all [0,+inf] (not disabled, no soften)
  for (int k = 0; k < 8; ++k) {
    EXPECT_DOUBLE_EQ(
        b.lbg[static_cast<std::size_t>(reg.direction_row(k))], 0.0);
    EXPECT_DOUBLE_EQ(b.lbg[static_cast<std::size_t>(reg.min_alt_row(k))], 0.0);
  }
}

// ---------------------------------------------------------------------------
// BoundArray size == total_rows (solver sizing contract).
// ---------------------------------------------------------------------------
TEST(RowRegistry, boundArraySizeMatchesTotalRows) {
  RowRegistry reg(/*N=*/18, /*n_targets=*/3, /*n_rule_rows=*/5,
                  /*n_zone_rows=*/7);
  const BoundArray b = reg.build_bounds(RowBoundConfig{});
  EXPECT_EQ(static_cast<int>(b.lbg.size()), reg.total_rows());
  EXPECT_EQ(static_cast<int>(b.ubg.size()), reg.total_rows());
}

// v2.1 spec §4.2/§4.3/§4.5 — new RowBoundConfig fields default values.
// NOTE: terminal_nlp_soft defaults FALSE in Task 1; flipped to TRUE in Task 7
// after upper-band cost (Task 5) + tail-gate lateral (Task 7) land.
TEST(RowRegistry, V21FieldsHaveCorrectDefaults) {
  RowBoundConfig cfg;
  EXPECT_EQ(cfg.minalt_hard_from_k, 0);
  EXPECT_FALSE(cfg.minalt_override_valid);
  EXPECT_EQ(cfg.cpa_hard_from_k, 0);
  EXPECT_FALSE(cfg.cpa_override_valid);
  EXPECT_TRUE(cfg.terminal_nlp_soft);  // Task 7 flips default to true
}

// v2.1 spec §4.2 — min_alt reachable schedule. k<deadline soft, k>=deadline hard.
TEST(RowRegistry, MinaltReachableScheduleSoftensBeforeDeadline) {
  RowRegistry reg;
  reg.reset(/*N=*/18, /*n_targets=*/1, /*n_rule=*/0, /*n_zone=*/0);
  RowBoundConfig cfg;
  cfg.K = 0;
  cfg.direction_disabled = false;
  cfg.minalt_hard_from_k = 2;  // k<2 soft, k>=2 hard
  cfg.minalt_override_valid = true;
  BoundArray b = reg.build_bounds(cfg);
  for (int32_t k = 0; k < 18; ++k) {
    const std::size_t r = static_cast<std::size_t>(reg.min_alt_row(k));
    if (k < 2) {
      EXPECT_EQ(b.lbg[r], -std::numeric_limits<double>::infinity()) << "k=" << k;
      EXPECT_EQ(b.ubg[r],  std::numeric_limits<double>::infinity()) << "k=" << k;
    } else {
      EXPECT_EQ(b.lbg[r], 0.0) << "k=" << k;
      EXPECT_EQ(b.ubg[r], std::numeric_limits<double>::infinity()) << "k=" << k;
    }
  }
}

// direction_disabled wins over minalt schedule (B9): ALL minalt rows disabled.
TEST(RowRegistry, MinaltScheduleIgnoredWhenDirectionDisabled) {
  RowRegistry reg;
  reg.reset(18, 1, 0, 0);
  RowBoundConfig cfg;
  cfg.direction_disabled = true;       // stand-on / HOLD / ReduceSpeed
  cfg.minalt_hard_from_k = 5;          // would soften k<5, but ignored
  cfg.minalt_override_valid = true;
  BoundArray b = reg.build_bounds(cfg);
  for (int32_t k = 0; k < 18; ++k) {
    const std::size_t r = static_cast<std::size_t>(reg.min_alt_row(k));
    EXPECT_EQ(b.lbg[r], -std::numeric_limits<double>::infinity()) << "k=" << k;
    EXPECT_EQ(b.ubg[r],  std::numeric_limits<double>::infinity()) << "k=" << k;
  }
}

// v2.1 spec §4.3 — CPA suffix-hard. Row order: cpa_row(t,k)=cpa_start+k*n_targets+t
TEST(RowRegistry, CpaSuffixHardSoftensBeforeDeadline) {
  RowRegistry reg;
  reg.reset(/*N=*/18, /*n_targets=*/2, /*n_rule=*/0, /*n_zone=*/0);
  RowBoundConfig cfg;
  cfg.cpa_hard_from_k = 3;  // k<3 soft, k>=3 hard
  cfg.cpa_override_valid = true;
  BoundArray b = reg.build_bounds(cfg);
  for (int32_t k = 0; k < 18; ++k) {
    for (int32_t t = 0; t < 2; ++t) {
      const std::size_t r = static_cast<std::size_t>(reg.cpa_row(t, k));
      if (k < 3) {
        EXPECT_EQ(b.lbg[r], -std::numeric_limits<double>::infinity()) << "k=" << k << " t=" << t;
        EXPECT_EQ(b.ubg[r],  std::numeric_limits<double>::infinity()) << "k=" << k << " t=" << t;
      } else {
        EXPECT_EQ(b.lbg[r], 0.0) << "k=" << k << " t=" << t;
        EXPECT_EQ(b.ubg[r], std::numeric_limits<double>::infinity()) << "k=" << k << " t=" << t;
      }
    }
  }
}

// v2.1 spec §4.5 — terminal_nlp_soft=true disables all 3 terminal rows.
TEST(RowRegistry, TerminalNlpSoftDisablesAllTerminalRows) {
  RowRegistry reg;
  reg.reset(18, 1, 0, 0);
  RowBoundConfig cfg;
  cfg.terminal_disabled = false;     // give-way lateral (not stand-on)
  cfg.terminal_nlp_soft = true;      // v2.1 default
  BoundArray b = reg.build_bounds(cfg);
  for (int32_t i = 0; i < kTerminalRowCount; ++i) {
    const std::size_t r = static_cast<std::size_t>(reg.terminal_row(i));
    EXPECT_EQ(b.lbg[r], -std::numeric_limits<double>::infinity()) << "terminal row " << i;
    EXPECT_EQ(b.ubg[r],  std::numeric_limits<double>::infinity()) << "terminal row " << i;
  }
}

// terminal_nlp_soft=false + terminal_disabled=false -> legacy hard terminal.
TEST(RowRegistry, TerminalNlpSoftFalseKeepsHardForGiveWay) {
  RowRegistry reg;
  reg.reset(18, 1, 0, 0);
  RowBoundConfig cfg;
  cfg.terminal_disabled = false;
  cfg.terminal_nlp_soft = false;     // legacy v2
  BoundArray b = reg.build_bounds(cfg);
  // terminal_row(0) is g_term_side (hard [0,+inf]); (1)/(2) are lo/hi
  const std::size_t r0 = static_cast<std::size_t>(reg.terminal_row(0));
  EXPECT_EQ(b.lbg[r0], 0.0);  // legacy hard lower bound
}
