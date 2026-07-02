#ifndef MASS_L3_M5_MID_MPC_ROW_REGISTRY_HPP_
#define MASS_L3_M5_MID_MPC_ROW_REGISTRY_HPP_

// M5 Tactical Planner — Mid-MPC NLP Row Registry (Slice N1, spec §3.8).
//
// Manages the FIXED-ORDER row-class layout of the general-constraint vector g:
//   [ROT][prefix_psi_eq][prefix_u_eq][CPA][direction][min_alt][terminal]
//   [rule][zone]
//
// The class ORDER is fixed (never re-ordered by K or runtime flags, spec §3.8),
// so a given logical constraint always lives at the same row class even as the
// active prefix length K changes. The row COUNT inside CPA/rule/zone varies at
// runtime (depends on n_targets / applicable rules / zones), so the registry is
// rebuilt each Mid-MPC cycle (G1 rebuild model, spec §3.6).
//
// build_bounds() maps a per-cycle RowBoundConfig into the per-row lbg/ubg
// vectors implementing:
//   - prefix-equality rows: k<K → equality [0,0]; k>=K → DOUBLE-disabled
//     [-inf,+inf] (both sides, never single-sided, to avoid half-constraints).
//   - COLREG hard rows (CPA/direction/min_alt) prefix-softened when
//     colreg_prefix_softened: k<K → [-inf,+inf]; k>=K → [0,+inf] hard floor.
//   - direction_disabled: all direction + min_alt rows double-disabled
//     (preferred_direction==0 / STAND_ON / HOLD — spec §3.3).
//
// N1 first version default (K=0, soften=false, direction_disabled=false) makes
// every fixed-class row except prefix-equality use the legacy [0,+inf], and
// prefix-equality rows become [-inf,+inf] (no-op). This is the no-behavior-change
// guarantee: C1/D1/T1 later populate the real expressions + K>0.
//
// PATH-D (MISRA C++:2023): ≤60 lines per function, CC ≤10, no float.
// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md §10.

#include <cstdint>
#include <limits>
#include <vector>

namespace mass_l3::m5::mid_mpc {

// Terminal-class row count: g_term_side / g_term_lo / g_term_hi (spec §5.5).
// Fixed (3 rows), populated by Slice T1.
constexpr int32_t kTerminalRowCount = 3;

// ---------------------------------------------------------------------------
// RowBoundConfig — per-cycle dynamic bound policy (spec §3.8).
// Built by assemble_input_ / solver from M4/M6 state and committed prefix.
// ---------------------------------------------------------------------------
struct RowBoundConfig {
  // Active committed-prefix length K (spec §6.3). Rows with k<K are the frozen
  // committed geometry; k>=K are the free suffix. 0 = no prefix (first commit).
  int32_t K{0};
  // True → prefix segment (k<K) COLREG hard rows softened to [-inf,+inf] so a
  // target moving into the frozen geometry cannot make the NLP infeasible
  // (spec §6.4). Suffix (k>=K) keeps the [0,+inf] hard floor.
  bool colreg_prefix_softened{false};
  // True → direction + min_alt rows fully disabled (preferred_direction==0,
  // primary_role==STAND_ON, or M4 behavior HOLD/REDUCE_SPEED — spec §3.3).
  bool direction_disabled{false};
};

// ---------------------------------------------------------------------------
// BoundArray — output lbg/ubg pair sized to total_rows().
// ---------------------------------------------------------------------------
struct BoundArray {
  std::vector<double> lbg;
  std::vector<double> ubg;
};

// ---------------------------------------------------------------------------
// RowRegistry — fixed-class g row layout + per-row bound builder.
//
// Constructed per Mid-MPC cycle after the compiler rows are known (n_targets
// from ConstraintInputs.targets, n_rule_rows / n_zone_rows from the compiled
// ConstraintCompiler outputs). The registry only tracks indices and bounds;
// the symbolic g expressions are assembled by MidMpcNlpFormulation in the SAME
// fixed order so indices line up.
// ---------------------------------------------------------------------------
class RowRegistry {
 public:
  // Default: zero-row layout (replaced per-cycle in build_symbolic_graph).
  RowRegistry() noexcept = default;

