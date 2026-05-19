#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "m4_behavior_arbiter/error.hpp"
#include "m4_behavior_arbiter/types.hpp"

namespace mass_l3::m4 {

/**
 * @brief Piecewise-linear interval-valued utility function over (heading, speed).
 *
 * Per [R3] Benjamin et al. 2010. Stored as stack-allocated array of up to Pieces
 * rectangular pieces. Heading wraps at 360°. Utility ∈ [0, 1].
 *
 * @tparam Pieces Compile-time max piece count (default 32, matches ivp.max_pieces_per_function).
 */
template <size_t Pieces = 32>
class IvPFunction {
 public:
  /**
   * @brief Single rectangular piece: heading × speed → utility.
   */
  struct Piece {
    double heading_min_deg;  ///< [0, 360)
    double heading_max_deg;  ///< [0, 360); may be < heading_min_deg for wrap-around pieces
    double speed_min_kn;
    double speed_max_kn;
    double utility;          ///< [0, 1]
  };

  /// @brief Default-constructs an empty function (zero pieces).
  IvPFunction() = default;

  /**
   * @brief Set pieces; validates count ≤ Pieces and utility ∈ [0,1].
   * @param pieces Vector of pieces to store.
   * @return ErrorCode::Ok or YamlInvalidValue on validation failure.
   * @post piece_count() == pieces.size() if Ok.
   */
  ErrorCode set_pieces(const std::vector<Piece>& pieces);

  /**
   * @brief Evaluate utility at (psi_deg, u_kn) with 360° heading wrap.
   * @param psi_deg Heading in degrees; wrapped to [0, 360) internally.
   * @param u_kn Speed in knots.
   * @return Utility of the first matching piece (first-match-wins), or 0.0 if no piece matches.
   */
  double evaluate(double psi_deg, double u_kn) const;

  /**
   * @brief Number of active pieces.
   * @return Piece count set via set_pieces().
   */
  size_t piece_count() const { return piece_count_; }

  /**
   * @brief Read-only piece access.
   * @param i Index in [0, piece_count()).
   * @return Const reference to the piece.
   * @throws std::out_of_range if i >= piece_count().
   */
  const Piece& piece(size_t i) const;

 private:
  std::array<Piece, Pieces> pieces_{};
  size_t piece_count_{0};

  static constexpr double kEps = 1e-9;

  // Wrap heading to [0, 360)
  static double wrap_heading(double psi_deg);

  // Check if wrapped heading is inside piece interval (handles wrap-around)
  static bool heading_in_piece(double psi_wrapped, const Piece& p);
};

/// Default instantiation alias
using IvPFunctionDefault = IvPFunction<32>;

// ============================================================================
// Template Implementation
// ============================================================================

template <size_t Pieces>
double IvPFunction<Pieces>::wrap_heading(double psi_deg) {
  double wrapped = std::fmod(psi_deg, 360.0);
  if (wrapped < 0.0) {
    wrapped += 360.0;
  }
  return wrapped;
}

template <size_t Pieces>
bool IvPFunction<Pieces>::heading_in_piece(double psi_wrapped, const Piece& p) {
  // Normal case: heading_min <= heading_max
  // kEps (1e-9) << 1 deg; distinguishes normal from wrap-around without false positives
  if (p.heading_min_deg <= p.heading_max_deg + kEps) {
    return psi_wrapped >= p.heading_min_deg - kEps && psi_wrapped <= p.heading_max_deg + kEps;
  }

  // Wrap-around case: heading_min > heading_max (e.g., 350° → 10°)
  // Check if psi >= min OR psi <= max
  return psi_wrapped >= p.heading_min_deg - kEps || psi_wrapped <= p.heading_max_deg + kEps;
}

template <size_t Pieces>
ErrorCode IvPFunction<Pieces>::set_pieces(const std::vector<Piece>& pieces) {
  // Check piece count
  if (pieces.size() > Pieces) {
    return ErrorCode::YamlInvalidValue;
  }

  // Validate utility values and heading ranges
  for (const auto& p : pieces) {
    if (p.utility < -kEps || p.utility > 1.0 + kEps) {
      return ErrorCode::YamlInvalidValue;
    }

    // Validate heading range (must not be degenerate: 0-width or full-circle)
    const double h_span = (p.heading_max_deg >= p.heading_min_deg)
        ? (p.heading_max_deg - p.heading_min_deg)
        : (360.0 - p.heading_min_deg + p.heading_max_deg);
    if (h_span < kEps || h_span >= 360.0 - kEps) {
      return ErrorCode::YamlInvalidValue;
    }
  }

  // Copy to pieces_ array
  for (size_t i = 0; i < pieces.size(); ++i) {
    pieces_[i] = pieces[i];
  }
  piece_count_ = pieces.size();

  return ErrorCode::Ok;
}

template <size_t Pieces>
double IvPFunction<Pieces>::evaluate(double psi_deg, double u_kn) const {
  double psi_wrapped = wrap_heading(psi_deg);

  for (size_t i = 0; i < piece_count_; ++i) {
    const auto& p = pieces_[i];

    // Check if (psi, u) falls within this piece
    if (heading_in_piece(psi_wrapped, p) && u_kn >= p.speed_min_kn &&
        u_kn <= p.speed_max_kn) {
      return p.utility;
    }
  }

  return 0.0;
}

template <size_t Pieces>
const typename IvPFunction<Pieces>::Piece& IvPFunction<Pieces>::piece(size_t i) const {
  if (i >= piece_count_) {
    throw std::out_of_range("IvPFunction::piece: index out of range");
  }
  return pieces_[i];
}

}  // namespace mass_l3::m4
