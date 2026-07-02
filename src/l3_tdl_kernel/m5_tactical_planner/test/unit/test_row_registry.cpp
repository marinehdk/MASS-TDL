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

namespace {
constexpr double kInf = std::numeric_limits<double>::infinity();

// Expect a bound equals +inf (avoids floating ambiguity).
void ExpectInf(double v) { EXPECT_EQ(v, kInf); }
void ExpectNegInf(double v) { EXPECT_EQ(v, -kInf); }
}  // namespace

// ---------------------------------------------------------------------------
// total_rows: fixed-class spans must be contiguous (no gaps, no overlap).
// Layout: [ROT 2(N-1)][prefix_psi_eq N][prefix_u_eq N][CPA n_targets*N]
//         [direction N][min_alt N][terminal 3]  (rule/zone appended after)
// ---------------------------------------------------------------------------
TEST(RowRegistry, totalRowsIsSumOfClassSpans) {
  const RowRegistry reg(/*N=*/18, /*n_targets=*/2, /*n_rule_rows=*/4,
                        /*n_zone_rows=*/8);
  // ROT 2*(18-1)=34, prefix_psi 18, prefix_u 18, CPA 2*18=36, dir 18,
  // minalt 18, term 3, rule 4, zone 8 → 34+18+18+36+18+18+3+4+8 = 157
  EXPECT_EQ(reg.total_rows(), 34 + 18 + 18 + 36 + 18 + 18 + 3 + 4 + 8);
}

TEST(RowRegistry, classRangesAreContiguousAndOrdered) {
  const RowRegistry reg(/*N=*/6, /*n_targets=*/1, /*n_rule_rows=*/0,
                        /*n_zone_rows=*/0);
  // ROT 2*(6-1)=10 → [0,10), prefix_psi_eq [10,16), prefix_u_eq [16,22),
  // CPA n_targets*N=6 → [22,28), direction [28,34), min_alt [34,40),
  // terminal [40,43), rule [43,43), zone [43,43).
  EXPECT_EQ(reg.rot_row_start(), 0);
  EXPECT_EQ(reg.rot_row_end(), 10);          // 2*(6-1)
  EXPECT_EQ(reg.prefix_psi_eq_row(0), 10);
  EXPECT_EQ(reg.prefix_psi_eq_row(5), 15);
  EXPECT_EQ(reg.prefix_u_eq_row(0), 16);
  EXPECT_EQ(reg.prefix_u_eq_row(5), 21);
  EXPECT_EQ(reg.cpa_row(/*t=*/0, /*k=*/0), 22);
  EXPECT_EQ(reg.cpa_row(/*t=*/0, /*k=*/5), 27);
  EXPECT_EQ(reg.direction_row(0), 28);
  EXPECT_EQ(reg.direction_row(5), 33);
  EXPECT_EQ(reg.min_alt_row(0), 34);
  EXPECT_EQ(reg.min_alt_row(5), 39);
  EXPECT_EQ(reg.terminal_row(0), 40);
  EXPECT_EQ(reg.terminal_row(2), 42);
  EXPECT_EQ(reg.rule_row_start(), 43);
  EXPECT_EQ(reg.zone_row_start(), 43);  // 0 rule rows
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
  RowBoundConfig cfg;  // defaults
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