  // @param N            Horizon length.
  // @param n_targets    Actual target count this cycle (drives CPA row count).
  // @param n_rule_rows  Compiled COLREGS-rule row count (ConstraintCompiler).
  // @param n_zone_rows  Compiled zone row count (ConstraintCompiler).
  RowRegistry(int32_t N, int32_t n_targets, int32_t n_rule_rows,
              int32_t n_zone_rows) noexcept
      : N_(N),
        n_targets_(n_targets),
        rot_end_(2 * (N - 1)),
        prefix_psi_end_(rot_end_ + N),
        prefix_u_end_(prefix_psi_end_ + N),
        cpa_end_(prefix_u_end_ + n_targets * N),
        dir_end_(cpa_end_ + N),
        minalt_end_(dir_end_ + N),
        term_end_(minalt_end_ + kTerminalRowCount),
        rule_end_(term_end_ + n_rule_rows),
        zone_end_(rule_end_ + n_zone_rows) {}

  // Re-bind to a new (N, n_targets, n_rule_rows, n_zone_rows) layout. Used by
  // the formulation each cycle (default-constructed member re-assigned in-place).
  void reset(int32_t N, int32_t n_targets, int32_t n_rule_rows,
             int32_t n_zone_rows) noexcept {
    *this = RowRegistry(N, n_targets, n_rule_rows, n_zone_rows);
  }

  // ── Per-row accessors ───────────────────────────────────────────────────
  // ROT: 2 smooth linear rows per step (hi/lo), k ∈ [0, N-2).
  [[nodiscard]] int32_t rot_row_start() const noexcept { return 0; }
  [[nodiscard]] int32_t rot_row_end() const noexcept { return rot_end_; }

  // prefix psi/u equality: one row per step k ∈ [0, N).
  [[nodiscard]] int32_t prefix_psi_eq_row(int32_t k) const noexcept {
    return rot_end_ + k;
  }
  [[nodiscard]] int32_t prefix_u_eq_row(int32_t k) const noexcept {
    return prefix_psi_end_ + k;
  }

  // CPA hard floor: k-MAJOR layout matching ConstraintCompiler::compile_cpa_distance
  // (outer loop k, inner loop t). Row(t,k) = cpa_start + k*n_targets + t.
  [[nodiscard]] int32_t cpa_row(int32_t t, int32_t k) const noexcept {
    return prefix_u_end_ + k * n_targets_ + t;
  }

  // direction / min_alt: one row per step k ∈ [0, N).
  [[nodiscard]] int32_t direction_row(int32_t k) const noexcept {
    return cpa_end_ + k;
  }
  [[nodiscard]] int32_t min_alt_row(int32_t k) const noexcept {
    return dir_end_ + k;
  }

  // terminal: 3 rows (side / lo / hi), populated by Slice T1.
  [[nodiscard]] int32_t terminal_row(int32_t i) const noexcept {
    return minalt_end_ + i;  // i ∈ {0,1,2}
  }

  // rule / zone: appended after terminal, counts from ConstraintCompiler.
  [[nodiscard]] int32_t rule_row_start() const noexcept { return term_end_; }
  [[nodiscard]] int32_t zone_row_start() const noexcept { return rule_end_; }
  [[nodiscard]] int32_t total_rows() const noexcept { return zone_end_; }

