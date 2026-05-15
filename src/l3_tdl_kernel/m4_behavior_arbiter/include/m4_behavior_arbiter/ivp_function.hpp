#pragma once
#include <array>
#include <cstddef>
#include <initializer_list>
#include <string>

namespace mass_l3::m4 {

enum class ErrorCode {
  Ok = 0,
  InvalidPieceCount,
  OutOfRange,
};

struct Piece {
  double heading_min;
  double heading_max;
  double speed_min;
  double speed_max;
  double value;
};

template <size_t Pieces>
class IvPFunction {
public:
  static constexpr size_t kMaxPieces = Pieces;

  ErrorCode set_pieces(std::initializer_list<Piece> pieces) {
    size_t i = 0;
    for (const auto& p : pieces) {
      if (i >= kMaxPieces) {
        return ErrorCode::InvalidPieceCount;
      }
      pieces_[i] = p;
      ++i;
    }
    return ErrorCode::Ok;
  }

  const std::array<Piece, Pieces>& pieces() const { return pieces_; }

  double evaluate(double psi_deg, double u_kn) const {
    for (const auto& p : pieces_) {
      if (psi_deg >= p.heading_min && psi_deg <= p.heading_max &&
          u_kn >= p.speed_min && u_kn <= p.speed_max) {
        return p.value;
      }
    }
    return 0.0;
  }

  double weight{1.0};
  std::string priority{"normal"};

private:
  std::array<Piece, Pieces> pieces_{};
};

using IvPFunctionDefault = IvPFunction<32>;

}  // namespace mass_l3::m4