  // ── Bound builder ───────────────────────────────────────────────────────
  // Produces lbg/ubg of length total_rows() implementing the spec §3.8 +
  // §3.3 policies. Default config reproduces legacy [0,+inf] everywhere except
  // prefix-equality (which is [-inf,+inf] when K=0).
  //
  // K clamp (spec §6.3): the active prefix K is caller-supplied (C1 derives it
  // from the GNC guard). A defensive clamp to [0, N_] guards against an upstream
  // K>N that would otherwise index out of bounds in the prefix loops. This does
  // NOT validate K against the spec K_max=N-K_suffix_min (that is C1's duty);
  // it only prevents OOB writes. K>N clamps to N (entire horizon = prefix).
  [[nodiscard]] BoundArray build_bounds(const RowBoundConfig& cfg) const {
    const int32_t n = total_rows();
    BoundArray b;
    const auto nu = static_cast<std::size_t>(n);
    b.lbg.resize(nu, 0.0);
    b.ubg.resize(nu, std::numeric_limits<double>::infinity());

    // Clamp K to [0, N_] so prefix loops never index past the horizon.
    RowBoundConfig cfg_eff = cfg;
    if (cfg_eff.K < 0) { cfg_eff.K = 0; }
    if (cfg_eff.K > N_) { cfg_eff.K = N_; }

    apply_prefix_equality_(cfg_eff, b);
    if (cfg_eff.colreg_prefix_softened) { apply_colreg_prefix_soften_(cfg_eff, b); }
    if (cfg_eff.direction_disabled) { apply_direction_disable_(b); }
    return b;
  }

 private:
  int32_t N_{0};
  int32_t n_targets_{0};
  int32_t rot_end_{0};
  int32_t prefix_psi_end_{0};
  int32_t prefix_u_end_{0};
  int32_t cpa_end_{0};
  int32_t dir_end_{0};
  int32_t minalt_end_{0};
  int32_t term_end_{0};
  int32_t rule_end_{0};
  int32_t zone_end_{0};

  static constexpr double kInf = std::numeric_limits<double>::infinity();

  // prefix-equality: active k<K → [0,0]; inactive k>=K → [-inf,+inf] (double).
  void apply_prefix_equality_(const RowBoundConfig& cfg, BoundArray& b) const {
    for (int32_t k = 0; k < N_; ++k) {
      const bool active = (k < cfg.K);
      const double lb = active ? 0.0 : -kInf;
      const double ub = active ? 0.0 : kInf;
      const std::size_t rp = static_cast<std::size_t>(prefix_psi_eq_row(k));
      const std::size_t ru = static_cast<std::size_t>(prefix_u_eq_row(k));
      b.lbg[rp] = lb; b.ubg[rp] = ub;
      b.lbg[ru] = lb; b.ubg[ru] = ub;
    }
  }

  // COLREG prefix soften (k<K): CPA + direction + min_alt → [-inf,+inf].
  void apply_colreg_prefix_soften_(const RowBoundConfig& cfg,
                                   BoundArray& b) const {
    for (int32_t t = 0; t < n_targets_; ++t) {
      for (int32_t k = 0; k < cfg.K; ++k) {
        const std::size_t r = static_cast<std::size_t>(cpa_row(t, k));
        b.lbg[r] = -kInf; b.ubg[r] = kInf;
      }
    }
    for (int32_t k = 0; k < cfg.K; ++k) {
      const std::size_t rd = static_cast<std::size_t>(direction_row(k));
      const std::size_t rm = static_cast<std::size_t>(min_alt_row(k));
      b.lbg[rd] = -kInf; b.ubg[rd] = kInf;
      b.lbg[rm] = -kInf; b.ubg[rm] = kInf;
    }
  }

  // direction_disabled: ALL direction + min_alt rows → [-inf,+inf].
  void apply_direction_disable_(BoundArray& b) const {
    for (int32_t k = 0; k < N_; ++k) {
      const std::size_t rd = static_cast<std::size_t>(direction_row(k));
      const std::size_t rm = static_cast<std::size_t>(min_alt_row(k));
      b.lbg[rd] = -kInf; b.ubg[rd] = kInf;
      b.lbg[rm] = -kInf; b.ubg[rm] = kInf;
    }
  }
};

}  // namespace mass_l3::m5::mid_mpc

#endif  // MASS_L3_M5_MID_MPC_ROW_REGISTRY_HPP_
